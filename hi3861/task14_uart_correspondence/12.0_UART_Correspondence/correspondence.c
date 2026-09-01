/*
 * 任务24：系统通信协议（双核综合）——桌面巡逻模式
 *
 * Hi3861（主核）通过 UART2（GPIO_11=TXD / GPIO_12=RXD，115200-8-N-1）
 * 向 STM32（从核）发送 6 字节运动控制帧：
 *   0xFC | 左轮方向(0/1) | 左轮速度(×100,圈/s) | 右轮方向 | 右轮速度 | 0xFD
 *
 * 本模式：小车在桌面上自主巡逻，不会掉下桌子，并避让周围障碍物。
 *   - 防跌落：TCRT5000 红外对管朝下探桌沿（GPIO_13=左 / GPIO_14=右），
 *     任一侧"下方无反射"→ 立即停车 → 倒车 → 向另一侧原地转向离开
 *   - 避障：HC-SR04 超声波（GPIO_7=TRIG / GPIO_8=ECHO）前方测距，
 *     <25cm → 停车 → SG90 舵机（GPIO_2）带超声波左右扫描 → 向空侧转向；
 *     两侧都近 → 掉头
 *   - 开机自标定：在桌面中央读 10 次红外取多数为"地面电平"，
 *     桌面深浅颜色不影响判断（edge = 读数 ≠ 地面电平）
 *   - 巡航低速 0.6 圈/s（≈8.5cm/s），30ms 一查桌沿（每查走 ~3mm），
 *     30ms 内两次确认才动作，抗红外噪声
 *   - 测距带 30ms 超时保护（官方 while(1) 无超时，挂死=小车冲下桌）
 *
 * 转向灯由 STM32 从核根据协议帧自动点亮（左转闪左半边/右转闪右半边），
 * 本工程不需要关心灯光——双核分工的收益。
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <memory.h>
#include "wifiiot_uart.h"
#include "ohos_init.h"
#include "cmsis_os2.h"
#include "wifiiot_gpio.h"
#include "hi_io.h"
#include "hi_time.h"
#include "wifiiot_pwm.h"
#include "hi_pwm.h"
#include "hi_uart.h"
#include "wifiiot_gpio_ex.h"

static void car_patrol(void);

uint8_t uart_sendbuf[20];

/***通信协议***/
/*
函数功能 ：发送至stm32的数据协议
参数    ： 电机实际转速的一百倍，例如：设置转速为1rad/s，则传入100
*/
void stm32motor_control(int motorA, int motorB)
{
    uint8_t A_dir = 0;
    uint8_t B_dir = 0;

    //小车运动方向 前进（正转）：0   后退（反转） 1
    if (motorA < 0) {
        A_dir = 1;
        motorA = -motorA;
    } else {
        A_dir = 0;
    }
    if (motorB < 0) {
        B_dir = 1;
        motorB = -motorB;
    } else {
        B_dir = 0;
    }
    //限制幅度 -150 ~150
    if (motorA > 150) {
        motorA = 150;
    }
    if (motorB > 150) {
        motorB = 150;
    }

    // 数据协议
    uart_sendbuf[0] = 0xFC;   // 帧头
    uart_sendbuf[1] = A_dir;  // 左轮方向    0正转，1反转
    uart_sendbuf[2] = motorA; // 左轮速度
    uart_sendbuf[3] = B_dir;  // 右轮方向    0正转，1反转
    uart_sendbuf[4] = motorB; // 右轮速度
    uart_sendbuf[5] = 0xFD;   // 帧尾
    UartWrite(WIFI_IOT_UART_IDX_2, (unsigned char *)uart_sendbuf, 6);
}

// 小车前进（巡逻巡航 0.6 圈/s ≈ 8.5cm/s，低速保证桌沿检测余量）
void car_forward(void)
{
    stm32motor_control(60, 60);
}

// 小车后退
void car_backward(void)
{
    stm32motor_control(-60, -60);
}

// 原地左转（左轮反转/右轮正转，避障用）
void car_left(void)
{
    stm32motor_control(-50, 150);
}

// 原地右转（左轮正转/右轮反转）
void car_right(void)
{
    stm32motor_control(150, -50);
}

// 缓弧左转前进（循迹用差速）
void car_left_tra(void)
{
    stm32motor_control(65, 110);
}

// 缓弧右转前进
void car_right_tra(void)
{
    stm32motor_control(110, 65);
}

// 小车停止
void car_stop(void)
{
    stm32motor_control(0, 0);
}

/*==================== 传感器驱动（源自 supportPack 官方代码） ====================*/

// 红外对管（朝下探桌沿）：左 GPIO_13 / 右 GPIO_14
#define GPIOL 13
#define GPIOR 14
// HC-SR04 超声波：GPIO_7=TRIG / GPIO_8=ECHO（装在 SG90 舵机云台上）
#define GPIO_TRIG 7
#define GPIO_ECHO 8
// SG90 舵机：GPIO_2
#define GPIO_SG90 2

