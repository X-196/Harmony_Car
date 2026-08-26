#include "stm32f10x.h"
#include "sys.h"
#include "delay.h"
#include "motor.h"
#include "colorful_led.h"

int main(void)
{
	RCC->CSR |= 1 << 24;                  // 清除复位标志
	Stm32_Clock_Init(9);                  // 外部时钟8Mhz 9倍频 = 72MHz
	delay_init();                         // 延时初始化（SysTick）
	MY_NVIC_PriorityGroupConfig(2);       // 中断优先级分组
	uart_init(115200);                    // 串口初始化 115200
	JTAG_Set(JTAG_SWD_DISABLE);           // 关闭JTAG接口
	JTAG_Set(SWD_ENABLE);                 // 打开SWD接口（可用SWD调试）

	PWM_Init(7199, 9);                    // 定时器初始化，PWM频率1000Hz
	colorful_led_Init();                  // 炫彩灯初始化

	printf("QST青软\r\n");

	while (1)
	{
		Set_Pwm(2500, 2500);              // 前进（左右轮正速度）
		delay_ms(1000);
		delay_ms(1000);                   // 前进 2 秒
		Set_Pwm(-2500, -2500);            // 后退（左右轮负速度）
		delay_ms(1000);
		delay_ms(1000);                   // 后退 2 秒
	}
}
