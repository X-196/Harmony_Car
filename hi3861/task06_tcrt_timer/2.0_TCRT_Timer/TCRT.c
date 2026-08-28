#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "ohos_init.h"
#include "cmsis_os2.h"
#include "wifiiot_gpio.h"
#include "wifiiot_gpio_ex.h"
#include "wifiiot_watchdog.h"
#include "hi_io.h"

/* 任务6：红外对管（TCRT5000）收发 + 软件定时器
 * 原理图：双路红外循迹传感器（TCRT5000 + LM393），输出 TC_OUT_L -> IO13、TC_OUT_R -> IO14；
 *        红外发射管经 3.3VD + 120R 恒流供电（硬件常亮），软件读取 LM393 数字输出。
 * 核心：用软件定时器(osTimerNew)周期性读取左右红外信号并打印。
 */

#define TCRT_L_GPIO 13   // 红外左(TC_OUT_L) 接 IO13
#define TCRT_R_GPIO 14   // 红外右(TC_OUT_R) 接 IO14

osTimerId_t tcrt_timer_id;   // 软件定时器

/* 定时器回调：读取左右红外对管信号并打印 */
static void TcrTimerCallback(void *arg)
{
    (void)arg;
    WifiIotGpioValue l = WIFI_IOT_GPIO_VALUE0;
    WifiIotGpioValue r = WIFI_IOT_GPIO_VALUE0;
    GpioGetInputVal(TCRT_L_GPIO, &l);
    GpioGetInputVal(TCRT_R_GPIO, &r);
    // LM393 输出：检测到反射(黑线/障碍)为低电平，无障碍为高电平
    printf("IR L=%d R=%d  %s\r\n",
           (int)l, (int)r,
           (l == WIFI_IOT_GPIO_VALUE0 && r == WIFI_IOT_GPIO_VALUE0) ? "both reflect" :
           ((l == WIFI_IOT_GPIO_VALUE0) ? "left reflect" :
            (r == WIFI_IOT_GPIO_VALUE0) ? "right reflect" : "none/clear"));
}

/* 任务入口：初始化 GPIO + 创建软件定时器 */
static void TCRT(void)
{
    WatchDogDisable();   // 关闭看门狗
    GpioInit();

    // 配置 GPIO13/GPIO14 为输入（红外传感器数字输出）
    IoSetFunc(WIFI_IOT_IO_NAME_GPIO_13, WIFI_IOT_IO_FUNC_GPIO_13_GPIO);
    IoSetFunc(WIFI_IOT_IO_NAME_GPIO_14, WIFI_IOT_IO_FUNC_GPIO_14_GPIO);
    GpioSetDir(WIFI_IOT_IO_NAME_GPIO_13, WIFI_IOT_GPIO_DIR_IN);
    GpioSetDir(WIFI_IOT_IO_NAME_GPIO_14, WIFI_IOT_GPIO_DIR_IN);

    // 创建软件定时器：每 500ms(50 ticks，tick频率100Hz) 读一次红外
    tcrt_timer_id = osTimerNew((osTimerFunc_t)TcrTimerCallback, osTimerPeriodic, NULL, NULL);
    if (tcrt_timer_id == NULL)
    {
        printf("Falied to create timer!\r\n");
    }
    osTimerStart(tcrt_timer_id, 50);
}

/* 启动任务（添加在整个文件的最末尾） */
APP_FEATURE_INIT(TCRT);
