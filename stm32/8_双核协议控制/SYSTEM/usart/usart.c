#include "sys.h"
#include "usart.h"	  
#include <string.h>

//////////////////////////////////////////////////////////////////
//加入以下代码,支持printf函数,而不需要选择use MicroLIB	  
#if 1
#pragma import(__use_no_semihosting)             
//标准库需要的支持函数                 
struct __FILE 
{ 
	int handle;

}; 

FILE __stdout;       
//定义_sys_exit()以避免使用半主机模式    
_sys_exit(int x) 
{ 
	x = x; 
}  
//重定义fputc函数 
int fputc(int ch, FILE *f)
{      
	while((USART1->SR&0X40)==0);//循环发送,直到发送完毕   
    USART1->DR = (u8) ch;      
	return ch;
}
#endif 

#if EN_USART1_RX   //如果使能了接收

u8 USART_RX_BUF[USART_REC_LEN];     //协议帧接收缓冲(6字节帧)
u8 USART_RX_COUNT = 0;              //帧内字节计数
u8 CAR_buff[4];                     //解析出的有效数据: [0]左轮方向 [1]左轮速度 [2]右轮方向 [3]右轮速度
volatile u8 uart_rec_flag = 0;      //收到一帧完整数据的标志

/* 双核通信协议(Hi3861 -> STM32, 115200-8-N-1):
 *   Byte1  0xFC   帧头
 *   Byte2  0/1    左轮方向(0正转/前进, 1反转/后退)
 *   Byte3  0~150  左轮速度(实际转速x100, 单位圈/s)
 *   Byte4  0/1    右轮方向
 *   Byte5  0~150  右轮速度
 *   Byte6  0xFD   帧尾
 */
void uart_init(u32 bound){
  //GPIO端口设置
  GPIO_InitTypeDef GPIO_InitStructure;
	USART_InitTypeDef USART_InitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;
	 
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1|RCC_APB2Periph_GPIOA, ENABLE);	//使能USART1，GPIOA时钟
  
	//USART1_TX   GPIOA.9
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9; //PA.9
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;	//复用推挽输出
  GPIO_Init(GPIOA, &GPIO_InitStructure);//初始化GPIOA.9
   
  //USART1_RX	  GPIOA.10初始化
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;//PA10
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;//浮空输入
  GPIO_Init(GPIOA, &GPIO_InitStructure);//初始化GPIOA.10  

  //Usart1 NVIC 配置
  NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority=3 ;//抢占优先级3
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 3;		//子优先级3
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;			//IRQ通道使能
	NVIC_Init(&NVIC_InitStructure);	//根据指定的参数初始化VIC寄存器
  
   //USART 初始化设置

	USART_InitStructure.USART_BaudRate = bound;//串口波特率
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;//字长为8位数据格式
	USART_InitStructure.USART_StopBits = USART_StopBits_1;//一个停止位
	USART_InitStructure.USART_Parity = USART_Parity_No;//无奇偶校验位
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;//无硬件数据流控制
	USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;//收发模式

  USART_Init(USART1, &USART_InitStructure); //初始化串口1
  USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);//开启串口接受中断
  USART_Cmd(USART1, ENABLE);                    //使能串口1 

}

void USART1_IRQHandler(void)                	//串口1中断服务程序: 逐字节解析双核协议帧
{
	u8 Res;

	if(USART_GetITStatus(USART1, USART_IT_RXNE) != RESET)   //接收中断
	{
		Res = USART_ReceiveData(USART1);   //读取接收到的数据
		USART_RX_BUF[USART_RX_COUNT] = Res;

		if(USART_RX_BUF[0] == 0xFC)       //寻找帧头
			USART_RX_COUNT++;
		else
			USART_RX_COUNT = 0;           //不是帧头, 重新找

		if(USART_RX_BUF[5] == 0xFD)       //得到帧尾 -> 一帧数据接收完成
		{
			USART_RX_COUNT = 0;
			CAR_buff[0] = USART_RX_BUF[1];   //左轮方向
			CAR_buff[1] = USART_RX_BUF[2];   //左轮速度
			CAR_buff[2] = USART_RX_BUF[3];   //右轮方向
			CAR_buff[3] = USART_RX_BUF[4];   //右轮速度
			memset(USART_RX_BUF, 0, 6);
			uart_rec_flag = 1;                //串口帧标志, System_Control 中消费
		}
		USART_ClearFlag(USART1, USART_FLAG_RXNE);   //清除中断标志位
	}
}

#endif	
