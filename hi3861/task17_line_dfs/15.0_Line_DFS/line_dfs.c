/*
 * Trace following v6.3 "DFS junction backtracking" line follower.
 *
 * Sensor semantics (as observed on the car):
 *   sensor on the black tape  => "white" reading (line_* = 1)
 *   sensor on normal ground   => "black" reading (line_* = 0)
 *   IR GPIO13 = left probe, GPIO14 = right probe; 2-sample debounce both ways.
 *
 * Behavior:
 *   1. Normal line following: single probe on tape => gentle correction
 *      (tape under left probe => steer left, tape under right => steer right);
 *      both probes on ground => straight.
 *   2. At a double-white junction, try LEFT first, then RIGHT if the
 *      left branch is later found to be a dead end.
 *   3. A dead end is heuristically detected as both probes seeing ground
 *      continuously for TRACE_DEAD_END_TICKS after the line was acquired.
 *      The car reverses until it reaches the previous double-white junction,
 *      then tries the unvisited branch. This is a bounded DFS stack.
 *
 * Limitation: with only two probes, a long straight section and a dead end
 *      can both look like double-black. TRACE_DEAD_END_TICKS therefore needs
 *      calibration on the actual track. There is no reliable finish marker
 *      in the current hardware interface; endpoint detection is not inferred.
 *   Each turn has a timeout so the car can never spin forever.
 *
 * Motor link unchanged: GPIO11 UART2_TXD -> STM32, 115200 8N1,
 * AA | CMD | LEN | PAYLOAD | CHECK, SET_SPEED / STOP. The 100 ms SET_SPEED
 * resend doubles as the motion-lease heartbeat (STM32 stops on its own when
 * the 300 ms lease expires).
 *
 * Safety: motion is disabled by default (TRACE_ENABLE_MOTION 0). Stale or
 * failed sensor data forces a local STOP.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include "ohos_init.h"
#include "cmsis_os2.h"
#include "wifiiot_errno.h"
#include "wifiiot_gpio.h"
#include "wifiiot_gpio_ex.h"
#include "wifiiot_uart.h"
#include "wifiiot_uart_ex.h"
#include "hi_time.h"

/* Keep this 0 until the static and wheels-off-ground checks are complete. */
#define TRACE_ENABLE_MOTION               0

/* BLE debug mirror: JDY-16 (Gamer_0o0) on UART1, phone app watches the log. */
#define TRACE_BLE_DEBUG                   1
#define BLE_DEBUG_BAUD                    9600U
#define BLE_HEARTBEAT_TICKS               100U /* 1 s status heartbeat over BLE */

#define PROTOCOL_SOF                      0xAAU
#define MAX_PAYLOAD                       16U
#define PROTOCOL_FRAME_MAX                (MAX_PAYLOAD + 4U)
#define PROTOCOL_CMD_SET_SPEED            0x01U
#define PROTOCOL_CMD_STOP                 0x02U
#define PROTOCOL_CMD_ACK                  0x81U

#define PROTOCOL_STATUS_OK                0U
#define PROTOCOL_STATUS_BAD_LENGTH        1U
#define PROTOCOL_STATUS_BAD_CHECKSUM      2U
#define PROTOCOL_STATUS_INVALID_PARAM     3U
#define PROTOCOL_STATUS_UNKNOWN_CMD       4U
#define PROTOCOL_STATUS_UNKNOWN_STATUS    255U

#define TRACE_THREAD_STACK_SIZE           4096U
#define TRACE_THREAD_PRIORITY             25
#define ACK_READ_BUFFER_SIZE              64U
#define ACK_POLL_DELAY_TICKS              1U

/* Hi3861 uses a 10 ms RTOS tick in this project. */
#define SENSOR_SAMPLE_TICKS               1U   /* 10 ms */
#define TRACE_DEBOUNCE_SAMPLES            2U   /* 2-sample confirm both ways = 20 ms */
#define CONTROL_LOOP_TICKS                1U   /* 10 ms reaction */
#define COMMAND_RESEND_TICKS              10U  /* 100 ms, below STM32's 300 ms lease */
#define SENSOR_STALE_TICKS                12U  /* 120 ms without a fresh sample => STOP */
#define STARTUP_GUARD_TICKS               100U /* 1 s initial STOP guard */
#define STATUS_LOG_TICKS                  10U  /* 100 ms cadence for status prints */

#define DRIVE_SPEED                       80   /* straight-line speed */
#define CORRECT_INNER_SPEED               50   /* slower / inside wheel of a correction */
#define CORRECT_OUTER_SPEED               70   /* faster / outside wheel of a correction */
/* In-place pivot used only for the two crossing turns. Tune on the car: too
   fast overshoots the sensor exit; too slow crawls through the junction. */
