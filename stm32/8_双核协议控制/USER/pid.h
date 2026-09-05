#ifndef __PID_H
#define __PID_H
#include "stm32f10x.h"

/* 车灯状态(由 System_Control 根据协议帧更新, 主循环 led_task 渲染) */
#define CAR_LED_STOP  0   // 停止: 灯全灭
#define CAR_LED_RUN   1   // 前进: 日行灯(左右常亮白)
#define CAR_LED_LEFT  2   // 左转: 左侧转向灯闪(琥珀)
#define CAR_LED_RIGHT 3   // 右转: 右侧转向灯闪(琥珀)
#define CAR_LED_BACK  4   // 后退: 倒车灯(左右常亮白+红边)

extern volatile u8 Car_Led_State;   // 当前车灯状态
extern volatile u32 Car_Led_Tick;   // 状态时间片计数

// 速度闭环：增量式 PI 控制器（A=左轮，B=右轮）
int Incremental_PI_A(int Encoders_A, int Target_A);   // 左轮 PID
int Incremental_PI_B(int Encoders_B, int Target_B);   // 右轮 PID

// 把目标转速(圈/s)换算成编码器目标脉冲数（每 OverflowTime ms 的量）
int Rs_To_CPR(float rads);

// 编码器累计里程（脉冲, 带符号; 供主核算 s2=odl+odr、theta=odr-odl）
extern volatile int32_t odo_left;
extern volatile int32_t odo_right;

// 系统闭环控制函数（消费协议帧 → 读编码器 → 算目标 → PID → Set_Pwm）
void System_Control(void);

#endif
