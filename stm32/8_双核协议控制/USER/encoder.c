#include "encoder.h"
#include "stm32f10x_gpio.h"

void Encoder_Init_TIM2(void)
{
  TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
  TIM_ICInitTypeDef TIM_ICInitStructure;
  GPIO_InitTypeDef GPIO_InitStructure;
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
  GPIO_Init(GPIOA, &GPIO_InitStructure);
  TIM_TimeBaseStructInit(&TIM_TimeBaseStructure);
  TIM_TimeBaseStructure.TIM_Prescaler = 0x0;
  TIM_TimeBaseStructure.TIM_Period = ENCODER_TIM_PERIOD;
  TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
  TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
  TIM_TimeBaseInit(TIM2, &TIM_TimeBaseStructure);
  TIM_EncoderInterfaceConfig(TIM2, TIM_EncoderMode_TI12,
                             TIM_ICPolarity_Rising, TIM_ICPolarity_Rising);
  TIM_ICStructInit(&TIM_ICInitStructure);
  TIM_ICInitStructure.TIM_ICFilter = 10;
  TIM_ICInit(TIM2, &TIM_ICInitStructure);
  TIM_ClearFlag(TIM2, TIM_FLAG_Update);
  TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);
  TIM_SetCounter(TIM2, 0);
  TIM_Cmd(TIM2, ENABLE);
}

void Encoder_Init_TIM3(void)
{
  TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
  TIM_ICInitTypeDef TIM_ICInitStructure;
  GPIO_InitTypeDef GPIO_InitStructure;
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
  GPIO_Init(GPIOA, &GPIO_InitStructure);
  TIM_TimeBaseStructInit(&TIM_TimeBaseStructure);
  TIM_TimeBaseStructure.TIM_Prescaler = 0x0;
  TIM_TimeBaseStructure.TIM_Period = ENCODER_TIM_PERIOD;
  TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
  TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
  TIM_TimeBaseInit(TIM3, &TIM_TimeBaseStructure);
  TIM_EncoderInterfaceConfig(TIM3, TIM_EncoderMode_TI12,
                             TIM_ICPolarity_Rising, TIM_ICPolarity_Rising);
  TIM_ICStructInit(&TIM_ICInitStructure);
  TIM_ICInitStructure.TIM_ICFilter = 10;
  TIM_ICInit(TIM3, &TIM_ICInitStructure);
  TIM_ClearFlag(TIM3, TIM_FLAG_Update);
  TIM_ITConfig(TIM3, TIM_IT_Update, ENABLE);
  TIM_SetCounter(TIM3, 0);
  TIM_Cmd(TIM3, ENABLE);
}

/**************************************************************************
函数功能：读取指定编码器的计数值并清零（2=左轮TIM2，3=右轮TIM3）
入口参数：TIMX（2 或 3）
返回  值：计数增量（当前 CNT，读后清零）
**************************************************************************/
int Read_Encoder(unsigned char TIMX)
{
    int Encoder_Count = 0;
    if (TIMX == 2)      // 左轮：TIM2
    {
        Encoder_Count = (int)((short)TIM2->CNT);   // 当前计数值
        TIM2->CNT = 0;                             // 清零，供下次读取
    }
    else if (TIMX == 3) // 右轮：TIM3
    {
        Encoder_Count = (int)((short)TIM3->CNT);
        TIM3->CNT = 0;
    }
    return Encoder_Count;   // 返回增量脉冲数（可正可负，区分方向）
}
