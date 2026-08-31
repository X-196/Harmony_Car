#ifndef __HAL_BSP_AP3216C_H__
#define __HAL_BSP_AP3216C_H__

#include <stdint.h>
#include <stddef.h>

/*****************************************************************************
 * AP3216C 三合一环境光/接近/红外传感器 I2C 地址与寄存器定义
 * 器件 7 位地址 0x1E，OpenHarmony WiFi-IoT I2C 接口需左移 1 位（读写位）= 0x3C
 ****************************************************************************/
#define AP3216C_I2C_IDX      0        /* 模块使用的 I2C 总线号（I2C0：GPIO9=SCL、GPIO10=SDA） */
#define AP3216C_I2C_ADDR     0x3C     /* 器件的 I2C 从机地址（含读写位，0x1E << 1） */

/* 系统配置寄存器（0x00）工作模式 */
#define AP3216C_SYS_CONFIG  0x00      /* 系统配置寄存器 */
#define AP3216C_SYS_PM_OFF  0x00      /* 掉电模式 */
#define AP3216C_SYS_ALS_ONCE 0x01     /* ALS 单次测量 */
#define AP3216C_SYS_PS_ONCE  0x02     /* PS+IR 单次测量 */
#define AP3216C_SYS_ALS_PS_ONCE 0x03  /* ALS+PS+IR 单次测量 */
#define AP3216C_SYS_ALS_CONT 0x04     /* ALS 连续测量 */
#define AP3216C_SYS_PS_CONT  0x05     /* PS+IR 连续测量 */
#define AP3216C_SYS_ALS_PS_CONT 0x06  /* ALS+PS+IR 连续测量 */
#define AP3216C_SYS_SW_RESET 0x07     /* 软复位 */

/* 数据寄存器 */
#define AP3216C_IR_L_REG     0x0A     /* IR 数据低字节 */
#define AP3216C_IR_H_REG     0x0B     /* IR 数据高字节（bit15=数据有效位） */
#define AP3216C_ALS_L_REG    0x0C     /* ALS 数据低字节 */
#define AP3216C_ALS_H_REG    0x0D     /* ALS 数据高字节 */
#define AP3216C_PS_L_REG     0x0E     /* PS 数据低字节（bit0=目标接近标志） */
#define AP3216C_PS_H_REG     0x0F     /* PS 数据高字节 */

/**
 * @brief AP3216C 读取三合一传感器的 ir / als / ps 数据
 * @param ir  人体红外传感器（IR LED）数据
 * @param als 光强传感器（ALS）数据
 * @param ps  接近传感器（PS）数据
 * @return Returns {@link IOT_SUCCESS} 成功;
 *         Returns {@link IOT_FAILURE} 失败.
 */
uint32_t AP3216C_ReadData(uint16_t *ir, uint16_t *als, uint16_t *ps);

/**
 * @brief AP3216C 初始化（GPIO 复用 + I2C0 初始化 + 软复位 + 连续测量模式）
 * @return Returns {@link IOT_SUCCESS} 成功;
 *         Returns {@link IOT_FAILURE} 失败.
 */
uint32_t AP3216C_Init(void);

#endif /* !__HAL_BSP_AP3216C_H__ */
