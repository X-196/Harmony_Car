#include "sys.h"
#include "usart.h"	  

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

u8 USART_RX_BUF[USART_REC_LEN];     //接收缓冲,最大USART_REC_LEN个字节.
u8 USART_RX_STA=0;       //接收状态标记	  
u8 count=0;
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
	USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;	//收发模式

  USART_Init(USART1, &USART_InitStructure); //初始化串口1
  USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);//开启串口接受中断
  USART_Cmd(USART1, ENABLE);                    //使能串口1 

}

/**************************************************************************
按字符累积匹配指令, 无需换行, 输入完整即触发
返回: 0=尚未累积完整(继续等待), 1..6=已匹配到对应指令(取缓冲)
**************************************************************************/
u8 usart_cmd_match(void)
{
	u8 i;
	const char *cmd[6]	= {"HELLO","ALL LIGHT","FASTROUND","SLOWROUND","FASTBL","SLOWBL"};
	u8        sta[6]	= {1,2,3,4,5,6};
	u8        len[6]	= {5,9,9,9,6,6};

	//1. 是否完整匹配某条指令
	for(i=0;i<6;i++)
	{
		if(count==len[i] && strncmp((char*)USART_RX_BUF,cmd[i],len[i])==0)
		{
			count=0;		//清零,准备下一条指令
			return sta[i];
		}
	}
	//2. 是否仍是某条指令的前缀(未输入完)
	for(i=0;i<6;i++)
	{
		if(count<=len[i] && strncmp((char*)USART_RX_BUF,cmd[i],count)==0)
			return 0;		//等待更多字符
	}
	//3. 不符合任何指令/前缀, 丢弃重来
	count=0;
	return 0;
}

void USART1_IRQHandler(void)                	//串口1中断服务程序
	{
	u8 Res;
	u8 m;

	if(USART_GetITStatus(USART1, USART_IT_RXNE) != RESET)   //判断中断类型
		{
			Res =USART_ReceiveData(USART1);	//读取接收到的数据
			if(count < USART_REC_LEN-1)     //防止缓冲溢出
				USART_RX_BUF[count++]=Res;  //存字符并计数
			USART_RX_BUF[count]='\0';       //字符串结尾

			//指令按字符累积匹配,无需换行,输入完整即触发
			m = usart_cmd_match();
			if(m) USART_RX_STA = m;         //匹配到指令: 置状态, 主循环消费
    }
    USART_ClearFlag(USART1, USART_FLAG_RXNE); //清除接收表示位
}

#endif	

