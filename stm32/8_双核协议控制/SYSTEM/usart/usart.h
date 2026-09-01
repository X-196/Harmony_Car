#ifndef __USART_H
#define __USART_H
#include "stdio.h"	
#include "sys.h" 

#define USART_REC_LEN  			6	  //协议帧长度(帧头+4数据+帧尾)
#define EN_USART1_RX 			1		//使能（1）/禁止（0）串口1接收
	  	
extern u8  USART_RX_BUF[USART_REC_LEN]; //协议帧接收缓冲
extern u8  USART_RX_COUNT;              //帧内字节计数
extern u8  CAR_buff[4];                 //解析结果: 左方向/左速度/右方向/右速度
extern volatile u8 uart_rec_flag;       //收到一帧完整数据标志
void uart_init(u32 bound);

#endif