#define TURN_SPEED                        40
#define TURN_TIMEOUT_TICKS                300U /* 3 s hard stop on a turn */
#define BACKTRACK_SPEED                   45
#define BACKTRACK_TIMEOUT_TICKS           800U /* 8 s maximum reverse */
#define TRACE_DEAD_END_TICKS              80U  /* 800 ms double-black heuristic */
#define TRACE_STACK_MAX                   16U
#define TRACE_BACKTRACK_JUNCTION_GUARD    8U  /* do not retrigger same junction */

#define IR_LEFT_PIN                       WIFI_IOT_IO_NAME_GPIO_13
#define IR_RIGHT_PIN                      WIFI_IOT_IO_NAME_GPIO_14

/* The SDK provides this symbol even when older headers omit the declaration. */
extern unsigned int UartIsBufEmpty(WifiIotUartIdx id, unsigned char *empty);

#if TRACE_BLE_DEBUG
static void ble_debug_printf(const char *format, ...)
{
    char buffer[160];
    va_list args;
    int length;

    va_start(args, format);
    length = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    if (length <= 0) {
        return;
    }
    if (length >= (int)sizeof(buffer)) {
        length = (int)sizeof(buffer) - 1;
    }
    (void)UartWrite(WIFI_IOT_UART_IDX_1, (const uint8_t *)buffer, (unsigned int)length);
}
#else
#define ble_debug_printf(...) ((void)0)
#endif

typedef enum {
    RX_WAIT_SOF = 0,
    RX_CMD,
    RX_LEN,
    RX_PAYLOAD,
    RX_CHECK
} ProtocolRxState;

typedef struct {
    ProtocolRxState state;
    uint8_t cmd;
    uint8_t len;
    uint8_t index;
    uint8_t checksum;
    uint8_t payload[MAX_PAYLOAD];
} ProtocolRxParser;

typedef struct {
    uint8_t candidate;
    uint8_t stable;
    uint8_t count;
    uint8_t ready;
} SensorDebounce;

typedef struct {
    uint8_t raw_left;
    uint8_t raw_right;
    uint8_t line_left;   /* debounced: 1 = left probe on the tape (white reading) */
    uint8_t line_right;
    uint8_t ready;
    uint8_t healthy;
    uint32_t updated_tick;
    uint32_t sequence;
} SensorSnapshot;

typedef struct {
    int16_t left;
    int16_t right;
    uint8_t stop;
} MotionCommand;

typedef enum {
    TRACE_FOLLOW = 0,   /* normal line following */
    TRACE_TURN_LEFT,    /* pivot left until the right probe is black */
    TRACE_TURN_RIGHT,   /* pivot right until the left probe is black */
    TRACE_BACKTRACK,    /* reverse to the most recent junction */
    TRACE_FAILED        /* every branch from the start has been exhausted */
} TraceState;

typedef struct {
    uint8_t tried_left;
    uint8_t tried_right;
} TraceJunction;

typedef struct {
    TraceState state;
    uint32_t state_enter_tick;
    uint32_t double_black_ticks;
    uint32_t backtrack_junction_guard;
    uint8_t junction_latched;
    uint8_t line_seen;
    TraceJunction stack[TRACE_STACK_MAX];
    uint8_t stack_depth;
} TraceController;

static osMutexId_t sensor_mutex;
static SensorSnapshot sensor_snapshot;
static volatile uint8_t sensor_reset_requested;
static uint32_t link_ack_ok;
static uint32_t link_ack_bad;

static uint8_t protocol_checksum(uint8_t cmd, uint8_t len, const uint8_t *payload)
{
    uint8_t i;
    uint8_t checksum = (uint8_t)(cmd + len);

    for (i = 0U; i < len; i++) {
        checksum = (uint8_t)(checksum + payload[i]);
    }
    return checksum;
}

static uint8_t protocol_encode_frame(uint8_t cmd, uint8_t len, const uint8_t *payload,
    uint8_t *frame, uint8_t capacity)
{
    uint8_t i;
    uint8_t frame_len = (uint8_t)(len + 4U);

    if (frame == NULL || len > MAX_PAYLOAD || capacity < frame_len) {
        return 0U;
    }
    if (len != 0U && payload == NULL) {
        return 0U;
    }

    frame[0] = PROTOCOL_SOF;
    frame[1] = cmd;
    frame[2] = len;
    for (i = 0U; i < len; i++) {
        frame[3U + i] = payload[i];
    }
    frame[3U + len] = protocol_checksum(cmd, len, payload);
    return frame_len;
}

