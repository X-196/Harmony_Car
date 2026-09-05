#include "usart.h"
#include "protocol.h"

void uart_init(u32 bound)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 |
                           RCC_APB2Periph_GPIOA, ENABLE);

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

void USART1_SendBytes(const u8 *data, u8 len)
{
    u8 i;

    if (data == 0)
    {
        return;
    }

    for (i = 0; i < len; i++)
    {
        while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET)
        {
        }
        USART_SendData(USART1, data[i]);
    }
}

u8 USART1_SendFrame(u8 cmd, u8 len, const u8 *payload)
{
    u8 frame[PROTOCOL_MAX_FRAME_SIZE];
    u8 frame_len;

    frame_len = Protocol_EncodeFrame(cmd, len, payload,
                                     frame, sizeof(frame));
    if (frame_len == 0U)
    {
        return 0;
    }

    USART1_SendBytes(frame, frame_len);
    return 1;
}

void USART1_IRQHandler(void)
{
    u8 byte;

    if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET)
    {
        byte = (u8)USART_ReceiveData(USART1);
        Protocol_ParseByte(byte);
    }
}
#include "protocol.h"

typedef enum
{
    RX_WAIT_SOF = 0,
    RX_CMD,
    RX_LEN,
    RX_PAYLOAD,
    RX_CHECK
} ProtocolRxState;

typedef struct
{
    volatile u8 ready;
    volatile u8 cmd;
    volatile u8 len;
    volatile u8 payload[PROTOCOL_MAX_PAYLOAD];
    volatile ProtocolStatus status;
} ProtocolMailbox;

static volatile ProtocolRxState rx_state = RX_WAIT_SOF;
static volatile u8 rx_cmd = 0;
static volatile u8 rx_len = 0;
static volatile u8 rx_index = 0;
static volatile u8 rx_checksum = 0;
static volatile u8 rx_idle_ms = 0;
static u8 rx_payload[PROTOCOL_MAX_PAYLOAD];
static ProtocolMailbox mailbox;

static void Protocol_ResetRx(void)
{
    rx_state = RX_WAIT_SOF;
    rx_len = 0;
    rx_index = 0;
    rx_checksum = 0;
    rx_idle_ms = 0;
}

static void Protocol_StartFrame(void)
{
    rx_state = RX_CMD;
    rx_len = 0;
    rx_index = 0;
    rx_checksum = 0;
    rx_idle_ms = 0;
}

static void Protocol_Publish(u8 cmd, u8 len, ProtocolStatus status)
{
    u8 i;
    u8 copy_len = len;

    if (copy_len > PROTOCOL_MAX_PAYLOAD)
    {
        copy_len = 0;
    }

    mailbox.cmd = cmd;
    mailbox.len = len;
    mailbox.status = status;
    for (i = 0; i < copy_len; i++)
    {
        mailbox.payload[i] = rx_payload[i];
    }
    mailbox.ready = 1;
}

static ProtocolStatus Protocol_ValidateCommand(u8 cmd, u8 len)
{
    switch (cmd)
    {
        case PROTOCOL_CMD_SET_SPEED:
            return (len == 4U) ? PROTOCOL_STATUS_OK : PROTOCOL_STATUS_BAD_LENGTH;
        case PROTOCOL_CMD_STOP:
        case PROTOCOL_CMD_PING:
        case PROTOCOL_CMD_GET_STATUS:
            return (len == 0U) ? PROTOCOL_STATUS_OK : PROTOCOL_STATUS_BAD_LENGTH;
        case PROTOCOL_CMD_ACK:
            return (len == 2U) ? PROTOCOL_STATUS_OK : PROTOCOL_STATUS_BAD_LENGTH;
        default:
            return PROTOCOL_STATUS_UNKNOWN_CMD;
    }
}

void Protocol_Init(void)
{
    mailbox.ready = 0;
    mailbox.cmd = 0;
    mailbox.len = 0;
    mailbox.status = PROTOCOL_STATUS_OK;
    Protocol_ResetRx();
}

