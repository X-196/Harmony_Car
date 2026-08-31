#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "ohos_init.h"
#include "cmsis_os2.h"
#include "wifiiot_gpio.h"
#include "wifiiot_gpio_ex.h"
#include "hi_io.h"
#include "hi_time.h"
#include "hal_bsp_ap3216c.h"
#include "hal_bsp_sht20.h"
#include "hal_bsp_ssd1306.h"

/*****************************************************************************
 * 任务13（综合）：OpenHarmony 系统驱动实验 —— AP3216C 光照 + 全传感器仪表盘
 *
 * 采集并显示小车全部传感器数据（OLED 6x8 小字体一屏 8 行 + 串口 ASCII）：
 *   - AP3216C 三合一（I2C0，0x3C）：ir 红外 / als 光强 / ps 接近
 *   - SHT20 温湿度（I2C0，0x80）：温度 ℃ / 湿度 %RH
 *   - HC-SR04 超声波（GPIO7=TRIG、GPIO8=ECHO）：距离 cm
 *   - TCRT5000 红外对管（GPIO13=左、GPIO14=右）：0=检测到（压黑线） 1=无
 *
 * 器件共 I2C0 总线（GPIO9=SCL、GPIO10=SDA），地址互不冲突。
 *****************************************************************************/

/* 超声波引脚 */
#define HCSR04_TRIG_GPIO 7
#define HCSR04_ECHO_GPIO 8
/* 红外对管引脚 */
#define TCRT_L_GPIO 13
#define TCRT_R_GPIO 14
/* 超声波 GPIO 复用为普通 GPIO */
#define GPIO_FUNC 0

/*---------------- 超声波测距（任务8 同款实现） ----------------*/
static float GetDistance(void)
{
  static unsigned long start_time = 0, time = 0;
  float distance = 0.0;
  WifiIotGpioValue value = WIFI_IOT_GPIO_VALUE0;
  unsigned int flag = 0;

  hi_io_set_func(HCSR04_ECHO_GPIO, GPIO_FUNC);

  GpioSetDir(HCSR04_ECHO_GPIO, WIFI_IOT_GPIO_DIR_IN);  /* ECHO 输入 */
  GpioSetDir(HCSR04_TRIG_GPIO, WIFI_IOT_GPIO_DIR_OUT); /* TRIG 输出 */

  /* TRIG 输出 >=10us 高电平触发 */
  GpioSetOutputVal(HCSR04_TRIG_GPIO, WIFI_IOT_GPIO_VALUE1);
  hi_udelay(20);
  GpioSetOutputVal(HCSR04_TRIG_GPIO, WIFI_IOT_GPIO_VALUE0);

  /* 测回响高电平时长：距离 = time * 0.034 / 2 */
  while (1)
  {
    GpioGetInputVal(HCSR04_ECHO_GPIO, &value);
    if (value == WIFI_IOT_GPIO_VALUE1 && flag == 0)
    {
      start_time = hi_get_us();
      flag = 1;
    }
    if (value == WIFI_IOT_GPIO_VALUE0 && flag == 1)
    {
      time = hi_get_us() - start_time;
      start_time = 0;
      break;
    }
  }
  distance = time * 0.034 / 2;
  return distance;
}

/*---------------- 主任务：采集全部传感器 + OLED 仪表盘 ----------------*/
static void Task1(void)
{
  char line[26];
  uint16_t ir = 0, als = 0, ps = 0;
  float temp = 0, humi = 0;
  WifiIotGpioValue l = WIFI_IOT_GPIO_VALUE0, r = WIFI_IOT_GPIO_VALUE0;

  /* I2C0 器件初始化（AP3216C / SHT20 / SSD1306 内部各自配 GPIO9/10 + I2cInit） */
  AP3216C_Init();
  SHT20_Init();
  SSD1306_Init();
  SSD1306_CLS();

  /* 超声波 + 红外对管 GPIO 初始化 */
  GpioInit();
  hi_io_set_func(HCSR04_TRIG_GPIO, GPIO_FUNC);
  hi_io_set_func(HCSR04_ECHO_GPIO, GPIO_FUNC);
  IoSetFunc(WIFI_IOT_IO_NAME_GPIO_13, WIFI_IOT_IO_FUNC_GPIO_13_GPIO);
  IoSetFunc(WIFI_IOT_IO_NAME_GPIO_14, WIFI_IOT_IO_FUNC_GPIO_14_GPIO);
  GpioSetDir(WIFI_IOT_IO_NAME_GPIO_13, WIFI_IOT_GPIO_DIR_IN);
  GpioSetDir(WIFI_IOT_IO_NAME_GPIO_14, WIFI_IOT_GPIO_DIR_IN);

  printf("sensor_dashboard demo!\r\n");

  while (1)
  {
    /* 1) AP3216C：ir / als / ps */
    AP3216C_ReadData(&ir, &als, &ps);
    /* 2) SHT20：温湿度 */
    SHT20_ReadData(&temp, &humi);
    /* 3) HC-SR04：距离 cm */
    float dist = GetDistance();
    /* 4) TCRT5000 红外对管：0=检测到（黑线），1=无 */
    GpioGetInputVal(TCRT_L_GPIO, &l);
    GpioGetInputVal(TCRT_R_GPIO, &r);

    /* 串口打印（ASCII） */
    printf("ir=%d als=%d ps=%d T=%.1fC H=%.1f%% D=%.1fcm L=%d R=%d\r\n",
           ir, als, ps, temp, humi, dist, (int)l, (int)r);

    /* OLED 仪表盘：8x16 大字体铺满整屏（128x64 → 16 列 × 4 行，每行 16px 高）
     * 行0 光强als + 红外ir；行1 接近ps + 温度T；行2 湿度H + 红外对管L/R；行3 距离cm */
    snprintf(line, sizeof(line), "A%5u I%4u", als, ir);
    SSD1306_ShowStr(0, 0, (uint8_t *)line, 16);
    snprintf(line, sizeof(line), "P%5u %2.0fC", ps, temp);
    SSD1306_ShowStr(0, 1, (uint8_t *)line, 16);
    snprintf(line, sizeof(line), "%2.0f%%H L%dR%d", humi, (int)l, (int)r);
    SSD1306_ShowStr(0, 2, (uint8_t *)line, 16);
    snprintf(line, sizeof(line), "  %4.1fcm", dist);
    SSD1306_ShowStr(0, 3, (uint8_t *)line, 16);

    sleep(1); /* 1s 刷新 */
  }
}

/* 任务创建 */
static void i2c_ap3216c_demo(void)
{
  osThreadAttr_t options;
  options.name = "thread_1";
  options.attr_bits = 0;
  options.cb_mem = NULL;
  options.cb_size = 0;
  options.stack_mem = NULL;
  options.stack_size = 1024 * 4;
  options.priority = osPriorityNormal;
  osThreadId_t Task1_ID;
  Task1_ID = osThreadNew((osThreadFunc_t)Task1, NULL, &options);
  if (Task1_ID != NULL)
  {
    printf("ID = %d, Create Task1_ID is OK!\r\n", Task1_ID);
  }
}

/* 启动任务（添加在整个文件的最末尾） */
APP_FEATURE_INIT(i2c_ap3216c_demo);