// 地面电平（开机标定）：读数 != 地面电平 => 探到桌沿/悬空
static WifiIotGpioValue ground_level = WIFI_IOT_GPIO_VALUE0;

static void sensor_init(void)
{
    GpioSetDir(GPIOL, WIFI_IOT_GPIO_DIR_IN);    // 左红外输入
    GpioSetDir(GPIOR, WIFI_IOT_GPIO_DIR_IN);    // 右红外输入
    GpioSetDir(GPIO_TRIG, WIFI_IOT_GPIO_DIR_OUT); // 超声波触发输出
    GpioSetDir(GPIO_ECHO, WIFI_IOT_GPIO_DIR_IN);  // 超声波回波输入
    GpioSetOutputVal(GPIO_TRIG, WIFI_IOT_GPIO_VALUE0);
}

static WifiIotGpioValue read_ir(unsigned int gpio)
{
    WifiIotGpioValue v = WIFI_IOT_GPIO_VALUE0;
    GpioGetInputVal(gpio, &v);
    return v;
}

/*
 * 超声波测距（带 30ms 超时保护），返回 cm；超时/无回波返回 999。
 * 官方 GetDistance 的 while(1) 无超时——回波丢失会永久挂死任务，
 * 小车失控冲下桌子，桌面模式下必须加超时。
 */
static float get_distance_cm(void)
{
    unsigned long long t0 = 0, now;
    WifiIotGpioValue value = WIFI_IOT_GPIO_VALUE0;
    unsigned int flag = 0;
    unsigned long long timeout;

    // 触发脉冲 20us
    GpioSetOutputVal(GPIO_TRIG, WIFI_IOT_GPIO_VALUE1);
    hi_udelay(20);
    GpioSetOutputVal(GPIO_TRIG, WIFI_IOT_GPIO_VALUE0);

    timeout = hi_get_us() + 30000;   // 最多等 30ms（约 5m 量程）
    while (1) {
        GpioGetInputVal(GPIO_ECHO, &value);
        now = hi_get_us();
        if (now > timeout) {
            return 999.0f;           // 超时：视为无障碍（探测失败不影响行驶）
        }
        if (value == WIFI_IOT_GPIO_VALUE1 && flag == 0) {
            t0 = now;                // 回波上升沿
            flag = 1;
        }
        if (value == WIFI_IOT_GPIO_VALUE0 && flag == 1) {
            return (float)(now - t0) * 0.034f / 2.0f;  // cm
        }
    }
}

/* SG90 舵机：x 微秒高电平 + (20000-x) 微秒低电平 为一帧 */
static void set_angle(unsigned int duty)
{
    GpioSetDir(GPIO_SG90, WIFI_IOT_GPIO_DIR_OUT);
    GpioSetOutputVal(GPIO_SG90, WIFI_IOT_GPIO_VALUE1);
    hi_udelay(duty);
    GpioSetOutputVal(GPIO_SG90, WIFI_IOT_GPIO_VALUE0);
    hi_udelay(20000 - duty);
}

static void engine_turn_left(void)   // 舵机带超声波转向左 45°
{
    for (int i = 0; i < 10; i++) {
        set_angle(2200);
    }
}

static void engine_turn_right(void)  // 右 45°
{
    for (int i = 0; i < 10; i++) {
        set_angle(1100);
    }
}

static void regress_middle(void)     // 回中
{
    for (int i = 0; i < 10; i++) {
        set_angle(1650);
    }
}

/*==================== 桌面巡逻逻辑 ====================*/

#define EDGE_CHECK_MS   30      // 桌沿检测周期
#define OBSTACLE_CM     25.0f   // 前方障碍判定距离
#define SPIN_MS         700     // 原地转一次的时长（约 90°，实车可调）
#define BACK_MS         300     // 探到桌先后倒车时长

/* 桌沿确认：连续两次读数都"非地面电平"才认定（30ms 间隔，抗噪声） */
static int edge_detected(int *left_edge)
{
    WifiIotGpioValue l = read_ir(GPIOL);
    WifiIotGpioValue r = read_ir(GPIOR);

    if (l == ground_level && r == ground_level) {
        return 0;                       // 两侧都在桌面上
    }
    usleep(5000);                       // 5ms 后复核
    l = read_ir(GPIOL);
    r = read_ir(GPIOR);
    if (l == ground_level && r == ground_level) {
        return 0;                       // 毛刺，忽略
    }
    *left_edge = (l != ground_level);   // 记下哪一侧悬空
    return 1;
}

/* 原地转向 duration_ms，期间持续检测桌沿（探到就中断返回1，外层重新处理） */
static int spin_for(int turn_left, uint32_t duration_ms)
{
    uint32_t t = 0;
    int dummy;
    while (t < duration_ms) {
        if (turn_left) car_left(); else car_right();
        usleep(EDGE_CHECK_MS);
        t += EDGE_CHECK_MS;
        if (edge_detected(&dummy)) {    // 转弯途中探到桌沿：停，交给外层
            return 1;
        }
    }
    return 0;
}

