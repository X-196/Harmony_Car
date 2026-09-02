/*
 * 蓝牙遥控小车（扩展：任务9 蓝牙 + 任务24 双核协议的综合应用）
 *
 * 链路：
 *   手机蓝牙App --BLE--> JDY-16透传 --UART1(GPIO_0=TX/GPIO_1=RX, 9600-8N1)--> Hi3861(主核)
 *   Hi3861 解析单字符命令，通过 UART2(GPIO_11=TX/GPIO_12=RX, 115200-8N1)
 *   向 STM32(从核, stm32/8_双核协议控制) 发 V2 10字节运动控制帧：
 *     0xFC | 0x02 | 0x0A | 左轮int16(×100) | 右轮int16(×100) | 序号 | XOR | 0xFD
 *
 * 命令集（大小写均可，回车/换行/空格忽略）：
 *   W=前进  S=后退  A=左转  D=右转  O=停止（手机九宫格方向键布局）
 *   I=中速100档  K=高速150档（设定巡航速度档，之后 W/S 按当前档行驶）
 *   备用字母（App 按钮键盘 A~K 没有 W/S/O 时映射用）：
 *   B=后退  C=右转  E=停止  F=左转
 *
 * 可靠性设计（对应 STM32 侧 500ms 无帧自动停车）：
 *   1. 心跳重发：每 100ms 无条件重发当前目标帧——保证 STM32 始终有有效帧，
 *      不会因单帧丢失或间隙触发 500ms 无帧自动停车。
 *   2. 命令改变立即发一帧，不等心跳周期，方向键按下马上响应。
 *   3. 发帧用【局部缓冲 + 互斥锁】：心跳线程与命令线程并发调用 stm32motor_control，
 *      若共用全局发送缓冲会互相覆盖、破坏帧导致 STM32 校验失败丢弃（此前的 bug），
 *      故每帧用私有局部数组，并用互斥锁串行化 UART2 发送。
 *
 * 走停由手机 App 控制：按下方向键发动作（按住期间心跳维持），松开发 O（或按停）停车。
 * 本固件不做“命令超时自动停车”——否则长按方向键 2 秒会被自动掐断（此前 bug）。
 *
 * 车灯全部由 STM32 从核按帧自动渲染（左转闪左转向灯/后退倒车灯等），
 * 本工程不关心灯光——双核分工的收益。
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
#include "hi_uart.h"
#include "wifiiot_gpio_ex.h"

/* 上电自检开关：1=只初始化UART2并持续发前进帧(复刻任务14单线程发帧,
 * 定位 3861->STM32->电机 链路); 0=蓝牙遥控 */
#define SELFTEST_AUTODRIVE 0

static osMutexId_t uart2_mutex = NULL;      /* 自检模式不使用；保留接口兼容 */
static int speed_level = 100;                /* 当前速度档（I=100 / K=150） */
static volatile int cur_a = 0, cur_b = 0;    /* 当前锁存的左右轮目标 */
static uint32_t next_heartbeat_tick = 0;      /* 单线程心跳调度 */

/*==================== UART2 -> STM32 运动控制协议 ====================*/

/*
 * 函数功能：发送至 STM32 的 V2 运动控制帧
 * 参数    ：左右轮有符号速度 ×100（单位 0.01 圈/s），范围 -150~150
 * 说明    ：每帧用私有局部缓冲，互斥锁串行化发送，避免多线程并发写入同一缓冲。
 */
void stm32motor_control(int motorA, int motorB)
{
    static uint8_t seq = 0;
    uint8_t checksum;
    uint8_t buf[10];                 /* 私有帧缓冲：避免心跳/命令线程相互覆盖 */

    if (motorA > 150) motorA = 150;
    if (motorA < -150) motorA = -150;
    if (motorB > 150) motorB = 150;
    if (motorB < -150) motorB = -150;

    // FC | version | length | left int16 LE | right int16 LE | seq | xor | FD
    buf[0] = 0xFC;
    buf[1] = 0x02;
    buf[2] = 0x0A;
    buf[3] = (uint8_t)(motorA & 0xFF);
    buf[4] = (uint8_t)((motorA >> 8) & 0xFF);
    buf[5] = (uint8_t)(motorB & 0xFF);
    buf[6] = (uint8_t)((motorB >> 8) & 0xFF);
    buf[7] = seq++;
    checksum = buf[1] ^ buf[2] ^ buf[3] ^
               buf[4] ^ buf[5] ^ buf[6] ^ buf[7];
    buf[8] = checksum;
    buf[9] = 0xFD;

    UartWrite(WIFI_IOT_UART_IDX_2, (unsigned char *)buf, 10);

#if SELFTEST_AUTODRIVE
    {   /* 自检版：把发出的帧字节打到串口0，确认 3861 确实在发 */
        unsigned int i;
        printf("TX:");
        for (i = 0; i < 10; i++) printf(" %02X", buf[i]);
        printf("\r\n");
    }
#endif
}

/* 发送新目标并锁存（心跳线程按此值重发） */
static void car_set(int a, int b)
{
    cur_a = a; cur_b = b;
    stm32motor_control(a, b);
}

void car_forward(void)   { car_set(speed_level, speed_level); }
void car_backward(void)  { car_set(-speed_level, -speed_level); }
void car_left(void)      { car_set(0, speed_level + 15); }     /* 左轮停/右轮转，支点转向 */
void car_right(void)     { car_set(speed_level + 15, 0); }
void car_stop(void)      { car_set(0, 0); }

