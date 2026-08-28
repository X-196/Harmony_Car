#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "ohos_init.h"
#include "cmsis_os2.h"
#include "wifiiot_gpio.h"
#include "wifiiot_gpio_ex.h"
#include "wifiiot_watchdog.h"
#include "wifiiot_uart.h"
#include "hi_io.h"
#include "hi_time.h"

/* 任务10：第一阶段综合实验
 * 目标：舵机左右旋转测距；前15s红外对管寻线；15s后蓝牙通信；
 *       任务3运行串口打印消息队列信息；任务1、2交替运行。
 * 综合任务6/7/8/9 的模块：舵机(SG90/GPIO2)、超声波(HC-SR04/GPIO7-8)、
 *       红外(TCRT/GPIO13-14)、蓝牙+UART1+消息队列。
 */

#define GPIO_SERVO 2      /* SG90 舵机信号 */
#define GPIO_TRIG  7      /* HC-SR04 TRIG */
#define GPIO_ECHO  8      /* HC-SR04 ECHO */
#define GPIO_IR_L  13     /* 红外左 TC_OUT_L */
#define GPIO_IR_R  14     /* 红外右 TC_OUT_R */
#define GPIO_TX    0      /* UART1_TXD */
#define GPIO_RX    1      /* UART1_RXD */

#define BAUD 9600
#define MSGQUEUE_OBJECTS 16

/* 消息队列对象 */
typedef struct { char *Buf; uint8_t Idx; } MSGQUEUE_OBJ_t;
MSGQUEUE_OBJ_t msg, msg_rx;
osMessageQueueId_t mid_MsgQueue;
osStatus_t status;

static void task1(void *arg);
static void task2(void *arg);
static void task3(void *arg);

/* ---------- 舵机：软件 PWM，20ms 周期 ---------- */
static void servo_pulse(unsigned int duty)   /* duty=高电平us */
{
    GpioSetOutputVal(GPIO_SERVO, WIFI_IOT_GPIO_VALUE1);
    hi_udelay(duty);
    GpioSetOutputVal(GPIO_SERVO, WIFI_IOT_GPIO_VALUE0);
    hi_udelay(20000 - duty);
}
static void servo_angle(unsigned int duty)
{
    unsigned int i;
    for (i = 0; i < 10; i++) servo_pulse(duty);
}

/* ---------- 超声波测距 ---------- */
static float get_distance(void)
{
    static unsigned long start_time = 0, time = 0;
    float distance = 0.0;
    WifiIotGpioValue value = WIFI_IOT_GPIO_VALUE0;
    unsigned int flag = 0;

    GpioSetDir(GPIO_ECHO, WIFI_IOT_GPIO_DIR_IN);
    GpioSetDir(GPIO_TRIG, WIFI_IOT_GPIO_DIR_OUT);
    GpioSetOutputVal(GPIO_TRIG, WIFI_IOT_GPIO_VALUE1);
    hi_udelay(20);
    GpioSetOutputVal(GPIO_TRIG, WIFI_IOT_GPIO_VALUE0);

    while (1)
    {
        GpioGetInputVal(GPIO_ECHO, &value);
        if (value == WIFI_IOT_GPIO_VALUE1 && flag == 0) { start_time = hi_get_us(); flag = 1; }
        if (value == WIFI_IOT_GPIO_VALUE0 && flag == 1) { time = hi_get_us() - start_time; break; }
    }
    distance = time * 0.034 / 2;
    return distance;
}

/* ---------- UART1 初始化（蓝牙透传） ---------- */
static void uart1_init(void)
{
    IoSetFunc(WIFI_IOT_IO_NAME_GPIO_0, WIFI_IOT_IO_FUNC_GPIO_0_UART1_TXD);
    IoSetFunc(WIFI_IOT_IO_NAME_GPIO_1, WIFI_IOT_IO_FUNC_GPIO_1_UART1_RXD);
    WifiIotUartAttribute ua = { .baudRate = BAUD, .dataBits = 8, .stopBits = 1, .parity = 0 };
    UartInit(WIFI_IOT_UART_IDX_1, &ua, NULL);
}

/* ---------- 任务1：舵机左右旋转测距 ---------- */
static void task1(void *arg)
{
    (void)arg;
    float d;
    for (;;)
    {
        servo_angle(1000);  /* 左 45° */
        d = get_distance(); printf("servo L dist=%.1f cm\r\n", d);
        osDelay(100);
        servo_angle(2000);  /* 右 135° */
        d = get_distance(); printf("servo R dist=%.1f cm\r\n", d);
        osDelay(100);
        servo_angle(1500);  /* 中 90° */
        d = get_distance(); printf("servo C dist=%.1f cm\r\n", d);
        osDelay(200);
    }
}

