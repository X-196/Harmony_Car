#include <stdio.h>
#include <unistd.h>
#include "ohos_init.h"
#include "cmsis_os2.h"
#include "hal_bsp_sht20.h"

/* 定义信号量 */
osSemaphoreId_t sem1;

/* 前置声明 */
static void thread1(void);
static void thread2(void);
static void thread3(void);

/* 任务创建 */
static void i2c_sht20_demo(void)
{
  osThreadAttr_t attr;
  attr.attr_bits = 0U;
  attr.cb_mem = NULL;
  attr.cb_size = 0U;
  attr.stack_mem = NULL;
  attr.stack_size = 1024 * 4;

  attr.name = "thread1";
  attr.priority = 25;
  if (osThreadNew((osThreadFunc_t)thread1, NULL, &attr) == NULL)
  {
    printf("Falied to create thread1!\n");
  }
  attr.name = "thread2";
  attr.priority = 25;
  if (osThreadNew((osThreadFunc_t)thread2, NULL, &attr) == NULL)
  {
    printf("Falied to create thread2!\n");
  }
  attr.name = "thread3";
  attr.priority = 25;
  if (osThreadNew((osThreadFunc_t)thread3, NULL, &attr) == NULL)
  {
    printf("Falied to create thread3!\n");
  }
  /* 创建信号量：初始值为0，最大值为4 */
  sem1 = osSemaphoreNew(4, 0, NULL);
  if (sem1 == NULL)
  {
    printf("Falied to create Semaphore1!\n");
  }
}

/* 任务1：周期释放信号量，使 thread2 / thread3 同步 */
static void thread1(void)
{
  while (1)
  {
    /* 释放两次，让 thread2 和 thread3 都能同步执行 */
    osSemaphoreRelease(sem1);
    /* 若只释放一次，则 thread2 和 thread3 会交替运行 */
    osSemaphoreRelease(sem1);
    printf("\n");
    printf("Thread1 release sem!\n");
    osDelay(300);   /* 延时3s */
  }
}

/* 任务2：IIC 读 SHT20 温湿度 */
static void thread2(void)
{
  float temperature = 0, humidity = 0;
  printf("i2c_sht20_demo()!\n");
  SHT20_Init();   /* SHT20 初始化 */
  while (1)
  {
    /* 等待 sem1 信号量 */
    osSemaphoreAcquire(sem1, osWaitForever);
    SHT20_ReadData(&temperature, &humidity);
    printf("temperature = %.2f      humidity = %.2f\r\n", temperature, humidity);
    printf("Thread2 get sem!\n");
    osDelay(1);   /* 延时10ms */
  }
}

/* 任务3：同步打印 */
static void thread3(void)
{
  while (1)
  {
    /* 等待 sem1 信号量 */
    osSemaphoreAcquire(sem1, osWaitForever);
    printf("Thread3 get sem!\n");
    osDelay(1);
  }
}

/* 启动任务（添加在整个文件的最末尾） */
APP_FEATURE_INIT(i2c_sht20_demo);