static int protocol_send_frame(uint8_t cmd, uint8_t len, const uint8_t *payload)
{
    uint8_t frame[PROTOCOL_FRAME_MAX];
    uint8_t frame_len = protocol_encode_frame(cmd, len, payload, frame, sizeof(frame));
    int written;

    if (frame_len == 0U) {
        printf("[Trace v6.3] frame encode failed: cmd=0x%02X len=%u\r\n", cmd, len);
        return -1;
    }

    written = UartWrite(WIFI_IOT_UART_IDX_2, frame, frame_len);
    if (written != frame_len) {
        printf("[Trace v6.3] UART2 write failed: cmd=0x%02X wrote=%d/%u\r\n",
            cmd, written, frame_len);
        return -1;
    }
    return 0;
}

static int protocol_send_stop(void)
{
    return protocol_send_frame(PROTOCOL_CMD_STOP, 0U, NULL);
}

static int protocol_send_speed(int16_t left, int16_t right)
{
    uint8_t payload[4];
    uint16_t left_raw = (uint16_t)left;
    uint16_t right_raw = (uint16_t)right;

    payload[0] = (uint8_t)(left_raw & 0xFFU);
    payload[1] = (uint8_t)((left_raw >> 8) & 0xFFU);
    payload[2] = (uint8_t)(right_raw & 0xFFU);
    payload[3] = (uint8_t)((right_raw >> 8) & 0xFFU);
    return protocol_send_frame(PROTOCOL_CMD_SET_SPEED, sizeof(payload), payload);
}

static const char *protocol_command_name(uint8_t cmd)
{
    switch (cmd) {
        case PROTOCOL_CMD_SET_SPEED:
            return "SET_SPEED";
        case PROTOCOL_CMD_STOP:
            return "STOP";
        case PROTOCOL_CMD_ACK:
            return "ACK";
        default:
            return "UNKNOWN_CMD";
    }
}

static const char *protocol_status_name(uint8_t status)
{
    switch (status) {
        case PROTOCOL_STATUS_OK:
            return "OK";
        case PROTOCOL_STATUS_BAD_LENGTH:
            return "BAD_LENGTH";
        case PROTOCOL_STATUS_BAD_CHECKSUM:
            return "BAD_CHECKSUM";
        case PROTOCOL_STATUS_INVALID_PARAM:
            return "INVALID_PARAM";
        case PROTOCOL_STATUS_UNKNOWN_CMD:
            return "UNKNOWN_CMD";
        default:
            return "UNKNOWN_STATUS";
    }
}

static void protocol_handle_received_frame(const ProtocolRxParser *parser)
{
    if (parser->cmd != PROTOCOL_CMD_ACK || parser->len != 2U) {
        printf("[Trace v6.3] RX ignored: cmd=0x%02X len=%u\r\n", parser->cmd, parser->len);
        return;
    }

    if (parser->payload[1] != PROTOCOL_STATUS_OK) {
        link_ack_bad++;
        printf("[Trace v6.3] ACK %s (0x%02X): %s (%u)\r\n",
            protocol_command_name(parser->payload[0]), parser->payload[0],
            protocol_status_name(parser->payload[1]), parser->payload[1]);
    } else {
        link_ack_ok++;
    }
}

static void protocol_rx_reset(ProtocolRxParser *parser)
{
    parser->state = RX_WAIT_SOF;
    parser->cmd = 0U;
    parser->len = 0U;
    parser->index = 0U;
    parser->checksum = 0U;
}

static void protocol_rx_start(ProtocolRxParser *parser)
{
    parser->state = RX_CMD;
    parser->len = 0U;
    parser->index = 0U;
    parser->checksum = 0U;
}

static void protocol_parse_byte(ProtocolRxParser *parser, uint8_t byte)
{
    switch (parser->state) {
        case RX_WAIT_SOF:
            if (byte == PROTOCOL_SOF) {
                protocol_rx_start(parser);
            }
            break;

        case RX_CMD:
            parser->cmd = byte;
            parser->checksum = byte;
            parser->state = RX_LEN;
            break;

        case RX_LEN:
            parser->len = byte;
            parser->checksum = (uint8_t)(parser->checksum + byte);
            if (parser->len > MAX_PAYLOAD) {
                printf("[Trace v6.3] RX bad length: %u\r\n", parser->len);
                protocol_rx_reset(parser);
                if (byte == PROTOCOL_SOF) {
                    protocol_rx_start(parser);
                }
            } else if (parser->len == 0U) {
                parser->state = RX_CHECK;
            } else {
                parser->index = 0U;
                parser->state = RX_PAYLOAD;
            }
            break;

        case RX_PAYLOAD:
            parser->payload[parser->index++] = byte;
            parser->checksum = (uint8_t)(parser->checksum + byte);
            if (parser->index >= parser->len) {
                parser->state = RX_CHECK;
            }
            break;

        case RX_CHECK:
            if (byte == parser->checksum) {
                protocol_handle_received_frame(parser);
            } else {
                printf("[Trace v6.3] RX checksum error: cmd=0x%02X\r\n", parser->cmd);
            }
            protocol_rx_reset(parser);
            if (byte == PROTOCOL_SOF) {
                protocol_rx_start(parser);
            }
            break;

        default:
            protocol_rx_reset(parser);
            break;
    }
}