/*
 * 开机标定地面电平：小车此刻应放在桌面中央。
 * 连续读 10 次左红外取多数（开机时两侧都在桌面上，以左侧为准）。
 * 这样无论桌面深色/浅色，"桌沿=读数突变"，不会因桌面颜色误判。
 */
static void calibrate_ground(void)
{
    int cnt1 = 0, i;
    for (i = 0; i < 10; i++) {
        if (read_ir(GPIOL) == WIFI_IOT_GPIO_VALUE1) cnt1++;
        usleep(20000);
    }
    ground_level = (cnt1 >= 5) ? WIFI_IOT_GPIO_VALUE1 : WIFI_IOT_GPIO_VALUE0;
    printf("Ground calibrated: %d (1=%d/10)\r\n", (int)ground_level, cnt1);
}

/*
 * 桌面巡逻主任务：
 *   前进(30ms一查桌沿) -> [桌沿] 停/倒车/反向转  -> 继续
 *                       -> [障碍] 停/扫描/向空侧转 -> 继续
 */
static void car_patrol(void)
{
    int left_edge;
    float dist, dist_l, dist_r;

    printf("Table patrol start\r\n");
    calibrate_ground();
    regress_middle();               // 舵机回中，超声波朝前
    usleep(100000);

    while (1) {
        /*---- 1. 桌沿检测（最高优先级） ----*/
        if (edge_detected(&left_edge)) {
            printf("EDGE %s!\r\n", left_edge ? "L" : "R");
            car_stop();
            usleep(150000);         // 刹车
            car_backward();         // 倒车离开桌沿
            usleep(BACK_MS * 1000);
            car_stop();
            usleep(100000);
            (void)spin_for(left_edge ? 0 : 1, SPIN_MS);  // 向另一侧转 90°
            continue;               // 回到巡航
        }

        /*---- 2. 前方障碍检测 ----*/
        dist = get_distance_cm();
        if (dist < OBSTACLE_CM) {
            printf("OBSTACLE %dcm, scan...\r\n", (int)dist);
            car_stop();
            usleep(100000);

            engine_turn_left();     // 舵机带超声波扫左
            usleep(50000);
            dist_l = get_distance_cm();
            engine_turn_right();    // 扫右
            usleep(50000);
            dist_r = get_distance_cm();
            regress_middle();       // 回中
            printf("L=%dcm R=%dcm\r\n", (int)dist_l, (int)dist_r);

            if (dist_l < OBSTACLE_CM && dist_r < OBSTACLE_CM) {
                // 两侧都堵：掉头（两个 90°）
                (void)spin_for(1, SPIN_MS);
                (void)spin_for(1, SPIN_MS);
            } else if (dist_l >= dist_r) {
                (void)spin_for(1, SPIN_MS);   // 左侧更空 -> 左转
            } else {
                (void)spin_for(0, SPIN_MS);   // 右侧更空 -> 右转
            }
            continue;
        }

        /*---- 3. 正常巡航 ----*/
        car_forward();              // 每周期重发帧（坏帧下一周期即纠正）
        usleep(EDGE_CHECK_MS);
    }
}

/*****任务创建*****/
static void correspondence(void)
{
    osThreadAttr_t attr;

    GpioInit(); // GPIO功能初始化
    /**********************通讯串口初始化******************/
    IoSetFunc(WIFI_IOT_IO_NAME_GPIO_11, WIFI_IOT_IO_FUNC_GPIO_11_UART2_TXD); // GPIO_11复用为UART2_TXD
    IoSetFunc(WIFI_IOT_IO_NAME_GPIO_12, WIFI_IOT_IO_FUNC_GPIO_12_UART2_RXD); // GPIO_12复用为UART2_RXD

    /***************串口参数******************/
    WifiIotUartAttribute uart_attr2 = {
        // 波特率: 115200
        .baudRate = 115200,
        // 数据位: 8bits
        .dataBits = 8,
        .stopBits = 1,
        .parity = 0,
    };
    UartInit(WIFI_IOT_UART_IDX_2, &uart_attr2, NULL);

    sensor_init();   // 红外/超声波/舵机引脚初始化

    attr.attr_bits = 0U;        // 设置osThreadJoin是否可以使用
    attr.cb_mem = NULL;         // 控制块指针设置
    attr.cb_size = 0U;          // 控制块指针大小
    attr.stack_mem = NULL;      // 任务栈设置
    attr.stack_size = 1024 * 4; // 任务栈大小
    attr.priority = 25;         // 任务优先级
    attr.name = "car_patrol";   // 任务名称
    if (osThreadNew((osThreadFunc_t)car_patrol, NULL, &attr) == NULL) {
        printf("Falied to create car_patrol!\n");
    }
}
APP_FEATURE_INIT(correspondence); // 启动任务
