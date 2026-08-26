#include "motor.h"

// 初始化电机方向引脚 PB13/PB14（推挽输出）
void Motor_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);   // 使能 PB 端口时钟
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_14 | GPIO_Pin_13; // PB14/PB13
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;         // 推挽输出
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOB, &GPIO_InitStructure);
	AIN = 0;
	BIN = 0;
}

// 初始化 TIM4 PWM：arr=重装载值(ARR)，psc=预分频(PSC)
// PWM频率 = 72M / (psc+1) / (arr+1)
void PWM_Init(u16 arr, u16 psc)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	TIM_TimeBaseInitTypeDef  TIM_TimeBaseStructure;
	TIM_OCInitTypeDef  TIM_OCInitStructure;

	Motor_Init();                                                  // 先初始化方向引脚
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4, ENABLE);          // 使能 TIM4 时钟
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);         // 使能 GPIOB 时钟

	// PB6/PB7 复用推挽输出，输出 PWM（PB6=TIM4_CH1，PB7=TIM4_CH2）
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;               // 复用推挽输出
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOB, &GPIO_InitStructure);

	TIM_TimeBaseStructure.TIM_Period = arr;                        // 自动重装载值
	TIM_TimeBaseStructure.TIM_Prescaler = psc;                     // 预分频系数
	TIM_TimeBaseStructure.TIM_ClockDivision = 0;
	TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;    // 向上计数
	TIM_TimeBaseInit(TIM4, &TIM_TimeBaseStructure);

	TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;              // PWM 模式1
	TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;  // 比较输出使能
	TIM_OCInitStructure.TIM_Pulse = 0;                             // 初始占空比0
	TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;      // 输出极性高
	TIM_OC1Init(TIM4, &TIM_OCInitStructure);                       // CH1
	TIM_OC2Init(TIM4, &TIM_OCInitStructure);                       // CH2

	TIM_CtrlPWMOutputs(TIM4, ENABLE);                              // MOE 主输出使能
	TIM_OC1PreloadConfig(TIM4, TIM_OCPreload_Enable);              // CH1 预装载
	TIM_OC2PreloadConfig(TIM4, TIM_OCPreload_Enable);              // CH2 预装载
	TIM_ARRPreloadConfig(TIM4, ENABLE);                            // ARR 预装载
	TIM_Cmd(TIM4, ENABLE);                                         // 使能 TIM4
}

// 求绝对值
u32 myabs(long int a)
{
	u32 temp;
	if (a < 0) temp = -a;
	else       temp = a;
	return temp;
}

// 设置左右轮速度：moto>0 前进，moto<0 后退；值不能超过 ARR(7199)
void Set_Pwm(int moto1, int moto2)
{
	if (moto2 >= 0) { AIN = 0; PWMA = myabs(moto2); }        // 右轮前进
	else            { AIN = 1; PWMA = 7199 - myabs(moto2); } // 右轮后退
	if (moto1 >= 0) { BIN = 0; PWMB = myabs(moto1); }        // 左轮前进
	else            { BIN = 1; PWMB = 7199 - myabs(moto1); } // 左轮后退
}