static void trace_ack_thread(void *arg)
{
    uint8_t buffer[ACK_READ_BUFFER_SIZE];
    ProtocolRxParser parser;

    (void)arg;
    protocol_rx_reset(&parser);

    while (1) {
        unsigned char empty = 1U;
        unsigned int result = UartIsBufEmpty(WIFI_IOT_UART_IDX_2, &empty);

        if (result == WIFI_IOT_SUCCESS && empty == 0U) {
            int count = UartRead(WIFI_IOT_UART_IDX_2, buffer, sizeof(buffer));
            int i;

            if (count < 0) {
                printf("[Trace v6.3] UART2 read failed\r\n");
            } else {
                for (i = 0; i < count; i++) {
                    protocol_parse_byte(&parser, buffer[i]);
                }
            }
        } else if (result != WIFI_IOT_SUCCESS) {
            printf("[Trace v6.3] UART2 buffer check failed: %u\r\n", result);
        }
        osDelay(ACK_POLL_DELAY_TICKS);
    }
}

static void sensor_debounce_reset(SensorDebounce *filter)
{
    filter->candidate = 0U;
    filter->stable = 0U;
    filter->count = 0U;
    filter->ready = 0U;
}

static void sensor_debounce_update(SensorDebounce *filter, uint8_t sample)
{
    if (filter->count == 0U || filter->candidate != sample) {
        filter->candidate = sample;
        filter->count = 1U;
    } else if (filter->count < TRACE_DEBOUNCE_SAMPLES) {
        filter->count++;
    }

    if (filter->count >= TRACE_DEBOUNCE_SAMPLES) {
        filter->stable = filter->candidate;
        filter->ready = 1U;
    }
}

static void sensor_request_reset(void)
{
    if (osMutexAcquire(sensor_mutex, osWaitForever) != osOK) {
        return;
    }
    sensor_reset_requested = 1U;
    sensor_snapshot.ready = 0U;
    sensor_snapshot.healthy = 0U;
    osMutexRelease(sensor_mutex);
}

static uint8_t sensor_take_reset_request(void)
{
    uint8_t requested = 0U;

    if (osMutexAcquire(sensor_mutex, osWaitForever) != osOK) {
        return 0U;
    }
    requested = sensor_reset_requested;
    sensor_reset_requested = 0U;
    osMutexRelease(sensor_mutex);
    return requested;
}

static void sensor_publish(uint8_t raw_left, uint8_t raw_right,
    const SensorDebounce *left, const SensorDebounce *right,
    uint8_t healthy, uint32_t now)
{
    if (osMutexAcquire(sensor_mutex, osWaitForever) != osOK) {
        return;
    }

    sensor_snapshot.raw_left = raw_left;
    sensor_snapshot.raw_right = raw_right;
    sensor_snapshot.line_left = left->stable;
    sensor_snapshot.line_right = right->stable;
    sensor_snapshot.ready = (uint8_t)(healthy != 0U &&
        left->ready != 0U && right->ready != 0U);
    sensor_snapshot.healthy = healthy;
    sensor_snapshot.updated_tick = now;
    sensor_snapshot.sequence++;
    osMutexRelease(sensor_mutex);
}

static SensorSnapshot sensor_take_snapshot(void)
{
    SensorSnapshot snapshot = {0};

    if (osMutexAcquire(sensor_mutex, osWaitForever) != osOK) {
        return snapshot;
    }
    snapshot = sensor_snapshot;
    osMutexRelease(sensor_mutex);
    return snapshot;
}

