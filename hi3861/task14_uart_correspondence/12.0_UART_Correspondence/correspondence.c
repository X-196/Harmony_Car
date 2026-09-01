/*
 * 任务24：系统通信协议（双核综合）
 *
 * Hi3861（主核）通过 UART2（GPIO_11=TXD / GPIO_12=RXD，115200-8-N-1）
 * 向 STM32（从核）发送 6 字节运动控制帧：
 *
 *   Byte1  0xFC    帧头
 *   Byte2  0/1     左轮方向（0 正转/前进，1 反转/后退）
 *   Byte3  0~150   左轮速度（实际转速 ×100，单位 圈/s，精度 0.01）
 *   Byte4  0/1     右轮方向
 *   Byte5  0~150   右轮速度
 *   Byte6  0xFD    帧尾
 *
 * 学生任务：更改 3861 的程序，验证小车前进、后退、左转、右转动作。
 * 本工程循环演示：前进 2s → 左转 2s → 前进 2s → 右转 2s → 后退 2s → 停止 2s
 * （左/右转的同时 STM32 侧会点亮对应侧转向灯，后退点亮倒车灯——见 STM32 侧工程）
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

static void car_demo(void);

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

// 小车前进（0.7圈/s，约10cm/s，演示用低速更稳）
void car_forward(void)
{
    stm32motor_control(70, 70);
}

// 小车后退
void car_backward(void)
{
    stm32motor_control(-70, -70);
}

// 小车左转前进（缓弧线：左轮0.65/右轮1.1圈/s，循迹同款差速，边走边转不甩头）
void car_left(void)
{
    stm32motor_control(65, 110);
}

// 小车右转前进（缓弧线：左轮1.1/右轮0.65圈/s）
void car_right(void)
{
    stm32motor_control(110, 65);
}

// 小车停止
void car_stop(void)
{
    stm32motor_control(0, 0);
}

/* 动作步进：持续 duration_ms 毫秒，期间每 200ms 重发一次协议帧（抗帧丢失） */
static void run_step(void (*action)(void), uint32_t duration_ms)
{
    uint32_t elapsed = 0;
    action();                              // 立即发第一帧
    while (elapsed < duration_ms) {
        usleep(200000);                    // 200ms
        elapsed += 200;
        if (elapsed < duration_ms) {
            action();                      // 周期重发
        }
    }
}

/*
 * 演示任务（时序按演示需求调）：
 *   前进 3s → 左转前进 2s → 右转前进 2s → 停 0.5s（PID刹车缓冲）
 *   → 倒车 2s → 停 2s，循环。
 * 帧每 200ms 周期重发：万一某帧被干扰损坏，200ms 内下一帧即纠正，
 * 比只发一次抗噪得多（讲解同款机制）。
 */
static void car_demo(void)
{
    printf("Dual-core protocol demo start\r\n");
    while (1) {
        printf("FORWARD\r\n");
        run_step(car_forward, 3000);   // 前进 3s

        printf("LEFT\r\n");
        run_step(car_left, 2000);      // 左转前进 2s（左转向灯闪）

        printf("RIGHT\r\n");
        run_step(car_right, 2000);     // 右转前进 2s（右转向灯闪）

        printf("STOP\r\n");
        run_step(car_stop, 500);       // 停 0.5s（刹车缓冲，前进->倒车不硬反向）

        printf("BACKWARD\r\n");
        run_step(car_backward, 2000);  // 倒车 2s（倒车灯亮）

        printf("STOP\r\n");
        run_step(car_stop, 2000);      // 停 2s（灯全灭）
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

    attr.attr_bits = 0U;        // 设置osThreadJoin是否可以使用
    attr.cb_mem = NULL;         // 控制块指针设置
    attr.cb_size = 0U;          // 控制块指针大小
    attr.stack_mem = NULL;      // 任务栈设置
    attr.stack_size = 1024 * 2; // 任务栈大小
    attr.priority = 25;         // 任务优先级
    attr.name = "car_demo";     // 任务名称
    if (osThreadNew((osThreadFunc_t)car_demo, NULL, &attr) == NULL) {
        printf("Falied to create car_demo!\n");
    }
}
APP_FEATURE_INIT(correspondence); // 启动任务
