#ifndef __MOTOR_H
#define __MOTOR_H
#include "stm32f10x.h"
#include "sys.h"

// 电机方向控制引脚（L9110S）：AIN/BIN 决定正反转方向
#define AIN  PBout(14)   // B14，方向引脚（对应 PWMA）
#define BIN  PBout(13)   // B13，方向引脚（对应 PWMB）

// PWM 占空比比较寄存器（TIM4，PB6/PB7 复用输出）
#define PWMA TIM4->CCR1  // 左轮（TIM4_CH1 = PB6）
#define PWMB TIM4->CCR2  // 右轮（TIM4_CH2 = PB7）

void Motor_Init(void);               // 初始化电机方向引脚 PB13/PB14
void PWM_Init(u16 arr, u16 psc);     // 初始化 TIM4 PWM：频率 = 72M/(psc+1)/(arr+1)
void Set_Pwm(int moto1, int moto2);  // 设置左右轮速度：>0 前进，<0 后退，值≤arr

#endif