/* ---------- 任务2：前15s红外寻线，15s后蓝牙 ---------- */
static void task2(void *arg)
{
    (void)arg;
    uint32_t freq = osKernelGetTickFreq();          /* 实际 tick 频率 */
    uint32_t start = hi_get_tick();
    uint32_t limit = 15 * freq;                     /* 15s 对应的 tick 数 */
    uint8_t buf[256] = {0};
    printf("task2 tickFreq=%u, 15s window=%u ticks\r\n", (unsigned)freq, (unsigned)limit);
    for (;;)
    {
        uint32_t now = hi_get_tick();
        uint32_t elapsed = (now >= start) ? (now - start) : now;
        if (elapsed < limit)   /* 前 15s：红外寻线 */
        {
            WifiIotGpioValue l = WIFI_IOT_GPIO_VALUE0, r = WIFI_IOT_GPIO_VALUE0;
            GpioGetInputVal(GPIO_IR_L, &l);
            GpioGetInputVal(GPIO_IR_R, &r);
            printf("IR trace L=%d R=%d\r\n", (int)l, (int)r);
        }
        else                  /* 15s 后：蓝牙/UART1 收发 */
        {
            int n = UartRead(WIFI_IOT_UART_IDX_1, buf, sizeof(buf));
            if (n > 0)
            {
                buf[n] = '\0';
                printf("BLE recv:%s\r\n", buf);
                msg.Idx = 200U; msg.Buf = (char *)buf;
                osMessageQueuePut(mid_MsgQueue, &msg, 0U, 0U);
            }
        }
        osDelay(50);
    }
}

/* ---------- 任务3：消息队列（发送多条消息并依次读出） ---------- */
static void task3(void *arg)
{
    (void)arg;
    uint8_t i;
    char bufs[5][24];
    for (;;)
    {
        for (i = 0; i < 5; i++)
        {
            sprintf((char *)bufs[i], "SUM msg %d", i);
            msg.Idx = i; msg.Buf = bufs[i];
            if (osMessageQueuePut(mid_MsgQueue, &msg, 0U, 0U) == 0)
                printf("Put msg Idx=%d:%s\r\n", msg.Idx, msg.Buf);
        }
        status = osMessageQueueGet(mid_MsgQueue, &msg_rx, NULL, osWaitForever);
        if (status == osOK)
            printf("MQ Get Idx=%d:%s\r\n", msg_rx.Idx, (char *)msg_rx.Buf);
        osDelay(100);
    }
}

/* ---------- 任务入口 ---------- */
static void Sum_Experiment_First(void)
{
    WatchDogDisable();
    GpioInit();

    /* 舵机 GPIO2 复用为 GPIO + 输出 */
    IoSetFunc(WIFI_IOT_IO_NAME_GPIO_2, WIFI_IOT_IO_FUNC_GPIO_2_GPIO);
    GpioSetDir(WIFI_IOT_IO_NAME_GPIO_2, WIFI_IOT_GPIO_DIR_OUT);
    /* 超声波 GPIO7/8 */
    hi_io_set_func(GPIO_ECHO, 0);
    GpioSetDir(GPIO_ECHO, WIFI_IOT_GPIO_DIR_IN);
    GpioSetDir(GPIO_TRIG, WIFI_IOT_GPIO_DIR_OUT);
    /* 红外 GPIO13/14 输入 */
    IoSetFunc(WIFI_IOT_IO_NAME_GPIO_13, WIFI_IOT_IO_FUNC_GPIO_13_GPIO);
    IoSetFunc(WIFI_IOT_IO_NAME_GPIO_14, WIFI_IOT_IO_FUNC_GPIO_14_GPIO);
    GpioSetDir(WIFI_IOT_IO_NAME_GPIO_13, WIFI_IOT_GPIO_DIR_IN);
    GpioSetDir(WIFI_IOT_IO_NAME_GPIO_14, WIFI_IOT_GPIO_DIR_IN);
    /* UART1 */
    uart1_init();
    /* 消息队列 */
    mid_MsgQueue = osMessageQueueNew(MSGQUEUE_OBJECTS, sizeof(MSGQUEUE_OBJ_t), NULL);

    osThreadAttr_t attr;
    attr.attr_bits = 0U; attr.cb_mem = NULL; attr.cb_size = 0U; attr.stack_mem = NULL;
    attr.stack_size = 1024 * 8; attr.priority = 25;
    attr.name = "task1"; osThreadNew((osThreadFunc_t)task1, NULL, &attr);
    attr.name = "task2"; osThreadNew((osThreadFunc_t)task2, NULL, &attr);
    attr.name = "task3"; osThreadNew((osThreadFunc_t)task3, NULL, &attr);
}

/* 启动任务（添加在整个文件的最末尾） */
APP_FEATURE_INIT(Sum_Experiment_First);
