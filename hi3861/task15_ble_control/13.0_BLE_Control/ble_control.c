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
 *   1. 心跳重发：每 150ms 重发一次当前速度帧——单帧丢帧不会导致中途停车
 *      （STM32 收帧刷新 no_frame_ticks，5×100ms 内必须有新帧）
 *   2. 命令改变立即发一帧，不等心跳周期，手机上方向键按下去马上响应
 *   3. 串口0 printf 与 UART2 控制帧分走两个串口，互不干扰
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

/*==================== UART2 -> STM32 运动控制协议 ====================*/

uint8_t uart_sendbuf[20];

/*
 * 函数功能：发送至 STM32 的 V2 运动控制帧
 * 参数    ：左右轮有符号速度 ×100（单位 0.01 圈/s），范围 -150~150
 */
void stm32motor_control(int motorA, int motorB)
{
    static uint8_t seq = 0;
    uint8_t checksum;

    if (motorA > 150) motorA = 150;
    if (motorA < -150) motorA = -150;
    if (motorB > 150) motorB = 150;
    if (motorB < -150) motorB = -150;

    // FC | version | length | left int16 LE | right int16 LE | seq | xor | FD
    uart_sendbuf[0] = 0xFC;
    uart_sendbuf[1] = 0x02;
    uart_sendbuf[2] = 0x0A;
    uart_sendbuf[3] = (uint8_t)(motorA & 0xFF);
    uart_sendbuf[4] = (uint8_t)((motorA >> 8) & 0xFF);
    uart_sendbuf[5] = (uint8_t)(motorB & 0xFF);
    uart_sendbuf[6] = (uint8_t)((motorB >> 8) & 0xFF);
    uart_sendbuf[7] = seq++;
    checksum = uart_sendbuf[1] ^ uart_sendbuf[2] ^ uart_sendbuf[3] ^
               uart_sendbuf[4] ^ uart_sendbuf[5] ^ uart_sendbuf[6] ^ uart_sendbuf[7];
    uart_sendbuf[8] = checksum;
    uart_sendbuf[9] = 0xFD;
    UartWrite(WIFI_IOT_UART_IDX_2, (unsigned char *)uart_sendbuf, 10);
}

/*==================== 遥控状态与动作 ====================*/

static int speed_level = 100;     /* 当前速度档（I=100 / K=150），默认中速 */
static int cur_a = 0, cur_b = 0;  /* 当前锁存的左右轮目标（心跳重发用） */

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

/*==================== 心跳与蓝牙接收 ====================*/

/*
 * 心跳线程：每 150ms 重发当前速度帧。
 * STM32 侧 500ms 收不到有效帧会自动停车（失效保护），遥控模式必须持续供帧。
 */
static void heartbeat_task(void)
{
    for (;;) {
        usleep(150 * 1000);
        if (cur_a == 0 && cur_b == 0) continue;  /* 停止态停发，让 STM32 刹车灯常亮而不是被反复唤醒 */
        stm32motor_control(cur_a, cur_b);
    }
}

/*
 * 蓝牙接收线程：UART1 逐字节读，解析单字符命令。
 * UartRead 无数据时阻塞挂起（不空转烧 CPU）。
 */
static void ble_ctrl_task(void)
{
    uint8_t byte;
    unsigned int n;

    printf("BLE control ready: W/A/S/D/O + I/K\r\n");
    for (;;) {
        n = UartRead(WIFI_IOT_UART_IDX_1, &byte, 1);
        if (n != 1) { usleep(20000); continue; }

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
            case 'b': case 'B':   /* 备用字母：后退（App 若映射 B 按钮） */
                car_backward();printf("CMD B: backward %d\r\n", speed_level); break;
            case 'c': case 'C':   /* 备用字母：右转 */
                car_right();   printf("CMD C: right\r\n"); break;
            case 'e': case 'E':   /* 备用字母：停止 */
                car_stop();    printf("CMD E: stop\r\n"); break;
            case 'f': case 'F':   /* 备用字母：左转 */
                car_left();    printf("CMD F: left\r\n"); break;
            case '\r': case '\n': case ' ':
                break;          /* 手机 App 回车确认/粘包填充，忽略 */
            default:
                printf("CMD? [%02x]\r\n", (unsigned char)c); break;
        }
    }
}

/*****任务创建*****/
static void ble_control(void)
{
    osThreadAttr_t attr;

    GpioInit(); // GPIO功能初始化

    /* UART1：蓝牙 JDY-16 透传（9600） */
    IoSetFunc(WIFI_IOT_IO_NAME_GPIO_0, WIFI_IOT_IO_FUNC_GPIO_0_UART1_TXD);
    IoSetFunc(WIFI_IOT_IO_NAME_GPIO_1, WIFI_IOT_IO_FUNC_GPIO_1_UART1_RXD);
    WifiIotUartAttribute uart_attr1 = {
        .baudRate = 9600,
        .dataBits = 8,
        .stopBits = 1,
        .parity = 0,
    };
    UartInit(WIFI_IOT_UART_IDX_1, &uart_attr1, NULL);

    /* UART2：与 STM32 通信（115200） */
    IoSetFunc(WIFI_IOT_IO_NAME_GPIO_11, WIFI_IOT_IO_FUNC_GPIO_11_UART2_TXD);
    IoSetFunc(WIFI_IOT_IO_NAME_GPIO_12, WIFI_IOT_IO_FUNC_GPIO_12_UART2_RXD);
    WifiIotUartAttribute uart_attr2 = {
        .baudRate = 115200,
        .dataBits = 8,
        .stopBits = 1,
        .parity = 0,
    };
    UartInit(WIFI_IOT_UART_IDX_2, &uart_attr2, NULL);

    car_stop();   /* 上电先发一帧停车，清掉 STM32 侧可能的历史目标 */

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

    attr.name = "heartbeat";
    attr.priority = 24;
    if (osThreadNew((osThreadFunc_t)heartbeat_task, NULL, &attr) == NULL) {
        printf("Falied to create heartbeat!\n");
    }
}
APP_FEATURE_INIT(ble_control); // 启动任务
