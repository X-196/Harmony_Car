#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "ohos_init.h"
#include "cmsis_os2.h"
#include "hal_bsp_ssd1306.h"

/* 任务函数：在 OLED 上以字符形式显示"鸿蒙先锋号" */
static void Task1(void *parame)
{
  (void)parame;
  SSD1306_Init(); // OLED 显示屏初始化
  SSD1306_CLS();  // 清屏
  // 5 个字 x 24px = 120px 宽，占满整行宽度（128-120)/2 = 4 居中；第 2 页开始（垂直约占满）
  SSD1306_ShowChinese24(4, 2, (uint8_t *)"鸿蒙先锋号");
  while (1)
  {
    sleep(1); // 1 s
  }
}

/* 任务创建 */
static void i2c_sdd1306_demo(void)
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
    printf("ID = %d, Create Task1_ID is OK!", Task1_ID);
  }
}

/* 启动任务（添加在整个文件的最末尾） */
APP_FEATURE_INIT(i2c_sdd1306_demo);
