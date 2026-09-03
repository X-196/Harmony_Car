/*
 * 迷宫巡逻（14.0_Maze_Patrol）：避障 + 防跌落（黑胶带禁区）二合一
 *
 * 演示场景：纸箱围成的迷宫里自主巡逻 2 分钟——不能撞箱子（超声波避障），
 * 不能碰到/进入黑色胶带围成的禁区（红外检测，与桌沿检测同构）。
 *
 * 传感器（与任务24/任务6/8/10 完全相同的接法）：
 *   - HC-SR04 超声波（GPIO_7=TRIG / GPIO_8=ECHO），装在 SG90 舵机云台上
 *   - SG90 舵机 GPIO_2：带超声波左右扫 45° 选路，平时回中
 *   - TCRT5000 红外对管朝下：左 GPIO_13 / 右 GPIO_14
 *     桌沿（下方无反射）与黑胶带（吸红外、反射骤变）都表现为
 *     "读数 != 开机标定的地面电平"——一套检测通吃防跌落和禁区
 *
 * 传输层：GPIO11 软件位倒 UART @9600（task15 实机验证版）。
 * 本车 Hi3861 旧版 UART HAL 在 UART1/UART2 同开时 UART2 失效，且实车
 * STM32 已是 9600 版任务24 固件，软件 UART 免重烧。发一帧约 10.4ms。
 * 注意：STM32 有 500ms 无有效帧自动停车保护——任何一次测距（30ms 超时）
 * 都在保护期内，但舵机扫描（数百 ms）期间必须持续供帧。
 *
 * 计时铁律（task15 踩坑）：本内核 tick=100Hz，osDelay(1)=10ms，不能用
 * 循环计数当毫秒——所有时长一律 hi_get_us() 实时时间。
 *
 * 决策层（task14 实机标定参数）：
 *   - 前方 <20cm：停 → 舵机左/右扫 45° 各测 3 次取中值 → 向空侧转 90°
 *   - 两侧都近：掉头 180°
 *   - 红外任一侧异常（桌沿/胶带）：立即停 → 倒车 300ms 脱离 → 向另一侧转 90°
 *   - 转向/直行全部走"带危险检测的受控段"：每 40ms 查红外，动作中途踩到
 *     胶带/桌沿立即停倒车改道，绝不盲跑（旧版固定延时直行 2s 在箱阵里必撞）
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

/* ==================== 任务开关：演示参数集中在这里 ==================== */
#define DEMO_RUN_SECS   120     /* 演示时长 2 分钟，到点停车 */
#define CRUISE          60      /* 巡航速度 0.6圈/s（≈8.5cm/s，留足检测余量） */
#define OBSTACLE_CM     20.0f   /* 前方障碍阈值 */
#define TURN_90_MS      1400    /* 单轮支点转90°（task14 实测：700ms≈45°） */
#define BACK_MS         300     /* 红外触发后倒车脱离时长 */
#define SERVO_US        300     /* 舵机持压时长 ms（10 帧 ×20ms） */

/* ==================================================================== */

/* ---------- GPIO11 软件 UART TX @9600（task15 实机验证版） ---------- */
#define STM32_TX_GPIO   11
#define SOFTUART_BIT_US 104     /* 9600 baud: 104.2us/bit，RTOS 抖动可忽略 */

static volatile uint8_t tx_seq = 0;

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

/* V2 10字节帧：FC|02|0A|左int16|右int16|seq|XOR|FD（一帧约10.4ms） */
static void stm32_send_frame(int left, int right)
{
    uint8_t frame[10];
    uint8_t checksum;
    unsigned int i;

    if (left > 150) left = 150;
    if (left < -150) left = -150;
    if (right > 150) right = -150;
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

    for (i = 0; i < 10; i++) {
        softuart_tx_byte(frame[i]);
    }
}

/* ---------- 传感器引脚（task6/8/10/14 同款接法） ---------- */
#define GPIO_IR_L   13          /* TCRT5000 左，朝下 */
#define GPIO_IR_R   14          /* TCRT5000 右，朝下 */
#define GPIO_TRIG   7           /* HC-SR04 TRIG */
#define GPIO_ECHO   8           /* HC-SR04 ECHO */
#define GPIO_SG90   2           /* SG90 舵机 */

