#ifndef __USART2_H
#define __USART2_H
#include "stm32f10x.h"

// USART2 与 NFC(PN532) 通信：PA2=TX(去 NFC_RX)，PA3=RX(接 NFC_TX)，115200
#define USART2_REC_LEN  200   // 接收缓冲长度

extern u8  USART2_RX_BUF[USART2_REC_LEN]; // 接收缓冲
extern u16 USART2_RX_STA;                 // 接收状态/长度

void usart2_init(u32 bound);   // 初始化 USART2（NFC 通信口）
void USART2_SendFrame(u8 *buf, u16 len); // 发送一帧数据到 NFC 模块

#endif
