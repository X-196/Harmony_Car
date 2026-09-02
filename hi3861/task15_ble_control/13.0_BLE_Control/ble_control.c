/*
 * 手机蓝牙遥控小车：UART1 蓝牙 + GPIO11 软件 UART 到 STM32（9600 间歇分时版）
 *
 * 本车 Hi3861 旧版 UART HAL 在 UART1/UART2 同时启用时会使 UART2 失效，
 * 因此不调用 UartInit(UART2)：UART1 硬件串口接蓝牙模块，GPIO11 位倒
 * 软件 UART 只发不收，送帧给 STM32。
 *
 * 间歇式单任务：同一个任务轮流干两件事——平时轮询蓝牙命令；收到命令
 * 或到 100ms 心跳点才发一帧，发完立刻继续听蓝牙。发送与接收不并发，
 * 位倒时序不会被接收侧打断。波特率 9600（位宽 104us）：位倒误差和
 * RTOS 中断抖动对采样都可以忽略；代价是 STM32 端要同步改 9600。
 *
 * 时序要点（踩坑）：本内核 LOSCFG_BASE_CORE_TICK_PER_SECOND=100，
 * 1 tick = 10ms，osDelay(1)=10ms——不能拿 osDelay 计数当毫秒用
 * （曾 osDelay(10) 当 10ms，实际循环一圈 100ms，心跳被放大到 1s+，
 * 超过 STM32 500ms 失效保护，车"走一步就停"）。心跳/收尾/倒计时
 * 一律用 hi_get_us() 真实时间。另：UartRead 传 NULL extraAttr 时
 * rx_block=0，是非阻塞读（空缓冲返回 0）。
 *
 * 命令格式：小写方向字母 + 可选数字（数字单位 0.1s），例如：
 *   w50 = 前进 5s 后自动停车；s30 = 后退 3s；a15/d15 = 左/右转 1.5s
 *   w/s/a/d 不带数字 = 持续运行到 o 停车（兼容旧玩法）
 *   o = 立即停车；i/k = 速度档 100/150（不带数字，下一次运动生效）
 *   兼容旧大写命令与备用映射：b=后退 c=右转 e=停止 f=左转
 *   数字最长跑到 600（60s）封顶。数字结束的判定：遇到非数字字符，
 *   或 100ms 内没有新字节（适配 App 整串发送，无需回车结尾）。
 *   JDY-16 的 "+CONNECTED"/"+DISCONNECTED" 状态行整行吞掉：
 *   断连自动停车，状态串不再被误解析成按键命令。
 *
 * STM32 端继续使用任务24的 V2 10字节协议（本改动不涉及 STM32）：
 * FC | 02 | 0A | 左轮int16 LE | 右轮int16 LE | seq | XOR | FD
 */
#include <stdio.h>
#include <unistd.h>
#include "wifiiot_uart.h"
#include "ohos_init.h"
#include "cmsis_os2.h"
#include "wifiiot_gpio.h"
#include "wifiiot_gpio_ex.h"
#include "hi_io.h"
#include "hi_time.h"

#define STM32_TX_GPIO 11
#define SOFTUART_BIT_US 104             /* 9600 baud: 104.2us/bit */
#define FRAME_PERIOD_US (100UL * 1000UL) /* 心跳帧周期 100ms，小于 STM32 500ms 超时 */
#define DIGIT_IDLE_US   (100UL * 1000UL) /* 数字收尾超时：100ms 无新字节视为命令完整 */
#define MAX_RUN_10THS 600               /* 定时命令最长 60s */
#define STATUS_LINE_MAX 32              /* "+CONNECTED\r\n" 最长 16 字节，留余量 */

static int speed_level = 100;
static volatile int target_left = 0;
static volatile int target_right = 0;
static volatile uint8_t tx_seq = 0;
static hi_u64 run_deadline_us = 0;      /* 定时命令的停车时刻；0=持续到 o */

/* 软件 UART TX：空闲高，起始位低，8位数据LSB first，停止位高。 */
static void softuart_tx_byte(uint8_t value)
{
    unsigned int i;

    GpioSetOutputVal(STM32_TX_GPIO, WIFI_IOT_GPIO_VALUE0);
    hi_udelay(SOFTUART_BIT_US);
    for (i = 0; i < 8; i++) {
        GpioSetOutputVal(STM32_TX_GPIO,
            (value & (1U << i)) ? WIFI_IOT_GPIO_VALUE1 : WIFI_IOT_GPIO_VALUE0);
        hi_udelay(SOFTUART_BIT_US);
    }
    GpioSetOutputVal(STM32_TX_GPIO, WIFI_IOT_GPIO_VALUE1);
    hi_udelay(SOFTUART_BIT_US);
}

