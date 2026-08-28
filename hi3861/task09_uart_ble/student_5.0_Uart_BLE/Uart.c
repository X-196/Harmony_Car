#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "ohos_init.h"
#include "cmsis_os2.h"
#include "wifiiot_errno.h"
#include "wifiiot_gpio.h"
#include "wifiiot_gpio_ex.h"
#include "wifiiot_uart.h"

/* 定义消息队列对象 */
typedef struct
{
  char *Buf;      /* 对象数据类型 */
  uint8_t Idx;
} MSGQUEUE_OBJ_t;
MSGQUEUE_OBJ_t msg;                     /* 发送用的结构体 */

typedef struct
{
  char *Buf;      /* 对象数据类型 */
  uint8_t Idx;
} MSGQUEUE_OBJ_t_rx;
MSGQUEUE_OBJ_t_rx msg_rx;               /* 接收用的结构体 */

osMessageQueueId_t mid_MsgQueue;        /* 消息队列id */
osStatus_t status;
#define MSGQUEUE_OBJECTS 16             /* 消息队列最大消息数 */
#define UART_TASK_STACK_SIZE 1024 * 16
#define UART_TASK_PRIO 25
#define UART_BUFF_SIZE 1000
#define MSG_NUM 5                       /* 演示：发送 5 条消息并依次读出 */
static const char *data = "Hello, QST!\r\n";

/* 前置声明 */
static void UART_Task(void);
static void thread2(void);

/* 创建任务 */
static void UART_ExampleEntry(void)
{
    /* 创建消息队列（每个消息节点大小 = 结构体大小） */
    mid_MsgQueue = osMessageQueueNew(MSGQUEUE_OBJECTS, sizeof(MSGQUEUE_OBJ_t), NULL);
    if (mid_MsgQueue == NULL)
        printf("Falied to create Message Queue!\n");

    osThreadAttr_t attr;
    attr.name = "UART_Task";
    attr.attr_bits = 0U;
    attr.cb_mem = NULL;
    attr.cb_size = 0U;
    attr.stack_mem = NULL;
    attr.stack_size = UART_TASK_STACK_SIZE;
    attr.priority = UART_TASK_PRIO;
    if (osThreadNew((osThreadFunc_t)UART_Task, NULL, &attr) == NULL)
        printf(" Falied to create UART_Task!\n");

    attr.name = "thread2";
    attr.stack_size = UART_TASK_STACK_SIZE;
    attr.priority = 25;
    if (osThreadNew((osThreadFunc_t)thread2, NULL, &attr) == NULL)
        printf("Falied to create thread2!\n");
}

/* UART 任务：串口发送 + 依次读出消息队列中的多条消息 */
static void UART_Task(void)
{
    uint32_t ret;
    GpioInit();
    /* GPIO_00 复用为 UART1_TXD */
    IoSetFunc(WIFI_IOT_IO_NAME_GPIO_0, WIFI_IOT_IO_FUNC_GPIO_0_UART1_TXD);
    /* GPIO_01 复用为 UART1_RXD */
    IoSetFunc(WIFI_IOT_IO_NAME_GPIO_1, WIFI_IOT_IO_FUNC_GPIO_1_UART1_RXD);

    WifiIotUartAttribute uart_attr = {
        .baudRate = 9600,
        .dataBits = 8,
        .stopBits = 1,
        .parity = 0,
    };
    ret = UartInit(WIFI_IOT_UART_IDX_1, &uart_attr, NULL);
    if (ret != WIFI_IOT_SUCCESS)
    {
        printf("Failed to init uart! Err code = %d\n", ret);
        return;
    }
    printf("UART Test Start\n");
    while (1)
    {
        printf("*************UART_example**************\r\n");
        /* 通过串口1发送数据 */
        UartWrite(WIFI_IOT_UART_IDX_1, (unsigned char *)data, strlen(data));

        /* 依次读出消息队列中的消息（FIFO，一条一条取出） */
        status = osMessageQueueGet(mid_MsgQueue, &msg_rx, NULL, osWaitForever);
        if (status == osOK)
            printf("Get msg Idx=%d : %s\r\n", msg_rx.Idx, (char *)msg_rx.Buf);
    }
}

/* thread2：发送多条消息到消息队列 + 读串口数据（蓝牙/上位机） */
static void thread2(void)
{
    uint8_t rt, i;
    uint8_t uart_buff[UART_BUFF_SIZE] = {0};
    uint8_t *uart_buff_ptr = uart_buff;
    char bufs[MSG_NUM][24];
    sleep(1);
    while (1)
    {
        printf("task2 running!\n");
        /* 1) 先连续放入多条消息到消息队列（不依赖串口，保证能依次读出） */
        for (i = 0; i < MSG_NUM; i++)
        {
            sprintf((char *)bufs[i], "QST msg %d", i);
            msg.Idx = i;
            msg.Buf = bufs[i];
            if (osMessageQueuePut(mid_MsgQueue, &msg, 0U, 0U) == 0)
                printf("Put msg Idx=%d : %s\r\n", msg.Idx, msg.Buf);
        }
        /* 2) 读串口1数据（蓝牙/上位机发来）；收到就放入队列 */
        rt = UartRead(WIFI_IOT_UART_IDX_1, uart_buff_ptr, UART_BUFF_SIZE);
        if (rt > 0)
        {
            uart_buff_ptr[rt] = '\0';
            printf("Uart1 read data:%s\n", uart_buff_ptr);
            msg.Idx = 100U;
            msg.Buf = (char *)uart_buff_ptr;
            if (osMessageQueuePut(mid_MsgQueue, &msg, 0U, 0U) == 0)
                printf("Put (uart) msg:%s\n", msg.Buf);
        }
        sleep(2);
    }
}

/* 启动任务（添加在整个文件的最末尾） */
APP_FEATURE_INIT(UART_ExampleEntry);