static void trace_sensor_thread(void *arg)
{
    SensorDebounce left = {0};
    SensorDebounce right = {0};
    uint8_t last_line_left = 0U;
    uint8_t last_line_right = 0U;
    uint8_t last_logged_ready = 0U;
    uint8_t was_healthy = 0U;

    (void)arg;

    while (1) {
        WifiIotGpioValue raw_left = WIFI_IOT_GPIO_VALUE1;
        WifiIotGpioValue raw_right = WIFI_IOT_GPIO_VALUE1;
        unsigned int left_result;
        unsigned int right_result;
        uint32_t now = hi_get_tick();

        if (sensor_take_reset_request() != 0U) {
            sensor_debounce_reset(&left);
            sensor_debounce_reset(&right);
            last_logged_ready = 0U;
        }

        left_result = GpioGetInputVal(IR_LEFT_PIN, &raw_left);
        right_result = GpioGetInputVal(IR_RIGHT_PIN, &raw_right);

        if (left_result != WIFI_IOT_SUCCESS || right_result != WIFI_IOT_SUCCESS ||
            (raw_left != WIFI_IOT_GPIO_VALUE0 && raw_left != WIFI_IOT_GPIO_VALUE1) ||
            (raw_right != WIFI_IOT_GPIO_VALUE0 && raw_right != WIFI_IOT_GPIO_VALUE1)) {
            sensor_debounce_reset(&left);
            sensor_debounce_reset(&right);
            sensor_publish((uint8_t)raw_left, (uint8_t)raw_right,
                &left, &right, 0U, now);
            if (was_healthy != 0U) {
                printf("[Trace v6.3] infrared read failed: left=%u right=%u\r\n",
                    left_result, right_result);
            }
            was_healthy = 0U;
            last_logged_ready = 0U;
        } else {
            /* Weak reflection (raw high) = tape / air, both read "white". */
            uint8_t line_left = (raw_left == WIFI_IOT_GPIO_VALUE1) ? 1U : 0U;
            uint8_t line_right = (raw_right == WIFI_IOT_GPIO_VALUE1) ? 1U : 0U;

            sensor_debounce_update(&left, line_left);
            sensor_debounce_update(&right, line_right);
            sensor_publish((uint8_t)raw_left, (uint8_t)raw_right,
                &left, &right, 1U, now);

            if (was_healthy == 0U) {
                printf("[Trace v6.3] infrared read recovered; debouncing again\r\n");
            }
            was_healthy = 1U;

            if (left.ready != 0U && right.ready != 0U &&
                (last_logged_ready == 0U || left.stable != last_line_left ||
                 right.stable != last_line_right)) {
                printf("[Trace v6.3] sensor raw L=%u R=%u, line L=%s R=%s\r\n",
                    (uint8_t)raw_left, (uint8_t)raw_right,
                    left.stable != 0U ? "white" : "black",
                    right.stable != 0U ? "white" : "black");
                ble_debug_printf("[Trace v6.3] sensor L=%s R=%s\r\n",
                    left.stable != 0U ? "white" : "black",
                    right.stable != 0U ? "white" : "black");
                last_line_left = left.stable;
                last_line_right = right.stable;
                last_logged_ready = 1U;
            }
        }
        osDelay(SENSOR_SAMPLE_TICKS);
    }
}

static const char *trace_state_name(TraceState state)
{
    switch (state) {
        case TRACE_FOLLOW:
            return "FOLLOW";
        case TRACE_TURN_LEFT:
            return "TURN_L";
        case TRACE_TURN_RIGHT:
            return "TURN_R";
        case TRACE_BACKTRACK:
            return "BACKTRACK";
        case TRACE_FAILED:
            return "FAILED";
        default:
            return "UNKNOWN";
    }
}

static void trace_enter_state(TraceController *controller, TraceState state,
    const SensorSnapshot *sensor, uint32_t now)
{
    TraceState previous = controller->state;

    controller->state = state;
    controller->state_enter_tick = now;
    printf("[Trace v6.3] %s -> %s (L=%s R=%s depth=%u)\r\n",
        trace_state_name(previous), trace_state_name(state),
        sensor->line_left != 0U ? "white" : "black",
        sensor->line_right != 0U ? "white" : "black",
        controller->stack_depth);
    ble_debug_printf("[Trace v6.3] %s -> %s (L=%s R=%s depth=%u)\r\n",
        trace_state_name(previous), trace_state_name(state),
        sensor->line_left != 0U ? "w" : "b",
        sensor->line_right != 0U ? "w" : "b",
        controller->stack_depth);
}

/* DFS-style state machine for a two-choice junction. A double-white event is
   latched so its multiple 10 ms samples cannot create duplicate stack entries. */
