#include "stm32f10x.h"
#include "sys.h"

int main(void)
  { 
		Stm32_Clock_Init(9);						//外部时钟8Mhz 9倍频  8*9= 72mhz倍频72mhz
		MY_NVIC_PriorityGroupConfig(2);	//=====中断优先级分组		
		uart_init(115200);	            //=====串口初始化为115200
		JTAG_Set(JTAG_SWD_DISABLE);     //=====关闭JTAG接口
		JTAG_Set(SWD_ENABLE);           //=====打开SWD接口 可以利用主板的SWD接口调试

		colorful_led_Init();            //=====炫彩灯初始化
		led_effect_init();              //=====灯效驱动初始化(非阻塞,可随时切换)

		printf("QST青软\r\n");
		/**主要程序**/
	while(1)
	{
		//切换灯效: 收到指令即实时切换, 无需复位
		if(USART_RX_STA != 0)
		{
			led_effect_set_mode(USART_RX_STA);  //1=HELLO 2=ALL LIGHT 3=FASTROUND 4=SLOWROUND 5=FASTBL 6=SLOWBL
			USART_RX_STA = 0;
		}

		//逐帧驱动一帧灯效(非阻塞), 帧间隔由下方 delay_ms 控制
		led_effect_run();

		delay_ms(10);           //约10ms一帧; 期间串口中断仍会置位USART_RX_STA
	}
}
	