/*==================== 蓝牙接收与心跳（单线程） ====================*/

/*
 * 同一个任务独占 UART2：收到命令立即发帧，每 100ms 发心跳。
 * 这样不会出现接收线程和心跳线程同时调用 UartWrite 的竞争。
 */
static void ble_ctrl_task(void)
{
    uint8_t byte;
    unsigned int n;
    uint32_t now;

    printf("BLE control ready: W/A/S/D/O + I/K\r\n");
    next_heartbeat_tick = osKernelGetTickCount();
    for (;;) {
        n = UartRead(WIFI_IOT_UART_IDX_1, &byte, 1);
        if (n == 1) {
            char c = (char)byte;
            switch (c) {
                case 'w': case 'W':
                    car_forward(); printf("CMD W: forward %d\r\n", speed_level); break;
                case 'a': case 'A':
                    car_left();    printf("CMD A: left\r\n"); break;
                case 's': case 'S':
                    car_backward();printf("CMD S: backward %d\r\n", speed_level); break;
                case 'd': case 'D':
                    car_right();   printf("CMD D: right\r\n"); break;
                case 'o': case 'O':
                    car_stop();    printf("CMD O: stop\r\n"); break;
                case 'i': case 'I':
                    speed_level = 100; printf("CMD I: speed 100\r\n"); break;
                case 'k': case 'K':
                    speed_level = 150; printf("CMD K: speed 150\r\n"); break;
                case 'b': case 'B':
                    car_backward();printf("CMD B: backward %d\r\n", speed_level); break;
                case 'c': case 'C':
                    car_right();   printf("CMD C: right\r\n"); break;
                case 'e': case 'E':
                    car_stop();    printf("CMD E: stop\r\n"); break;
                case 'f': case 'F':
                    car_left();    printf("CMD F: left\r\n"); break;
                case '\r': case '\n': case ' ':
                    break;
                default:
                    printf("CMD? [%02x]\r\n", (unsigned char)c); break;
            }
        }

        now = osKernelGetTickCount();
        if ((int32_t)(now - next_heartbeat_tick) >= 0) {
            stm32motor_control(cur_a, cur_b);
            next_heartbeat_tick = now + 10;  /* 100ms @ 100Hz */
        }
        osDelay(2);  /* 20ms 轮询；命令线程和心跳共用此任务 */
    }
}

/*****任务创建*****/
static void ble_control(void)
{
    osThreadAttr_t attr;

    GpioInit(); // GPIO功能初始化

    /* UART2：与 STM32 通信（115200）。必须先初始化 UART2：
     * Hi3861 旧版 UART HAL 初始化 UART1 时会重置共享串口控制状态；
     * 若先 UART1 后 UART2，UART2 看似初始化成功但实际不出有效波形。
     */
    IoSetFunc(WIFI_IOT_IO_NAME_GPIO_11, WIFI_IOT_IO_FUNC_GPIO_11_UART2_TXD);
    IoSetFunc(WIFI_IOT_IO_NAME_GPIO_12, WIFI_IOT_IO_FUNC_GPIO_12_UART2_RXD);
    WifiIotUartAttribute uart_attr2 = {
        .baudRate = 115200,
        .dataBits = 8,
        .stopBits = 1,
        .parity = 0,
    };
    UartInit(WIFI_IOT_UART_IDX_2, &uart_attr2, NULL);

    /* UART1：蓝牙 JDY-16 透传（9600）。放在 UART2 后初始化，避免旧 HAL
     * 初始化 UART1 时重置 UART2 的共享控制寄存器。 */
#if !SELFTEST_AUTODRIVE
    IoSetFunc(WIFI_IOT_IO_NAME_GPIO_0, WIFI_IOT_IO_FUNC_GPIO_0_UART1_TXD);
    IoSetFunc(WIFI_IOT_IO_NAME_GPIO_1, WIFI_IOT_IO_FUNC_GPIO_1_UART1_RXD);
    WifiIotUartAttribute uart_attr1 = {
        .baudRate = 9600,
        .dataBits = 8,
        .stopBits = 1,
        .parity = 0,
    };
    UartInit(WIFI_IOT_UART_IDX_1, &uart_attr1, NULL);
#endif

#if SELFTEST_AUTODRIVE
    /* 自检：绕开蓝牙/心跳/互斥，上电持续发前进帧，复刻任务14单线程发帧 */
    printf("SELFTEST: auto forward...\r\n");
    for (;;) {
        stm32motor_control(100, 100);
        osDelay(50);
    }
#endif

    /* 正式模式使用单一控制线程，UART2 不再需要单独的发送互斥锁 */
    uart2_mutex = NULL;

    attr.attr_bits = 0U;
    attr.cb_mem = NULL;
    attr.cb_size = 0U;
    attr.stack_mem = NULL;
    attr.stack_size = 1024 * 4;
    attr.priority = 25;

    attr.name = "ble_ctrl";
    if (osThreadNew((osThreadFunc_t)ble_ctrl_task, NULL, &attr) == NULL) {
        printf("Falied to create ble_ctrl!\n");
    }
}
APP_FEATURE_INIT(ble_control); // 启动任务
