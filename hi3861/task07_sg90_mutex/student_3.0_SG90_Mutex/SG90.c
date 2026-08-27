#include <stdio.h>
#include <unistd.h>
#include "ohos_init.h"
#include "cmsis_os2.h"
#include "wifiiot_gpio.h"
#include "wifiiot_gpio_ex.h"
#include "hi_io.h"
#include "hi_time.h"

osMutexId_t mutex_id;
#define GPIO2 2

/*
 * 学生需要完成内容（U+ 任务7 验收要求）：
 * 不参考源程序，用互斥锁实现同优先级的三个任务联动：
 *   任务1优先运行：串口输出1次"任务1开始运行"，舵机左转45度
 *   任务3在任务1运行3秒后再运行：串口输出2次"任务3开始运行"，舵机右转45度
 *   任务2在任务3运行后立即运行：串口输出3次"任务2开始运行"，舵机居中
 * 角度对应（绝对位置）：左转45->45度(1.0ms)、右转45->135度(2.0ms)、居中->90度(1.5ms)
 */

// 舵机控制：输出20000us周期脉冲(xus高电平, 20000-xus低电平)
void set_angle(unsigned int duty) {
    GpioSetDir(GPIO2, WIFI_IOT_GPIO_DIR_OUT);   // 设置GPIO2为输出模式
    GpioSetOutputVal(GPIO2, WIFI_IOT_GPIO_VALUE1);   // 高电平 xus
    hi_udelay(duty);
    GpioSetOutputVal(GPIO2, WIFI_IOT_GPIO_VALUE0);   // 低电平 20000-x us
    hi_udelay(20000 - duty);
}

// 左转45度：1.0ms 高电平
static void engine_left_45(void) {
    for (int i = 0; i < 10; i++) {
        set_angle(1000);
    }
}

// 居中：1.5ms 高电平
static void engine_center(void) {
    for (int i = 0; i < 10; i++) {
        set_angle(1500);
    }
}

// 右转45度：2.0ms 高电平
static void engine_right_45(void) {
    for (int i = 0; i < 10; i++) {
        set_angle(2000);
    }
}

// 任务1：优先运行，输出1次"任务1开始运行"，舵机左转45度
static void thread1(void) {
    osDelay(100U);   // 延时，等待互斥锁初始化
    while (1) {
        osMutexAcquire(mutex_id, osWaitForever);   // 申请互斥锁，独占舵机
        printf("任务1开始运行\r\n");               // 输出1次
        engine_left_45();                          // 舵机左转45度
        osDelay(300U);                             // 任务1运行3秒再释放，供任务3等待
        osMutexRelease(mutex_id);                  // 释放互斥锁
    }
}

// 任务3：在任务1运行3秒后再运行，输出2次"任务3开始运行"，舵机右转45度
static void thread3(void) {
    osDelay(400U);   // 让任务1先运行(100+300)后再进入
    while (1) {
        osMutexAcquire(mutex_id, osWaitForever);
        printf("任务3开始运行\r\n");               // 输出第1次
        printf("任务3开始运行\r\n");               // 输出第2次
        engine_right_45();                         // 舵机右转45度
        osDelay(300U);
        osMutexRelease(mutex_id);
    }
}

// 任务2：在任务3运行后立即运行，输出3次"任务2开始运行"，舵机居中
static void thread2(void) {
    osDelay(500U);   // 任务3先运行后再进入
    while (1) {
        osMutexAcquire(mutex_id, osWaitForever);
        printf("任务2开始运行\r\n");               // 输出第1次
        printf("任务2开始运行\r\n");               // 输出第2次
        printf("任务2开始运行\r\n");               // 输出第3次
        engine_center();                           // 舵机居中(90度)
        osDelay(300U);
        osMutexRelease(mutex_id);
    }
}

// 任务创建：三个任务同优先级，创建互斥锁
static void SG90(void) {
    GpioInit();    // 初始化GPIO
    IoSetFunc(WIFI_IOT_IO_NAME_GPIO_2, WIFI_IOT_IO_FUNC_GPIO_2_GPIO);   // 设置GPIO模式
    GpioSetDir(WIFI_IOT_IO_NAME_GPIO_2, WIFI_IOT_GPIO_DIR_OUT);         // 设置为输出模式

    osThreadAttr_t attr;
    attr.attr_bits = 0U;
    attr.cb_mem = NULL;
    attr.cb_size = 0U;
    attr.stack_mem = NULL;
    attr.stack_size = 1024 * 4;
    attr.name = "thread1";
    attr.priority = 25;   // 三个任务同优先级
    if (osThreadNew((osThreadFunc_t)thread1, NULL, &attr) == NULL) {
        printf("Falied to create thread1!\n");
    }
    attr.name = "thread2";
    attr.priority = 25;   // 同优先级
    if (osThreadNew((osThreadFunc_t)thread2, NULL, &attr) == NULL) {
        printf("Falied to create thread2!\n");
    }
    attr.name = "thread3";
    attr.priority = 25;   // 同优先级
    if (osThreadNew((osThreadFunc_t)thread3, NULL, &attr) == NULL) {
        printf("Falied to create thread3!\n");
    }
    mutex_id = osMutexNew(NULL);   // 创建互斥锁
    if (mutex_id == NULL) {
        printf("Falied to create Mutex!\n");
    }
}

APP_FEATURE_INIT(SG90);   // 任务启动
