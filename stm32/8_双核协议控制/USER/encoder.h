#ifndef __ENCODER_H
#define __ENCODER_H
#include "stm32f10x.h"

// 编码器自动重装载值（本实验设为 65535）
// 电机转一圈编码器采集的脉冲数 = 360 * 4 / PSC（此处 PSC=0）
#define ENCODER_TIM_PERIOD   65535

// 左右电机编码器分别接 PA0/PA1（TIM2）、PA6/PA7（TIM3）
void Encoder_Init_TIM2(void);   // 初始化 TIM2 编码器接口模式（左轮）
void Encoder_Init_TIM3(void);   // 初始化 TIM3 编码器接口模式（右轮）

// 读取指定编码器的当前计数值（2=左轮TIM2，3=右轮TIM3），读后清零
int Read_Encoder(unsigned char TIMX);

#endif