static void trace_logic_tick(TraceController *controller,
    const SensorSnapshot *sensor, uint32_t now)
{
    uint8_t lw = (uint8_t)(sensor->line_left != 0U);
    uint8_t rw = (uint8_t)(sensor->line_right != 0U);
    uint8_t both_white = (uint8_t)(lw != 0U && rw != 0U);
    uint8_t both_black = (uint8_t)(lw == 0U && rw == 0U);
    uint32_t dwell = (uint32_t)(now - controller->state_enter_tick);

    if (both_white == 0U) {
        controller->junction_latched = 0U;
    }

    switch (controller->state) {
        case TRACE_FOLLOW:
            if (both_white != 0U && controller->junction_latched == 0U) {
                if (controller->stack_depth >= TRACE_STACK_MAX) {
                    printf("[Trace v6.3] DFS stack full; stopping\r\n");
                    trace_enter_state(controller, TRACE_FAILED, sensor, now);
                    break;
                }
                controller->junction_latched = 1U;
                controller->stack[controller->stack_depth].tried_left = 1U;
                controller->stack[controller->stack_depth].tried_right = 0U;
                controller->stack_depth++;
                controller->line_seen = 0U;
                controller->double_black_ticks = 0U;
                printf("[Trace v6.3] junction depth=%u: try left\r\n",
                    controller->stack_depth);
                trace_enter_state(controller, TRACE_TURN_LEFT, sensor, now);
            } else if (both_black != 0U && controller->line_seen != 0U) {
                controller->double_black_ticks++;
                if (controller->double_black_ticks >= TRACE_DEAD_END_TICKS) {
                    printf("[Trace v6.3] dead end; backtracking\r\n");
                    controller->backtrack_junction_guard = 0U;
                    trace_enter_state(controller, TRACE_BACKTRACK, sensor, now);
                }
            } else {
                controller->double_black_ticks = 0U;
                if (both_black == 0U) {
                    controller->line_seen = 1U;
                }
            }
            break;

        case TRACE_TURN_LEFT:
            if (rw == 0U || dwell >= TURN_TIMEOUT_TICKS) {
                controller->line_seen = 0U;
                controller->double_black_ticks = 0U;
                trace_enter_state(controller, TRACE_FOLLOW, sensor, now);
            }
            break;

        case TRACE_TURN_RIGHT:
            if (lw == 0U || dwell >= TURN_TIMEOUT_TICKS) {
                controller->line_seen = 0U;
                controller->double_black_ticks = 0U;
                trace_enter_state(controller, TRACE_FOLLOW, sensor, now);
            }
            break;

        case TRACE_BACKTRACK:
            if (dwell >= BACKTRACK_TIMEOUT_TICKS) {
                printf("[Trace v6.3] BACKTRACK timeout; stopping\r\n");
                trace_enter_state(controller, TRACE_FAILED, sensor, now);
            } else if (both_white != 0U && controller->backtrack_junction_guard == 0U) {
                if (controller->stack_depth == 0U) {
                    trace_enter_state(controller, TRACE_FAILED, sensor, now);
                } else {
                    TraceJunction *junction = &controller->stack[controller->stack_depth - 1U];
                    controller->backtrack_junction_guard = TRACE_BACKTRACK_JUNCTION_GUARD;
                    if (junction->tried_right == 0U) {
                        junction->tried_right = 1U;
                        controller->line_seen = 0U;
                        controller->double_black_ticks = 0U;
                        printf("[Trace v6.3] junction depth=%u: try right\r\n",
                            controller->stack_depth);
                        trace_enter_state(controller, TRACE_TURN_RIGHT, sensor, now);
                    } else {
                        controller->stack_depth--;
                        if (controller->stack_depth == 0U) {
                            trace_enter_state(controller, TRACE_FAILED, sensor, now);
                        }
                    }
                }
            } else if (controller->backtrack_junction_guard != 0U && both_white == 0U) {
                controller->backtrack_junction_guard--;
            }
            break;

        case TRACE_FAILED:
        default:
            break;
    }
}

static MotionCommand trace_motion_command(const TraceController *controller,
    const SensorSnapshot *sensor)
{
    MotionCommand command = {0, 0, 1U};

    switch (controller->state) {
        case TRACE_TURN_LEFT:
            command.left = -TURN_SPEED;
            command.right = TURN_SPEED;
            command.stop = 0U;
            break;

        case TRACE_TURN_RIGHT:
            command.left = TURN_SPEED;
            command.right = -TURN_SPEED;
            command.stop = 0U;
            break;

        case TRACE_BACKTRACK:
            command.left = -BACKTRACK_SPEED;
            command.right = -BACKTRACK_SPEED;
            command.stop = 0U;
            break;

        case TRACE_FAILED:
            break;

        case TRACE_FOLLOW:
        default:
        {
            uint8_t lw = (uint8_t)(sensor->line_left != 0U);
            uint8_t rw = (uint8_t)(sensor->line_right != 0U);

            if (lw != 0U && rw != 0U) {
                /* double white: continue across a junction after the turn. */
                command.left = DRIVE_SPEED;
                command.right = DRIVE_SPEED;
            } else if (lw != 0U) {
                /* tape under left probe: steer left. */
                command.left = CORRECT_INNER_SPEED;
                command.right = CORRECT_OUTER_SPEED;
            } else if (rw != 0U) {
                /* tape under right probe: steer right. */
                command.left = CORRECT_OUTER_SPEED;
                command.right = CORRECT_INNER_SPEED;
            } else {
                /* both probes on ground: straight. */
                command.left = DRIVE_SPEED;
                command.right = DRIVE_SPEED;
            }
            command.stop = 0U;
            break;
        }
    }
    return command;
}

