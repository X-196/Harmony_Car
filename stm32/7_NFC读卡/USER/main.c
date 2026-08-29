#include "stm32f10x.h"
#include "sys.h"
#include "delay.h"
#include "usart.h"        // 串口1: 调试打印(UartAssist, COM9 115200)
#include "usart2.h"       // 串口2: 与 NFC 模块 PN532 通信(PA2=TX, PA3=RX)
#include "nfc.h"
#include "colorful_led.h"

int main(void)
{
	Stm32_Clock_Init(9);				//外部时钟8Mhz 9倍频  8*9= 72mhz倍频72mhz
	MY_NVIC_PriorityGroupConfig(2);	//=====中断优先级分组
	uart_init(115200);	            //=====串口1初始化为115200, 调试打印
	JTAG_Set(JTAG_SWD_DISABLE);     //=====关闭JTAG接口
	JTAG_Set(SWD_ENABLE);           //=====打开SWD接口 可以利用主板的SWD接口调试

	colorful_led_Init();            //=====炫彩灯初始化
	R_led_CLC();                    //=====上电车灯全灭, 等待刷卡触发
	usart2_init(115200);            //=====串口2初始化, 连接 NFC 模块 PN532(默认115200)

	printf("QST Task20: STM32 NFC read card\r\n");
	USART2_SendFrame((u8*)NFC_WakeUp, sizeof(NFC_WakeUp));   //=====发指令唤醒 NFC 模块
	/**主要程序**/
	while(1)
	{
		NFC_Handler();              //=====循环寻卡: 收到正确卡号则翻转车灯(亮<->灭)

		if(NFC_WakeUp_Ok == 0)      //=====唤醒应答丢失时, 周期重发唤醒指令
		{
			USART2_SendFrame((u8*)NFC_WakeUp, sizeof(NFC_WakeUp));
			delay_ms(500);
		}
	}
}

