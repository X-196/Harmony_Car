#include "usart2.h"
#include "nfc.h"

u8  USART2_RX_BUF[USART2_REC_LEN]; // 接收缓冲
u16 USART2_RX_STA = 0;             // 接收状态/长度

// 初始化 USART2（连接 NFC 模块 PN532），PA2=TX、PA3=RX
void usart2_init(u32 bound)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	USART_InitTypeDef USART_InitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;

	// 使能 USART2 与 GPIOA 时钟（USART2 走 APB1）
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

	// USART2_TX = PA2（复用推挽输出，去 NFC_RX）
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	// USART2_RX = PA3（浮空输入，接 NFC_TX）
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	// USART2 中断
	NVIC_InitStructure.NVIC_IRQChannel = USART2_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 3;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 3;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);

	// USART2 参数：115200, 8N1
	USART_InitStructure.USART_BaudRate = bound;
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;
	USART_InitStructure.USART_StopBits = USART_StopBits_1;
	USART_InitStructure.USART_Parity = USART_Parity_No;
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
	USART_Init(USART2, &USART_InitStructure);

	USART_ITConfig(USART2, USART_IT_RXNE, ENABLE); // 使能接收中断
	USART_Cmd(USART2, ENABLE);
}

// 发送一帧数据到 NFC 模块
void USART2_SendFrame(u8 *buf, u16 len)
{
	u16 i;
	for (i = 0; i < len; i++) {
		while ((USART2->SR & 0X40) == 0);  // 等待发送完成
		USART2->DR = buf[i];
	}
}

// 串口2中断服务程序: 每收到一个 NFC 应答字节交给判定函数
void USART2_IRQHandler(void)
{
	u8 Res;
	if (USART_GetITStatus(USART2, USART_IT_RXNE) != RESET)   // 接收中断
	{
		Res = USART_ReceiveData(USART2);                     // 读取收到的字节
		NFC_user_Handler(Res);
	}
}