static void sensor_init(void)
{
    IoSetFunc(WIFI_IOT_IO_NAME_GPIO_2, WIFI_IOT_IO_FUNC_GPIO_2_GPIO);
    GpioSetDir(GPIO_SG90, WIFI_IOT_GPIO_DIR_OUT);
    GpioSetOutputVal(GPIO_SG90, WIFI_IOT_GPIO_VALUE0);

    GpioSetDir(GPIO_TRIG, WIFI_IOT_GPIO_DIR_OUT);
    GpioSetOutputVal(GPIO_TRIG, WIFI_IOT_GPIO_VALUE0);
    GpioSetDir(GPIO_ECHO, WIFI_IOT_GPIO_DIR_IN);

    IoSetFunc(WIFI_IOT_IO_NAME_GPIO_13, WIFI_IOT_IO_FUNC_GPIO_13_GPIO);
    GpioSetDir(GPIO_IR_L, WIFI_IOT_GPIO_DIR_IN);
    IoSetFunc(WIFI_IOT_IO_NAME_GPIO_14, WIFI_IOT_IO_FUNC_GPIO_14_GPIO);
    GpioSetDir(GPIO_IR_R, WIFI_IOT_GPIO_DIR_IN);
}

static WifiIotGpioValue read_ir(unsigned int gpio)
{
    WifiIotGpioValue v = WIFI_IOT_GPIO_VALUE0;
    GpioGetInputVal(gpio, &v);
    return v;
}

/* 超声波测距（30ms 超时保护），返回 cm；超时/无回波返回 999。
 * 官方 while(1) 无超时——回波丢失永久挂死任务，车上必须加超时。 */
static float get_distance_cm(void)
{
    unsigned long long t0 = 0, now, timeout;
    WifiIotGpioValue value = WIFI_IOT_GPIO_VALUE0;
    unsigned int flag = 0;

    GpioSetOutputVal(GPIO_TRIG, WIFI_IOT_GPIO_VALUE1);
    hi_udelay(20);
    GpioSetOutputVal(GPIO_TRIG, WIFI_IOT_GPIO_VALUE0);

    timeout = hi_get_us() + 30000;
    while (1) {
        GpioGetInputVal(GPIO_ECHO, &value);
        now = hi_get_us();
        if (now > timeout) {
            return 999.0f;      /* 无回波按无障碍处理 */
        }
        if (value == WIFI_IOT_GPIO_VALUE1 && flag == 0) {
            t0 = now;
            flag = 1;
        }
        if (value == WIFI_IOT_GPIO_VALUE0 && flag == 1) {
            return (float)(now - t0) * 0.034f / 2.0f;
        }
    }
}

/* ---------- SG90：x 微秒高电平 + (20000-x) 微秒低电平 为一帧 ---------- */
static void set_angle_once(unsigned int duty_us)
{
    GpioSetOutputVal(GPIO_SG90, WIFI_IOT_GPIO_VALUE1);
    hi_udelay(duty_us);
    GpioSetOutputVal(GPIO_SG90, WIFI_IOT_GPIO_VALUE0);
    hi_udelay(20000 - duty_us);
}

/* 舵机持压移动（任务7/10 同款：10 帧 200ms 让 SG90 稳定到位）。
 * 期间持续给 STM32 供帧（500ms 失效保护）。 */
static void servo_move(int duty_us)
{
    hi_u64 until = hi_get_us() + (hi_u64)SERVO_US * 1000ULL;
    while (hi_get_us() < until) {
        set_angle_once(duty_us);
        stm32_send_frame(0, 0);
    }
}

/* 舵机角度标定（task14 实机）：1650=回中 0°，1100=右 45°，2200=左 45° */
#define SERVO_CENTER_US 1650
#define SERVO_LEFT_US   2200
#define SERVO_RIGHT_US  1100

static void servo_center(void) { servo_move(SERVO_CENTER_US); }

/* ---------- 红外危险检测：桌沿 / 黑胶带禁区 同构 ---------- */
static WifiIotGpioValue ground_level_left  = WIFI_IOT_GPIO_VALUE0;
static WifiIotGpioValue ground_level_right = WIFI_IOT_GPIO_VALUE0;

/* 开机标定：在普通地面（迷宫通道内）静止 2s，每 50ms 采样一次，
 * 每侧多数表决出"地面电平"。桌面深浅、场地材质不影响判断。
 * 返回 0=正常，-1=标定期间检测到胶带/异色地面（提示重新摆位）。 */
