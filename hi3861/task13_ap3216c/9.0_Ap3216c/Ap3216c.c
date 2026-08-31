#include <stdio.h>
#include <unistd.h>
#include "ohos_init.h"
#include "cmsis_os2.h"
#include "hal_bsp_ap3216c.h"
#include "hal_bsp_ssd1306.h"

/*
 * 任务13：OpenHarmony 系统驱动实验 —— AP3216C 传感器采集光照强度
 * I2C0（GPIO9=SCL、GPIO10=SDA）读取 AP3216C 三合一传感器，
 * 周期打印 人体红外(ir) / 光强(als) / 接近(ps) 三路数据（ASCII 输出，串口不乱码），
 * 并把三路数据显示到 SSD1306 OLED（同一条 I2C0 总线，地址 0x78 不冲突）。
 */

/* 任务函数：读 AP3216C 三合一传感器数据 + OLED 显示
 *  ir  人体红外传感器
 *  als 光强传感器
 *  ps  接近传感器
 */
static void Task1(void)
{
  char line[26];

  AP3216C_Init();   /* 三合一传感器初始化 */
  SSD1306_Init();   /* OLED 显示屏初始化（同一 I2C0 总线） */
  SSD1306_CLS();    /* 清屏 */

  printf("i2c_ap3216c_demo()!\r\n");
  uint16_t ir = 0, als = 0, ps = 0;
  while (1)
  {
    AP3216C_ReadData(&ir, &als, &ps);
    /* 串口打印（ASCII，避免串口助手 GBK/UTF-8 乱码） */
    printf("ir = %d    als = %d    ps = %d\r\n", ir, als, ps);

    /* OLED 显示：8x16 大字体铺满整屏（128x64，字符 8x16 → 16 列 × 4 行）
     * 行号 y 取 0~3：标题一行 + 三路数据各占一行（y*2 为页号） */
    snprintf(line, sizeof(line), "AP3216C LIGHT");
    SSD1306_ShowStr(16, 0, (uint8_t *)line, 16);
    snprintf(line, sizeof(line), "ir  %5u", ir);
    SSD1306_ShowStr(0, 1, (uint8_t *)line, 16);
    snprintf(line, sizeof(line), "als %5u", als);
    SSD1306_ShowStr(0, 2, (uint8_t *)line, 16);
    snprintf(line, sizeof(line), "ps  %5u", ps);
    SSD1306_ShowStr(0, 3, (uint8_t *)line, 16);

    sleep(1); /* 1s */
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
  options.stack_size = 1024;
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
