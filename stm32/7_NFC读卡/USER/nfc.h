#ifndef __NFC_H
#define __NFC_H
#include "stm32f10x.h"

// NFC 模块唤醒指令（PN532 唤醒包, 24 字节）
extern const u8 NFC_WakeUp[24];
// NFC 寻卡/查询标签指令（11 字节）
extern const u8 NFC_SearchCard[11];

// 状态标志
extern u8 NFC_WakeUp_Ok;     // NFC 唤醒成功标志
extern u8 NFC_find_Card;     // 找到一张卡标志
extern u8 NFC_sendcmd_find;  // 是否允许发寻卡指令
extern u8 led_flag;          // 流水灯反转标志

// 卡号判定结果
extern u8 NFC_Card_Index;    // 判定到的卡号: 1/2/3=预设卡, 0=未知卡
extern u8 NFC_Card_ID[4];    // 卡号 ID 尾 4 字节

// 接收缓冲（USART2 收到的数据）
extern u8 USART2_RX_BUF[];   // 定义于 usart2.c
extern u16 USART2_RX_STA;

// 功能函数
void NFC_user_Handler(u8 ch);          // NFC 收数据判定（在串口2中断里调用）
void NFC_Handler(void);                // 循环寻卡
void FoundCard_Handler(void);          // 找到卡时执行的功能函数
void put_HEX_to_UART1(u8 *buf, u16 len); // 把 NFC 收到的帧打印到串口1(调试)

#endif