static int calibrate_ground(void)
{
    int left1 = 0, right1 = 0, i, bad = 0;
    for (i = 0; i < 40; i++) {
        if (read_ir(GPIO_IR_L) == WIFI_IOT_GPIO_VALUE1) left1++;
        if (read_ir(GPIO_IR_R) == WIFI_IOT_GPIO_VALUE1) right1++;
        usleep(50000);
    }
    ground_level_left  = (left1  >= 20) ? WIFI_IOT_GPIO_VALUE1 : WIFI_IOT_GPIO_VALUE0;
    ground_level_right = (right1 >= 20) ? WIFI_IOT_GPIO_VALUE1 : WIFI_IOT_GPIO_VALUE0;
    /* 标定期间两侧读数不稳定（各出现 1/0 交替）→ 车可能摆在胶带上 */
    if (left1 > 8 && left1 < 32) bad = 1;
    if (right1 > 8 && right1 < 32) bad = 1;
    printf("Ground: L=%d R=%d %s\r\n", (int)ground_level_left,
           (int)ground_level_right, bad ? "(UNSTABLE? check position!)" : "OK");
    return bad;
}

/* 危险检测（带二次确认）：读数 != 地面电平 即视为桌沿/黑胶带。
 * out_left 非 NULL 时回填是哪侧（1=左侧异常）。 */
static int danger_detected(int *out_left)
{
    WifiIotGpioValue l = read_ir(GPIO_IR_L), r = read_ir(GPIO_IR_R);
    int ok = (l == ground_level_left && r == ground_level_right);
    if (ok) return 0;
    usleep(3000);    /* 3ms 后复读确认，滤掉瞬时噪声 */
    l = read_ir(GPIO_IR_L);
    r = read_ir(GPIO_IR_R);
    ok = (l == ground_level_left && r == ground_level_right);
    if (ok) return 0;
    if (out_left) *out_left = (l != ground_level_left);
    return 1;
}

/* ---------- 运动原语（V2 帧目标，单位 0.01圈/s） ---------- */
static void car_forward(void)  { stm32_send_frame(CRUISE, CRUISE); }
static void car_backward(void) { stm32_send_frame(-CRUISE, -CRUISE); }
static void car_stop(void)     { stm32_send_frame(0, 0); }
/* 单轮支点转向（task14 实测 1400ms≈90°，避免差速弧线转角不足） */
static void car_left(void)     { stm32_send_frame(0, 70); }
static void car_right(void)    { stm32_send_frame(70, 0); }

/* ---------- 受控运动段：带红外检测 + 演示计时 ---------- */
#define STEP_MS 40             /* 受控段步进：40ms 一查红外（每查走 ~3.4mm） */

static hi_u64 demo_deadline_us = 0;   /* 演示总截止时刻，0=未开始 */

/* 前进 duration_ms（0=一直前进）。返回：0=正常走完，1=踩到桌沿/胶带（已停车），
 * 2=演示时间到（已停车）。 */
static int forward_for(uint32_t duration_ms)
{
    hi_u64 until = hi_get_us() + (hi_u64)duration_ms * 1000ULL;
    int dummy;

    for (;;) {
        if (demo_deadline_us && hi_get_us() >= demo_deadline_us) {
            car_stop();
            return 2;
        }
        if (danger_detected(&dummy)) {
            car_stop();
            return 1;
        }
        car_forward();
        if (duration_ms == 0) {
            usleep(STEP_MS * 1000);
            continue;
        }
        if (hi_get_us() >= until) return 0;
        usleep(STEP_MS * 1000);
    }
}

/* 原地转 duration_ms。同 forward_for 返回值。 */
static int spin_for(int turn_left, uint32_t duration_ms)
{
    hi_u64 until = hi_get_us() + (hi_u64)duration_ms * 1000ULL;
    int dummy;

    for (;;) {
        if (demo_deadline_us && hi_get_us() >= demo_deadline_us) {
            car_stop();
            return 2;
        }
        if (danger_detected(&dummy)) {
            car_stop();
            return 1;
        }
        if (duration_ms != 0 && hi_get_us() >= until) break;
        if (turn_left) car_left(); else car_right();
        usleep(STEP_MS * 1000);
    }
    car_stop();
    return 0;
}

/* 后退 duration_ms（避障脱离用）。后退方向无红外监视，短时盲退。 */
static void backward_for(uint32_t duration_ms)
{
    hi_u64 until = hi_get_us() + (hi_u64)duration_ms * 1000ULL;
    while (hi_get_us() < until) {
        car_backward();
        usleep(STEP_MS * 1000);
    }
    car_stop();
}

/* ---------- 避障决策（task14 实机验证逻辑） ---------- */
#define SCAN_SETTLE_MS  300    /* 舵机到位后等旧回波消散 */
#define ESCAPE_MS       700    /* 转向后直行脱离时长 */

