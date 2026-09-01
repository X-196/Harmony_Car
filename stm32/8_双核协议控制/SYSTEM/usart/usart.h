#ifndef __USART_H
#define __USART_H
#include "stdio.h"
#include "sys.h"

#define USART_REC_LEN 10
#define EN_USART1_RX 1

extern u8 USART_RX_BUF[USART_REC_LEN];
extern u8 USART_RX_COUNT;
extern volatile int16_t CAR_buff[2];
extern volatile u8 uart_rec_flag;
extern volatile u8 uart_frame_seq;
void uart_init(u32 bound);

#endif
