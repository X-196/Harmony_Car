#include "hal_bsp_ap3216c.h"
#include "wifiiot_errno.h"
#include "wifiiot_gpio.h"
#include "wifiiot_gpio_ex.h"
#include "wifiiot_i2c.h"
#include "wifiiot_i2c_ex.h"
#include <unistd.h>
#include <stdio.h>

/*****************************************************************************
 * AP3216C 三合一环境传感器驱动（OpenHarmony WiFi-IoT I2C 接口）
 * 总线：I2C0（GPIO9 = SCL、GPIO10 = SDA），从机地址 0x3C（7 位地址 0x1E << 1）
 * 器件：ALS 数字环境光 + PS 接近传感器 + IR 红外 LED
 *****************************************************************************/

/* 向 AP3216C 的指定寄存器写一个字节 */
static uint32_t AP3216C_WriteByte(uint8_t regAddr, uint8_t byte)
{
  uint8_t buffer[] = {regAddr, byte};
  WifiIotI2cData i2cData = {0};
  i2cData.sendBuf = buffer;
  i2cData.sendLen = 2;

  return I2cWrite(AP3216C_I2C_IDX, AP3216C_I2C_ADDR, &i2cData);
}

/* 从 AP3216C 的指定寄存器读一个字节：先写寄存器地址，再重复起始读 */
static uint32_t AP3216C_ReadByte(uint8_t regAddr, uint8_t *byte)
{
  uint8_t reg = regAddr;
  WifiIotI2cData i2cData = {0};
  i2cData.sendBuf = &reg;
  i2cData.sendLen = 1;
  i2cData.receiveBuf = byte;
  i2cData.receiveLen = 1;

  return I2cWriteread(AP3216C_I2C_IDX, AP3216C_I2C_ADDR, &i2cData);
}

/* 读 ir / als / ps 三路 16 位数据 */
uint32_t AP3216C_ReadData(uint16_t *ir, uint16_t *als, uint16_t *ps)
{
  uint32_t result;
  uint8_t buffer[6] = {0};

  /* IR：低 0x0A 高 0x0B（高字节 bit15 为数据有效位，需剔除） */
  result = AP3216C_ReadByte(AP3216C_IR_L_REG, &buffer[0]);
  if (result != 0)
  {
    printf("I2C AP3216C read ir_l status = 0x%x!!!\r\n", result);
    return result;
  }
  result = AP3216C_ReadByte(AP3216C_IR_H_REG, &buffer[1]);
  if (result != 0)
  {
    printf("I2C AP3216C read ir_h status = 0x%x!!!\r\n", result);
    return result;
  }
  *ir = (uint16_t)(((buffer[1] & 0x03) << 8) | buffer[0]); /* bit15/14 为标志位，数据 10 位 */

  /* ALS：低 0x0C 高 0x0D（16 位有效） */
  result = AP3216C_ReadByte(AP3216C_ALS_L_REG, &buffer[2]);
  if (result != 0)
  {
    printf("I2C AP3216C read als_l status = 0x%x!!!\r\n", result);
    return result;
  }
  result = AP3216C_ReadByte(AP3216C_ALS_H_REG, &buffer[3]);
  if (result != 0)
  {
    printf("I2C AP3216C read als_h status = 0x%x!!!\r\n", result);
    return result;
  }
  *als = (uint16_t)((buffer[3] << 8) | buffer[2]);

  /* PS：低 0x0E（bit0 为接近标志） 高 0x0F（10 位数据取高字节部分） */
  result = AP3216C_ReadByte(AP3216C_PS_L_REG, &buffer[4]);
  if (result != 0)
  {
    printf("I2C AP3216C read ps_l status = 0x%x!!!\r\n", result);
    return result;
  }
  result = AP3216C_ReadByte(AP3216C_PS_H_REG, &buffer[5]);
  if (result != 0)
  {
    printf("I2C AP3216C read ps_h status = 0x%x!!!\r\n", result);
    return result;
  }
  *ps = (uint16_t)((buffer[5] << 2) | (buffer[4] & 0x03)); /* PS 数据 10 位：高字节 8 位 + 低字节 2 位 */

  return 0;
}

/* 传感器 AP3216C 的初始化 */
uint32_t AP3216C_Init(void)
{
  uint32_t result;

  GpioInit();

  /* GPIO_10 复用为 I2C0_SDA */
  IoSetFunc(WIFI_IOT_IO_NAME_GPIO_10, WIFI_IOT_IO_FUNC_GPIO_10_I2C0_SDA);

  /* GPIO_9 复用为 I2C0_SCL */
  IoSetFunc(WIFI_IOT_IO_NAME_GPIO_9, WIFI_IOT_IO_FUNC_GPIO_9_I2C0_SCL);

  /* baudrate: 400kbps */
  result = I2cInit(WIFI_IOT_I2C_IDX_0, 400000);
  if (result != 0)
  {
    printf("I2C AP3216C Init status is 0x%x!!!\r\n", result);
    return result;
  }
  result = I2cSetBaudrate(WIFI_IOT_I2C_IDX_0, 400000);
  if (result != 0)
  {
    printf("I2C AP3216C set baudrate status is 0x%x!!!\r\n", result);
    return result;
  }

  /* 软复位，复位后需要等待约 10ms 完成初始化 */
  result = AP3216C_WriteByte(AP3216C_SYS_CONFIG, AP3216C_SYS_SW_RESET);
  if (result != 0)
  {
    printf("I2C AP3216C sw reset status = 0x%x!!!\r\n", result);
    return result;
  }
  usleep(10 * 1000);

  /* 配置为 ALS+PS+IR 连续测量模式 */
  result = AP3216C_WriteByte(AP3216C_SYS_CONFIG, AP3216C_SYS_ALS_PS_CONT);
  if (result != 0)
  {
    printf("I2C AP3216C set config status = 0x%x!!!\r\n", result);
    return result;
  }

  usleep(100 * 1000); /* 等待第一次测量完成 */
  printf("I2C AP3216C Init is succeeded!!!\r\n");
  return 0;
}
