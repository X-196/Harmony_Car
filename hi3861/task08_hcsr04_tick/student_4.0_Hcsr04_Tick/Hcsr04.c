#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include "ohos_init.h"
#include "cmsis_os2.h"
#include "wifiiot_gpio.h"
#include "wifiiot_gpio_ex.h"
#include "wifiiot_watchdog.h"
#include "hi_io.h"
#include "hi_time.h"

/* 学生完成版：
 *   实现创建2个软件定时器：
 *     定时器1（周期 3s）：触发一次超声波测距并打印距离；
 *     定时器2（周期 1s）：打印当前系统 tick 值。
 *   （参考版用 while(1) + osDelay 循环，本版按要求改用软件定时器）
 */

//HC-SR04 超声波测距模块通过GPIO7和8连接到3861
#define GPIO_8 8
#define GPIO_7 7
#define GPIO_FUNC 0

osTimerId_t timer_measure_id;   // 定时器1：控制超声波测距（3秒）
osTimerId_t timer_tick_id;      // 定时器2：打印当前tick值（1秒）

/*测距功能实现*/
float GetDistance  (void)
{
    static unsigned long start_time = 0, time = 0;
    float distance = 0.0;
    WifiIotGpioValue value = WIFI_IOT_GPIO_VALUE0;
    unsigned int flag = 0;

    hi_io_set_func(GPIO_8, GPIO_FUNC);

    GpioSetDir(GPIO_8, WIFI_IOT_GPIO_DIR_IN);//GPIO_8设置为输入引脚
    GpioSetDir(GPIO_7, WIFI_IOT_GPIO_DIR_OUT);//GPIO_7设置为输出引脚

    //GPIO_7输出一个脉冲触发信号到超声波测距模块  至少10us
    GpioSetOutputVal(GPIO_7, WIFI_IOT_GPIO_VALUE1);
    hi_udelay(20);
    GpioSetOutputVal(GPIO_7, WIFI_IOT_GPIO_VALUE0);

    //超声波测距模块接收到GPIO_7输出的脉冲触发信号后,模块输出回响信号(高电平)到GPIO_8
    while (1) {
        GpioGetInputVal(GPIO_8, &value);

        //测量回响信号(高电平)时间
        if ( value == WIFI_IOT_GPIO_VALUE1 && flag == 0) {
            start_time = hi_get_us();
            flag = 1;
        }
        if (value == WIFI_IOT_GPIO_VALUE0 && flag == 1) {
            time = hi_get_us() - start_time;
            start_time = 0;
            break;
        }
    }

    //距离=高电平时间*0.034 / 2
    distance = time * 0.034 / 2;
    return distance;
}

/* 定时器1回调：每3秒执行一次超声波测距并打印距离 */
static void TimerMeasureCallback(void *arg)
{
    (void)arg;
    float distance = GetDistance();
    printf("distance is %.1f (cm)\r\n", distance);
}

/* 定时器2回调：每秒打印当前系统tick值 */
static void TimerTickCallback(void *arg)
{
    (void)arg;
    printf("tick value is %u\r\n", (unsigned int)hi_get_tick());
}

/*任务入口*/
static void Hcsr04(void)
{
    WatchDogDisable();  //关闭看门狗

    //创建软件定时器1：周期 3s（300 ticks，系统tick频率100Hz，即1tick=10ms）
    timer_measure_id = osTimerNew((osTimerFunc_t)TimerMeasureCallback, osTimerPeriodic, NULL, NULL);
    if (timer_measure_id == NULL) {
        printf("Failed to create measure timer!\n");
    }
    osTimerStart(timer_measure_id, 300);

    //创建软件定时器2：周期 1s（100 ticks）打印当前tick值
    timer_tick_id = osTimerNew((osTimerFunc_t)TimerTickCallback, osTimerPeriodic, NULL, NULL);
    if (timer_tick_id == NULL) {
        printf("Failed to create tick timer!\n");
    }
    osTimerStart(timer_tick_id, 100);
}

/*启动任务（添加在整个文件的最末尾）*/
APP_FEATURE_INIT(Hcsr04);//任务启动