/* 前方受阻时舵机扫两侧，返回 0=走左 1=走右 2=双侧都堵（掉头） */
static int scan_choose_side(void)
{
    float dl[3], dr[3], tmp;
    int i, j;

    servo_move(SERVO_LEFT_US);
    usleep(SCAN_SETTLE_MS * 1000);
    for (i = 0; i < 3; i++) { dl[i] = get_distance_cm(); usleep(60000); }
    for (i = 0; i < 2; i++) for (j = i + 1; j < 3; j++)
        if (dl[i] > dl[j]) { tmp = dl[i]; dl[i] = dl[j]; dl[j] = tmp; }

    servo_move(SERVO_RIGHT_US);
    usleep(SCAN_SETTLE_MS * 1000);
    for (i = 0; i < 3; i++) { dr[i] = get_distance_cm(); usleep(60000); }
    for (i = 0; i < 2; i++) for (j = i + 1; j < 3; j++)
        if (dr[i] > dr[j]) { tmp = dr[i]; dr[i] = dr[j]; dr[j] = tmp; }

    servo_center();
    usleep(SCAN_SETTLE_MS * 1000);
    printf("SCAN L=%d R=%d\r\n", (int)dl[1], (int)dr[1]);
    if (dl[1] < OBSTACLE_CM && dr[1] < OBSTACLE_CM) return 2;
    return (dl[1] >= dr[1]) ? 0 : 1;   /* 哪边远走哪边 */
}

/* 红外危险处理：停 → 倒车脱离 → 朝另一侧转 90°（远离边线）。
 * danger_left=1 表示左传感器压线（往右转），0=右侧压线（往左转）。 */
static void escape_danger(int danger_left)
{
    printf("DANGER %s -> back+turn\r\n", danger_left ? "LEFT" : "RIGHT");
    car_stop();
    usleep(100000);
    backward_for(BACK_MS);
    usleep(100000);
    (void)spin_for(!danger_left, TURN_90_MS);
}

/* ---------- 主状态机 ---------- */
static void maze_patrol(void)
{
    float dist;
    int rc, side, dummy;

    printf("Maze patrol v1: calibrating ground, keep car on NORMAL floor...\r\n");
    servo_center();
    (void)calibrate_ground();

    printf("Start in 3s. DEMO_RUN=%ds\r\n", DEMO_RUN_SECS);
    usleep(3000000);
    demo_deadline_us = hi_get_us() + (hi_u64)DEMO_RUN_SECS * 1000000ULL;

    while (1) {
        rc = forward_for(0);            /* 前进，直到危险/受阻/到时 */
        if (rc == 2) break;             /* 演示时间到 */
        if (rc == 1) {                  /* 踩线/桌沿 */
            danger_detected(&dummy);    /* 复读确认 */
            usleep(200000);
            if (danger_detected(&dummy)) {
                escape_danger(dummy);
                continue;
            }
            continue;                   /* 假信号：继续前进 */
        }

        dist = get_distance_cm();       /* 前方箱壁 */
        if (dist < OBSTACLE_CM) {
            printf("OBSTACLE %dcm\r\n", (int)dist);
            car_stop();
            usleep(250000);
            side = scan_choose_side();
            if (side == 2) {
                printf("BOTH BLOCKED -> U-TURN\r\n");
                (void)spin_for(1, TURN_90_MS);
                usleep(100000);
                (void)spin_for(1, TURN_90_MS);
            } else if (side == 0) {
                printf("TURN LEFT 90\r\n");
                (void)spin_for(1, TURN_90_MS);
            } else {
                printf("TURN RIGHT 90\r\n");
                (void)spin_for(0, TURN_90_MS);
            }
            car_stop();
            usleep(100000);
            (void)forward_for(ESCAPE_MS);   /* 受控直行脱离，途中踩线会再触发 */
            continue;
        }
        usleep(STEP_MS * 1000);
    }

    car_stop();
    printf("DEMO DONE, car stopped.\r\n");
}

/*****任务创建*****/
static void maze_patrol_entry(void)
{
    osThreadAttr_t attr;

    GpioInit();

    /* GPIO11 普通输出，软件 UART（绕开双 UART 冲突，task15 同款） */
    hi_io_set_func(STM32_TX_GPIO, 0);
    GpioSetDir(STM32_TX_GPIO, WIFI_IOT_GPIO_DIR_OUT);
    GpioSetOutputVal(STM32_TX_GPIO, WIFI_IOT_GPIO_VALUE1);

    sensor_init();

    attr.attr_bits = 0U;
    attr.cb_mem = NULL;
    attr.cb_size = 0U;
    attr.stack_mem = NULL;
    attr.stack_size = 1024 * 4;
    attr.priority = 25;
    attr.name = "maze_patrol";
    if (osThreadNew((osThreadFunc_t)maze_patrol, NULL, &attr) == NULL) {
        printf("Failed to create maze_patrol!\r\n");
    }
}
APP_FEATURE_INIT(maze_patrol_entry);