static void stm32_send_frame(int left, int right)
{
    uint8_t frame[10];
    uint8_t checksum;
    unsigned int i;

    if (left > 150) left = 150;
    if (left < -150) left = -150;
    if (right > 150) right = 150;
    if (right < -150) right = -150;

    frame[0] = 0xFC;
    frame[1] = 0x02;
    frame[2] = 0x0A;
    frame[3] = (uint8_t)(left & 0xFF);
    frame[4] = (uint8_t)((left >> 8) & 0xFF);
    frame[5] = (uint8_t)(right & 0xFF);
    frame[6] = (uint8_t)((right >> 8) & 0xFF);
    frame[7] = tx_seq++;
    checksum = frame[1] ^ frame[2] ^ frame[3] ^ frame[4] ^
               frame[5] ^ frame[6] ^ frame[7];
    frame[8] = checksum;
    frame[9] = 0xFD;

    /* 一帧约 10ms；期间到达的蓝牙字节由 UART1 硬件 FIFO 暂存，发完再读。 */
    for (i = 0; i < 10; i++) {
        softuart_tx_byte(frame[i]);
    }
}

static void set_target(int left, int right)
{
    target_left = left;
    target_right = right;
}

static void command_forward(void)  { set_target(speed_level, speed_level); }
static void command_backward(void) { set_target(-speed_level, -speed_level); }
static void command_left(void)     { set_target(0, speed_level + 15); }
static void command_right(void)    { set_target(speed_level + 15, 0); }
static void command_stop(void)     { set_target(0, 0); }

/* ---------- 命令解析：方向字母 + 可选数字（0.1s） ---------- */
#define CMD_STATE_NORMAL 0
#define CMD_STATE_PLUS   1      /* 收到 '+'，等状态行第 2 个字符 */
#define CMD_STATE_SKIP   2      /* 吞 JDY-16 状态行剩余字节 */

static int  cmd_state = CMD_STATE_NORMAL;
static int  swallow_cnt = 0;
static char cmd_letter = 0;     /* 非 0 = 已收到方向字母，正在收数字 */
static int  cmd_digits = 0;
static unsigned int cmd_value = 0;
static hi_u64 cmd_last_byte_us = 0;

/* 执行挂起的"字母+数字"命令。返回 1 表示目标已变，需要立即发帧。 */
static int execute_pending(void)
{
    int run = -1;               /* 无数字 = -1：持续到 o */
    char letter = cmd_letter;

    if (cmd_digits > 0) {
        run = (int)cmd_value;   /* 累加时已封顶 MAX_RUN_10THS */
    }
    cmd_letter = 0;
    cmd_digits = 0;
    cmd_value = 0;

    switch (letter) {
        case 'w':           command_forward();  break;
        case 's': case 'b': command_backward(); break;
        case 'a': case 'f': command_left();     break;
        case 'd': case 'c': command_right();    break;
        default:            command_stop();     break;
    }
    if (run == 0) {             /* 例如 w0：立刻停 */
        command_stop();
        run = -1;
        run_deadline_us = 0;
        printf("CMD %c: stop (time 0)\r\n", letter);
    } else if (run > 0) {
        run_deadline_us = hi_get_us() + (hi_u64)run * 100000U;  /* run x 0.1s 后停车 */
        printf("CMD %c: %d x0.1s (L=%d R=%d)\r\n", letter, run, target_left, target_right);
    } else {
        run_deadline_us = 0;
        printf("CMD %c: hold (L=%d R=%d)\r\n", letter, target_left, target_right);
    }
    return 1;
}