static void trace_send_command(const MotionCommand *command, uint32_t now)
{
    static MotionCommand previous = {0, 0, 1U};
    static uint32_t last_send_tick = 0U;
    static uint8_t sent_once = 0U;
    MotionCommand actual = *command;
    uint8_t changed;
    int result;

#if !TRACE_ENABLE_MOTION
    actual.left = 0;
    actual.right = 0;
    actual.stop = 1U;
#endif

    changed = (uint8_t)(sent_once == 0U || actual.stop != previous.stop ||
        actual.left != previous.left || actual.right != previous.right);
    if (changed == 0U && (uint32_t)(now - last_send_tick) < COMMAND_RESEND_TICKS) {
        return;
    }

    if (actual.stop != 0U) {
        result = protocol_send_stop();
    } else {
        result = protocol_send_speed(actual.left, actual.right);
    }

    if (result == 0) {
        previous = actual;
        last_send_tick = now;
        sent_once = 1U;
    }
}

static uint8_t trace_sensor_is_fresh(const SensorSnapshot *sensor, uint32_t now)
{
    if (sensor->sequence == 0U || sensor->healthy == 0U || sensor->ready == 0U) {
        return 0U;
    }
    return (uint8_t)((uint32_t)(now - sensor->updated_tick) <= SENSOR_STALE_TICKS);
}

static void trace_control_thread(void *arg)
{
    TraceController controller = {0};
    uint8_t stale_logged = 0U;
    uint32_t last_log_tick = 0U;
    uint32_t last_ble_tick = 0U;
    uint32_t guard_start;

    (void)arg;
    controller.state = TRACE_FOLLOW;
    controller.state_enter_tick = hi_get_tick();
    controller.double_black_ticks = 0U;
    controller.line_seen = 0U;

    printf("[Trace v6.3] control started: motion=%d drive=%d correct=%d/%d turn=%d/%u dead_end=%u\r\n",
        TRACE_ENABLE_MOTION, DRIVE_SPEED, CORRECT_INNER_SPEED, CORRECT_OUTER_SPEED,
        TURN_SPEED, TURN_TIMEOUT_TICKS, TRACE_DEAD_END_TICKS);
    ble_debug_printf("[Trace v6.3] control started: motion=%d drive=%d turn=%d\r\n",
        TRACE_ENABLE_MOTION, DRIVE_SPEED, TURN_SPEED);
    protocol_send_stop();

    guard_start = hi_get_tick();
    while ((uint32_t)(hi_get_tick() - guard_start) < STARTUP_GUARD_TICKS) {
        trace_send_command(&(MotionCommand){0, 0, 1U}, hi_get_tick());
        osDelay(CONTROL_LOOP_TICKS);
    }

#if !TRACE_ENABLE_MOTION
    printf("[Trace v6.3] motion disabled; all commands are forced STOP\r\n");
    ble_debug_printf("[Trace v6.3] motion disabled (safe build)\r\n");
#endif

    while (1) {
        SensorSnapshot sensor = sensor_take_snapshot();
        uint32_t now = hi_get_tick();
        MotionCommand command = {0, 0, 1U};

        if (trace_sensor_is_fresh(&sensor, now) == 0U) {
            if (stale_logged == 0U) {
                printf("[Trace v6.3] sensor data unavailable or stale; motors stopped\r\n");
                ble_debug_printf("[Trace v6.3] sensor stale; STOP\r\n");
                stale_logged = 1U;
            }
            controller.state = TRACE_FOLLOW;
            controller.state_enter_tick = now;
            controller.double_black_ticks = 0U;
            command = trace_motion_command(&controller, &sensor);
        } else {
            stale_logged = 0U;
            trace_logic_tick(&controller, &sensor, now);
            command = trace_motion_command(&controller, &sensor);
        }

        trace_send_command(&command, now);
        if ((uint32_t)(now - last_log_tick) >= STATUS_LOG_TICKS) {
            printf("[Trace v6.3] st=%s L=%s R=%s cmd=%d/%d depth=%u ack=%lu/%lu\r\n",
                trace_state_name(controller.state),
                sensor.line_left != 0U ? "white" : "black",
                sensor.line_right != 0U ? "white" : "black",
                command.left, command.right,
                controller.stack_depth,
                (unsigned long)link_ack_ok, (unsigned long)link_ack_bad);
            last_log_tick = now;
        }
        if ((uint32_t)(now - last_ble_tick) >= BLE_HEARTBEAT_TICKS) {
            ble_debug_printf("[Trace v6.3] st=%s L=%s R=%s depth=%u\r\n",
                trace_state_name(controller.state),
                sensor.line_left != 0U ? "w" : "b",
                sensor.line_right != 0U ? "w" : "b",
                controller.stack_depth);
            last_ble_tick = now;
        }
        osDelay(CONTROL_LOOP_TICKS);
    }
}

