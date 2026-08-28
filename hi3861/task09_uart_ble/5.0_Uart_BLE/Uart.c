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
MSGQUEUE_OBJ_t msg;                     /* 结构体别名命名为 msg */

typedef struct
{
  char *Buf;      /* 对象数据类型 */
  uint8_t Idx;
} MSGQUEUE_OBJ_t_rx;
MSGQUEUE_OBJ_t_rx msg_rx;               /* 结构体别名命名为 msg_rx */

osMessageQueueId_t mid_MsgQueue;        /* 消息队列id */
osStatus_t status;                      /* 创建返回参数 */
#define MSGQUEUE_OBJECTS 16             /* 消息队列对象的数量 */
#define UART_TASK_STACK_SIZE 1024 * 16  /* 任务堆栈大小 */
#define UART_TASK_PRIO 25               /* 任务优先级 */
#define UART_BUFF_SIZE 1000             /* 串口发送数据大小 */
static const char *data = "Hello, QST!\r\n"; /* 发送字符串 */

/* 创建任务 */
static void UART_ExampleEntry(void)
{
    /* 创建消息队列 */
    mid_MsgQueue = osMessageQueueNew(MSGQUEUE_OBJECTS, 100, NULL);
    if (mid_MsgQueue == NULL)
    {
      printf("Falied to create Message Queue!\n");
    }

    osThreadAttr_t attr;
    attr.name = "UART_Task";
    attr.attr_bits = 0U;
    attr.cb_mem = NULL;
    attr.cb_size = 0U;
    attr.stack_mem = NULL;
    attr.stack_size = UART_TASK_STACK_SIZE;
    attr.priority = UART_TASK_PRIO;
    if (osThreadNew((osThreadFunc_t)UART_Task, NULL, &attr) == NULL)
    {
        printf(" Falied to create UART_Task!\n");
    }
    attr.name = "thread2";
    attr.stack_size = UART_TASK_STACK_SIZE;
    attr.priority = 25;
    if (osThreadNew((osThreadFunc_t)thread2, NULL, &attr) == NULL)
    {
      printf("Falied to create thread2!\n");
    }
    attr.name = "thread3";
    attr.stack_size = 1024;
    attr.priority = 25;
    if (osThreadNew((osThreadFunc_t)thread3, NULL, &attr) == NULL)
    {
      printf("Falied to create thread3!\n");
    }
}

/* UART 任务：串口发送 + 从消息队列接收 */
static void UART_Task(void)
{
    uint32_t ret;
    GpioInit();
    /* GPIO_00 复用为 UART1_TXD */
    IoSetFunc(WIFI_IOT_IO_NAME_GPIO_0, WIFI_IOT_IO_FUNC_GPIO_0_UART1_TXD);
    /* GPIO_01 复用为 UART1_RXD */
    IoSetFunc(WIFI_IOT_IO_NAME_GPIO_1, WIFI_IOT_IO_FUNC_GPIO_1_UART1_RXD);

    WifiIotUartAttribute uart_attr = {
        .baudRate = 9600,   /* 波特率 */
        .dataBits = 8,      /* 数据位: 8bits */
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

        /* 消息队列接收 */
        status = osMessageQueueGet(mid_MsgQueue, &msg_rx, NULL, osWaitForever);
        if (status == osOK)
            printf("Message Queue id:%d, Get msg_rx:%s\n", msg_rx.Idx, msg_rx.Buf);
    }
}

/* thread2：串口接收，放入消息队列 */
void thread2(void)
{
    uint8_t rt;
    uint8_t uart_buff[UART_BUFF_SIZE] = {0};
    uint8_t *uart_buff_ptr = uart_buff;
    sleep(1);
    msg.Idx = 12U;
    while (1)
    {
        printf("任务2正在运行!\n");
        /* 通过串口1接收数据 */
        rt = UartRead(WIFI_IOT_UART_IDX_1, uart_buff_ptr, UART_BUFF_SIZE);
        printf("Uart1 read data:%s\n", uart_buff_ptr);
        uart_buff_ptr[rt] = '\0';   /* 加字符串结束符 */
        msg.Buf = (char *)uart_buff_ptr;/* 将串口接收到的数据发送到消息队列 */
        rt = osMessageQueuePut(mid_MsgQueue, &msg, 0U, 0U);
        if (rt == 0)
            printf("Message Queue Send msg:%s\n", msg.Buf);
        else
            printf("Message Queue Send msg failed");
        sleep(1);
    }
}

/* thread3 */
void thread3(void)
{
    while (1)
    {
        printf("任务3正在运行!\n");
        sleep(3);
    }
}

/* 启动任务（添加在整个文件的最末尾） */
APP_FEATURE_INIT(UART_ExampleEntry);
