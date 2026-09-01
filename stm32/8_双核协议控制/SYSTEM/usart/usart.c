#include "sys.h"
#include "usart.h"
#include <string.h>

#if 1
#pragma import(__use_no_semihosting)
struct __FILE { int handle; };
FILE __stdout;
_sys_exit(int x) { x = x; }
int fputc(int ch, FILE *f)
{
    while ((USART1->SR & 0X40) == 0);
    USART1->DR = (u8)ch;
    return ch;
}
#endif

#if EN_USART1_RX
u8 USART_RX_BUF[USART_REC_LEN];
u8 USART_RX_COUNT = 0;
volatile int16_t CAR_buff[2];       // 左/右轮有符号速度(×100)
volatile u8 uart_rec_flag = 0;
volatile u8 uart_frame_seq = 0;

void uart_init(u32 bound)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA, ENABLE);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 3;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 3;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    USART_InitStructure.USART_BaudRate = bound;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART1, &USART_InitStructure);
    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);
    USART_Cmd(USART1, ENABLE);
}

/* V2: FC | 02 | 0A | L int16 LE | R int16 LE | seq | XOR | FD */
void USART1_IRQHandler(void)
{
    u8 res;
    u8 checksum;

    if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET)
    {
        res = USART_ReceiveData(USART1);
        if (USART_RX_COUNT == 0)
        {
            if (res == 0xFC)
                USART_RX_BUF[USART_RX_COUNT++] = res;
        }
        else if (USART_RX_COUNT < USART_REC_LEN)
        {
            /* 已经锁定帧头后，帧内的0xFC只能是数据，不能重新同步 */
            USART_RX_BUF[USART_RX_COUNT++] = res;
        }

        if (USART_RX_COUNT == USART_REC_LEN)
        {
            checksum = USART_RX_BUF[1] ^ USART_RX_BUF[2] ^ USART_RX_BUF[3] ^
                       USART_RX_BUF[4] ^ USART_RX_BUF[5] ^ USART_RX_BUF[6] ^
                       USART_RX_BUF[7];
            if (USART_RX_BUF[0] == 0xFC && USART_RX_BUF[1] == 0x02 &&
                USART_RX_BUF[2] == 0x0A && USART_RX_BUF[9] == 0xFD &&
                USART_RX_BUF[8] == checksum)
            {
                CAR_buff[0] = (int16_t)((u16)USART_RX_BUF[3] | ((u16)USART_RX_BUF[4] << 8));
                CAR_buff[1] = (int16_t)((u16)USART_RX_BUF[5] | ((u16)USART_RX_BUF[6] << 8));
                uart_frame_seq = USART_RX_BUF[7];
                uart_rec_flag = 1;
            }
            USART_RX_COUNT = 0;
            memset(USART_RX_BUF, 0, USART_REC_LEN);
        }
        USART_ClearFlag(USART1, USART_FLAG_RXNE);
    }
}
#endif