static int trace_hardware_init(void)
{
    WifiIotUartAttribute uart_attr = {
        .baudRate = 115200,
        .dataBits = 8,
        .stopBits = 1,
        .parity = 0,
    };

    if (GpioInit() != WIFI_IOT_SUCCESS) {
        printf("[Trace] GPIO init failed\r\n");
        return -1;
    }
    if (IoSetFunc(WIFI_IOT_IO_NAME_GPIO_11,
        WIFI_IOT_IO_FUNC_GPIO_11_UART2_TXD) != WIFI_IOT_SUCCESS) {
        printf("[Trace] GPIO11 UART2 TX setup failed\r\n");
        return -1;
    }
    if (IoSetFunc(WIFI_IOT_IO_NAME_GPIO_12,
        WIFI_IOT_IO_FUNC_GPIO_12_UART2_RXD) != WIFI_IOT_SUCCESS) {
        printf("[Trace] GPIO12 UART2 RX setup failed\r\n");
        return -1;
    }
    if (IoSetFunc(IR_LEFT_PIN, WIFI_IOT_IO_FUNC_GPIO_13_GPIO) != WIFI_IOT_SUCCESS ||
        IoSetFunc(IR_RIGHT_PIN, WIFI_IOT_IO_FUNC_GPIO_14_GPIO) != WIFI_IOT_SUCCESS) {
        printf("[Trace] infrared GPIO function setup failed\r\n");
        return -1;
    }
    if (GpioSetDir(IR_LEFT_PIN, WIFI_IOT_GPIO_DIR_IN) != WIFI_IOT_SUCCESS ||
        GpioSetDir(IR_RIGHT_PIN, WIFI_IOT_GPIO_DIR_IN) != WIFI_IOT_SUCCESS) {
        printf("[Trace] infrared GPIO direction setup failed\r\n");
        return -1;
    }
    if (UartInit(WIFI_IOT_UART_IDX_2, &uart_attr, NULL) != WIFI_IOT_SUCCESS) {
        printf("[Trace] UART2 init failed\r\n");
        return -1;
    }
#if TRACE_BLE_DEBUG
    /* JDY-16 debug mirror on UART1 (GPIO0/1, 9600). peripheral_init() opened
       UART1 @115200 at boot; drop that stale config once. Debug-only channel:
       failure here must not kill the trace app. */
    {
        WifiIotUartAttribute ble_attr = {
            .baudRate = BLE_DEBUG_BAUD,
            .dataBits = 8,
            .stopBits = 1,
            .parity = 0,
        };

        (void)UartDeinit(WIFI_IOT_UART_IDX_1);
        if (IoSetFunc(WIFI_IOT_IO_NAME_GPIO_0,
            WIFI_IOT_IO_FUNC_GPIO_0_UART1_TXD) != WIFI_IOT_SUCCESS ||
            IoSetFunc(WIFI_IOT_IO_NAME_GPIO_1,
            WIFI_IOT_IO_FUNC_GPIO_1_UART1_RXD) != WIFI_IOT_SUCCESS ||
            UartInit(WIFI_IOT_UART_IDX_1, &ble_attr, NULL) != WIFI_IOT_SUCCESS) {
            printf("[Trace] UART1 BLE debug init failed; continuing without it\r\n");
        }
    }
#endif
    return 0;
}

static int trace_create_thread(const char *name, osThreadFunc_t function)
{
    osThreadAttr_t attr = {0};

    attr.name = name;
    attr.stack_size = TRACE_THREAD_STACK_SIZE;
    attr.priority = TRACE_THREAD_PRIORITY;
    if (osThreadNew(function, NULL, &attr) == NULL) {
        printf("[Trace] thread create failed: %s\r\n", name);
        return -1;
    }
    return 0;
}

static void TraceFollowingEntry(void)
{
    if (trace_hardware_init() != 0) {
        return;
    }

    sensor_mutex = osMutexNew(NULL);
    if (sensor_mutex == NULL) {
        printf("[Trace] sensor mutex create failed\r\n");
        return;
    }

    if (trace_create_thread("TraceAck", (osThreadFunc_t)trace_ack_thread) != 0) {
        return;
    }
    if (trace_create_thread("TraceSensor", (osThreadFunc_t)trace_sensor_thread) != 0) {
        return;
    }
    if (trace_create_thread("TraceControl", (osThreadFunc_t)trace_control_thread) != 0) {
        return;
    }

    printf("[Trace v6.3] ready: IR GPIO13/14, UART2 GPIO11/12 115200, BLE debug UART1 GPIO0/1 9600\r\n");
    ble_debug_printf("[Trace v6.3] ready: BLE debug link up\r\n");
}

APP_FEATURE_INIT(TraceFollowingEntry);