/* 处理一个蓝牙字节。返回 1 表示目标轮速已改变，需要立即补发一帧。 */
static int handle_ble_byte(uint8_t byte)
{
    char c = (char)byte;
    int need = 0;

    /* 正在收数字：数字则累加；非数字先把挂起命令收尾执行，字符继续走正常解析 */
    if (cmd_letter != 0) {
        if (c >= '0' && c <= '9') {
            cmd_value = cmd_value * 10 + (unsigned int)(c - '0');
            if (cmd_value > MAX_RUN_10THS) {
                cmd_value = MAX_RUN_10THS;
            }
            cmd_digits++;
            cmd_last_byte_us = hi_get_us();
            return 0;
        }
        need = execute_pending();
    }

    if (cmd_state == CMD_STATE_SKIP) {      /* 吞状态行剩余字节 */
        if (c == '\n' || ++swallow_cnt >= STATUS_LINE_MAX) {
            cmd_state = CMD_STATE_NORMAL;
        }
        return need;
    }
    if (cmd_state == CMD_STATE_PLUS) {      /* 状态行第 2 字符区分 CONN/DISCONN */
        cmd_state = CMD_STATE_SKIP;
        swallow_cnt = 0;
        if (c == 'D') {                     /* "+DISCONNECTED"：断连自动停车 */
            command_stop();
            run_deadline_us = 0;
            printf("BLE DISCONNECTED: auto stop\r\n");
            return 1;
        }
        if (c == 'C') {                     /* "+CONNECTED" */
            printf("BLE CONNECTED\r\n");
        }
        return need;
    }
    if (c == '+') {
        cmd_state = CMD_STATE_PLUS;
        return need;
    }

    if (c >= 'A' && c <= 'Z') {             /* 大写兼容，统一按小写处理 */
        c = (char)(c - 'A' + 'a');
    }
    switch (c) {
        case 'w': case 's': case 'a': case 'd':
        case 'b': case 'c': case 'f':       /* 方向命令：挂起，等可能的数字 */
            cmd_letter = c;
            cmd_digits = 0;
            cmd_value = 0;
            cmd_last_byte_us = hi_get_us();
            break;
        case 'o': case 'e':
            command_stop();
            run_deadline_us = 0;
            printf("CMD o: stop\r\n");
            return 1;
        case 'i': speed_level = 100; printf("CMD i: speed 100\r\n"); break;
        case 'k': speed_level = 150; printf("CMD k: speed 150\r\n"); break;
        case '\r': case '\n': case ' ': break;
        default: printf("CMD? [%02x]\r\n", (unsigned char)byte); break;
    }
    return need;
}

/* 间歇式单任务：轮询蓝牙与发送电机帧分时进行，互不并发。
 * 所有周期判定用 hi_get_us() 真实时间（tick=100Hz，osDelay(1)=10ms）。 */
static void ble_ctrl_task(void)
{
    uint8_t byte;
    int need_frame = 0;
    hi_u64 last_frame_us = 0;

    printf("BLE control ready v3(hi_get_us): w/s/a/d+time(0.1s), o stop, i/k speed\r\n");
    for (;;) {
        if (UartRead(WIFI_IOT_UART_IDX_1, &byte, 1) == 1) {
            if (handle_ble_byte(byte)) {
                need_frame = 1;
            }
        }
        /* 数字收尾超时：100ms 没有新字节，执行挂起的方向命令 */
        if (cmd_letter != 0 && hi_get_us() - cmd_last_byte_us >= DIGIT_IDLE_US) {
            if (execute_pending()) {
                need_frame = 1;
            }
        }
        /* 定时命令到点自动停车 */
        if (run_deadline_us != 0 && hi_get_us() >= run_deadline_us) {
            command_stop();
            run_deadline_us = 0;
            need_frame = 1;
            printf("AUTO STOP\r\n");
        }
        if (need_frame || hi_get_us() - last_frame_us >= FRAME_PERIOD_US) {
            stm32_send_frame(target_left, target_right);
            last_frame_us = hi_get_us();
            if (!need_frame) {
                printf(".");            /* 心跳探针：每 100ms 一个点，证明在持续发帧 */
            }
            need_frame = 0;
        }
        osDelay(1);     /* 1 tick = 10ms：蓝牙轮询节拍 */
    }
}

static void ble_control(void)
{
    osThreadAttr_t attr;

    GpioInit();

    /* 只把 GPIO11 配成普通 GPIO，完全绕开 UART2（与任务08实测同写法）。 */
    hi_io_set_func(STM32_TX_GPIO, 0);
    GpioSetDir(STM32_TX_GPIO, WIFI_IOT_GPIO_DIR_OUT);
    GpioSetOutputVal(STM32_TX_GPIO, WIFI_IOT_GPIO_VALUE1);

    /* 蓝牙唯一使用硬件 UART1。 */
    IoSetFunc(WIFI_IOT_IO_NAME_GPIO_0, WIFI_IOT_IO_FUNC_GPIO_0_UART1_TXD);
    IoSetFunc(WIFI_IOT_IO_NAME_GPIO_1, WIFI_IOT_IO_FUNC_GPIO_1_UART1_RXD);
    WifiIotUartAttribute uart_attr1 = {
        .baudRate = 9600,
        .dataBits = 8,
        .stopBits = 1,
        .parity = 0,
    };
    UartInit(WIFI_IOT_UART_IDX_1, &uart_attr1, NULL);

    attr.attr_bits = 0U;
    attr.cb_mem = NULL;
    attr.cb_size = 0U;
    attr.stack_mem = NULL;
    attr.stack_size = 1024 * 4;
    attr.priority = 25;
    attr.name = "ble_ctrl";
    if (osThreadNew((osThreadFunc_t)ble_ctrl_task, NULL, &attr) == NULL) {
        printf("Failed to create ble_ctrl!\r\n");
    }
}
APP_FEATURE_INIT(ble_control);
