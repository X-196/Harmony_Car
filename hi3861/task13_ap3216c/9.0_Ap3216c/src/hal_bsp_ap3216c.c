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
 * 说明：读寄存器采用「写寄存器地址 → I2cRead」两段传输（与官方 supportPack 一致，
 *       Hi3861 WiFi-IoT 的 I2cRead 发起始位时不含 ACK 的问题对本器件同样适用）
 *****************************************************************************/

/* 系统配置寄存器（0x00）取值 */
#define AP3216C_SYS_ALS_PS_ONCE 0x03 /* ALS+PS+IR 单次测量 */

/* 向从机设备发送数据（单字节） */
static uint32_t AP3216C_SendByte(uint8_t byte)
{
  WifiIotI2cData i2cData = {0};
  i2cData.sendBuf = &byte;
  i2cData.sendLen = 1;

  return I2cWrite(AP3216C_I2C_IDX, AP3216C_I2C_ADDR, &i2cData);
}

/* 读从机设备数据 */
static uint32_t AP3216C_RecvData(uint8_t *data, size_t size)
{
  WifiIotI2cData i2cData = {0};
  i2cData.receiveBuf = data;
  i2cData.receiveLen = size;

  return I2cRead(AP3216C_I2C_IDX, AP3216C_I2C_ADDR, &i2cData);
}

/* 向寄存器中写数据：regAddr + byte */
static uint32_t AP3216C_WriteReg(uint8_t regAddr, uint8_t byte)
{
  uint8_t buffer[] = {regAddr, byte};
  WifiIotI2cData i2cData = {0};
  i2cData.sendBuf = buffer;
  i2cData.sendLen = 2;

  return I2cWrite(AP3216C_I2C_IDX, AP3216C_I2C_ADDR, &i2cData);
}

/* 读寄存器中的数据：先写寄存器地址，再读 1 字节（两段传输，与 supportPack 一致） */
static uint32_t AP3216C_ReadRegByte(uint8_t regAddr, uint8_t *byte)
{
  uint32_t result;
  uint8_t buffer[2] = {0};

  /* 写命令 */
  result = AP3216C_SendByte(regAddr);
  if (result != 0)
  {
    printf("I2C AP3216C write reg(0x%02x) status = 0x%x!!!\r\n", regAddr, result);
    return result;
  }

  /* 读数据 */
  result = AP3216C_RecvData(buffer, 1);
  if (result != 0)
  {
    printf("I2C AP3216C read reg(0x%02x) status = 0x%x!!!\r\n", regAddr, result);
    return result;
  }
  *byte = buffer[0];

  return 0;
}

/* 读 ir / als / ps 三路数据 */
uint32_t AP3216C_ReadData(uint16_t *ir, uint16_t *als, uint16_t *ps)
{
  uint32_t result;
  uint8_t data_H = 0, data_L = 0;

  /* IR 数据（10 位）：低 0x0A 高 0x0B；低字节 bit7 为数据无效标志 */
  result = AP3216C_ReadRegByte(AP3216C_IR_L_REG, &data_L);
  if (result != 0)
    return result;
  result = AP3216C_ReadRegByte(AP3216C_IR_H_REG, &data_H);
  if (result != 0)
    return result;
  if (data_L & 0x80) /* IR_OF 为 1，数据无效 */
    *ir = 0;
  else
    *ir = ((uint16_t)data_H << 2) | (data_L & 0x03);

  /* ALS 数据（16 位）：低 0x0C 高 0x0D */
  result = AP3216C_ReadRegByte(AP3216C_ALS_L_REG, &data_L);
  if (result != 0)
    return result;
  result = AP3216C_ReadRegByte(AP3216C_ALS_H_REG, &data_H);
  if (result != 0)
    return result;
  *als = ((uint16_t)data_H << 8) | data_L;

  /* PS 数据（10 位）：低 0x0E 高 0x0F；低字节 bit6 为数据无效标志 */
  result = AP3216C_ReadRegByte(AP3216C_PS_L_REG, &data_L);
  if (result != 0)
    return result;
  result = AP3216C_ReadRegByte(AP3216C_PS_H_REG, &data_H);
  if (result != 0)
    return result;
  if (data_L & 0x40) /* 数据无效 */
    *ps = 0;
  else
    *ps = ((uint16_t)(data_H & 0x3F) << 4) | (data_L & 0x0F);

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

  /* 复位芯片（写 0x04 后等待 5ms） */
  result = AP3216C_WriteReg(AP3216C_SYS_CONFIG, 0x04);
  if (result != 0)
  {
    printf("I2C AP3216C reset status = 0x%x!!!\r\n", result);
    return result;
  }
  usleep(5000);

  /* 开启 ALS+PS+IR（0x03 单次测量模式，实测板上有效；supportPack 同款取值） */
  result = AP3216C_WriteReg(AP3216C_SYS_CONFIG, AP3216C_SYS_ALS_PS_ONCE);
  if (result != 0)
  {
    printf("I2C AP3216C set mode status = 0x%x!!!\r\n", result);
    return result;
  }

  usleep(100 * 1000); /* 等待第一次测量完成 */
  printf("I2C AP3216C Init is succeeded!!!\r\n");
  return 0;
}
