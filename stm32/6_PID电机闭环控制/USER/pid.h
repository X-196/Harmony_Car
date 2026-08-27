#ifndef __PID_H
#define __PID_H
#include "stm32f10x.h"

// 速度闭环：增量式 PI 控制器（A=左轮，B=右轮）
int Incremental_PI_A(int Encoders_A, int Target_A);   // 左轮 PID
int Incremental_PI_B(int Encoders_B, int Target_B);   // 右轮 PID

// 把目标转速(圈/s)换算成编码器目标脉冲数（每 OverflowTime ms 的量）
int Rs_To_CPR(float rads);

// 系统闭环控制函数（读编码器 → 算目标 → PID → Set_Pwm）
void System_Control(void);

#endif
