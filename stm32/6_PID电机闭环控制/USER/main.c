#include "stm32f10x.h"
#include "sys.h"
#include "delay.h"
#include "motor.h"
#include "encoder.h"
#include "pid.h"
#include "colorful_led.h"

/* QST先锋号 任务23：PID 电机速度闭环控制 */
int main(void)
{
	RCC->CSR |= 1 << 24;                  // 清除复位标志
	Stm32_Clock_Init(9);                  // 外部时钟8Mhz 9倍频 = 72MHz
	MY_NVIC_PriorityGroupConfig(2);       // 中断优先级分组
	uart_init(115200);                    // 串口初始化 115200
	JTAG_Set(JTAG_SWD_DISABLE);           // 关闭JTAG接口
	JTAG_Set(SWD_ENABLE);                 // 打开SWD接口

	Encoder_Init_TIM2();                  // 初始化左轮编码器 TIM2 (PA0/PA1)
	Encoder_Init_TIM3();                  // 初始化右轮编码器 TIM3 (PA6/PA7)
	PWM_Init(7199, 9);                    // 定时器初始化 频率1000Hz（TIM4 电机PWM）
	colorful_led_Init();                  // 炫彩灯初始化

	// 滴答定时器：每 1ms 触发一次中断，用于 100ms 周期闭环控制
	SysTick_Config(72000000 / 1000);

	printf("QST青软 -- PID speed closed loop\r\n");

	/** 主要程序：闭环控制由 SysTick_Handler 每 100ms 调度 System_Control() **/
	while (1)
	{
		delay_ms(100);
	}
}