void Protocol_ParseByte(u8 byte)
{
    ProtocolStatus status;

    /* Called from USART1 IRQ. SysTick runs at lower priority, so the parser
       state cannot be preempted by the timeout tick while this byte is handled. */
    rx_idle_ms = 0;

    switch (rx_state)
    {
        case RX_WAIT_SOF:
            if (byte == PROTOCOL_SOF)
            {
                Protocol_StartFrame();
            }
            break;

        case RX_CMD:
            rx_cmd = byte;
            rx_checksum = byte;
            rx_state = RX_LEN;
            break;

        case RX_LEN:
            rx_len = byte;
            rx_checksum = (u8)(rx_checksum + byte);
            if (rx_len > PROTOCOL_MAX_PAYLOAD)
            {
                Protocol_Publish(rx_cmd, rx_len, PROTOCOL_STATUS_BAD_LENGTH);
                Protocol_ResetRx();
                if (byte == PROTOCOL_SOF)
                {
                    Protocol_StartFrame();
                }
            }
            else if (rx_len == 0U)
            {
                rx_state = RX_CHECK;
            }
            else
            {
                rx_index = 0;
                rx_state = RX_PAYLOAD;
            }
            break;

        case RX_PAYLOAD:
            rx_payload[rx_index++] = byte;
            rx_checksum = (u8)(rx_checksum + byte);
            if (rx_index >= rx_len)
            {
                rx_state = RX_CHECK;
            }
            break;

        case RX_CHECK:
            if (byte == rx_checksum)
            {
                status = Protocol_ValidateCommand(rx_cmd, rx_len);
                Protocol_Publish(rx_cmd, rx_len, status);
                Protocol_ResetRx();
            }
            else
            {
                Protocol_Publish(rx_cmd, rx_len, PROTOCOL_STATUS_BAD_CHECKSUM);
                Protocol_ResetRx();
                if (byte == PROTOCOL_SOF)
                {
                    Protocol_StartFrame();
                }
            }
            break;

        default:
            Protocol_ResetRx();
            break;
    }
}

void Protocol_Tick1ms(void)
{
    /* SysTick has lower priority than USART1. Byte-sized state updates are
       sufficient here; never re-enable interrupts from inside an ISR. */
    if (rx_state != RX_WAIT_SOF)
    {
        if (rx_idle_ms < PROTOCOL_RX_TIMEOUT_MS)
        {
            rx_idle_ms++;
        }
        if (rx_idle_ms >= PROTOCOL_RX_TIMEOUT_MS)
        {
            Protocol_ResetRx();
        }
    }
}

u8 Protocol_TakeEvent(ProtocolEvent *event)
{
    u8 i;
    u8 ready;

    if (event == 0)
    {
        return 0;
    }

    __disable_irq();
    ready = mailbox.ready;
    if (ready != 0U)
    {
        event->cmd = mailbox.cmd;
        event->len = mailbox.len;
        event->status = mailbox.status;
        if (event->len <= PROTOCOL_MAX_PAYLOAD)
        {
            for (i = 0; i < event->len; i++)
            {
                event->payload[i] = mailbox.payload[i];
            }
        }
        mailbox.ready = 0;
    }
    __enable_irq();

    return ready;
}

u8 Protocol_Checksum(u8 cmd, u8 len, const u8 *payload)
{
    u8 i;
    u8 checksum = (u8)(cmd + len);

    for (i = 0; i < len; i++)
    {
        checksum = (u8)(checksum + payload[i]);
    }
    return checksum;
}

u8 Protocol_EncodeFrame(u8 cmd, u8 len, const u8 *payload,
                        u8 *frame, u8 capacity)
{
    u8 i;
    u8 frame_len = (u8)(len + 4U);

    if (frame == 0 || len > PROTOCOL_MAX_PAYLOAD || capacity < frame_len)
    {
        return 0;
    }
    if (len != 0U && payload == 0)
    {
        return 0;
    }

    frame[0] = PROTOCOL_SOF;
    frame[1] = cmd;
    frame[2] = len;
    for (i = 0; i < len; i++)
    {
        frame[3U + i] = payload[i];
    }
    frame[3U + len] = Protocol_Checksum(cmd, len, payload);
    return frame_len;
}
