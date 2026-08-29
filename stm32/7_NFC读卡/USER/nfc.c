#include "nfc.h"
#include "usart2.h"
#include "usart.h"      // 串口1(调试打印)
#include "colorful_led.h"
#include "delay.h"
#include <string.h>

// 唤醒指令（PN532 唤醒包）
const u8 NFC_WakeUp[] = {
	0x55,0x55,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0xFF,0x03,0xFD,0xD4,0x14,0x01,0x17,0x00
};
// 寻卡指令（查询一张标签）
const u8 NFC_SearchCard[] = {0x00,0x00,0xFF,0x04,0xFC,0xD4,0x4A,0x01,0x00,0xE1,0x00};

// 三张预设卡的卡号 UID（即手机 NFC 标签助手读到的 4 字节卡号, 换卡改这里）
static const u8 Card_UID[3][4] = {
	{0x04, 0x63, 0x1F, 0x49},   // 卡1(本次实测读到的卡片)
	{0x50, 0x84, 0xFC, 0x23},   // 卡2(参考示例)
	{0x40, 0x74, 0x80, 0x23},   // 卡3(参考示例)
};

u8 NFC_WakeUp_Ok    = 0;   // NFC 唤醒标志
u8 NFC_find_Card    = 0;   // NFC 找到一张卡
u8 NFC_sendcmd_find = 1;   // NFC 收到卡帧头等待
u8 led_flag         = 0;   // 流水灯反转标志

u8 NFC_Card_Index = 0;     // 本次判定到的卡号: 1/2/3=预设卡, 0=未知卡
u8 NFC_Card_ID[4] = {0};   // 卡号 ID 尾 4 字节(与手机标签助手读到的卡号对应)

static u8  rx_buf[64];     // 串口2接收临时帧缓冲
static u16 rx_cnt  = 0;    // 当前已收字节数

// 把收到的数据用十六进制打印到串口1（UartAssist 调试用）
void put_HEX_to_UART1(u8 *buf, u16 len)
{
	u16 i;
	for (i = 0; i < len; i++) {
		printf("%02X ", buf[i]);
	}
	printf("\r\n");
}

// 在收到的帧里查找预设卡号 UID（不依赖帧内固定位置, 应答对齐漂移也能命中）
static u8 NFC_MatchCard(u8 *buf, u16 len)
{
	u8 i, j;
	for (i = 0; i < 3; i++) {
		for (j = 0; j + 3 < len; j++) {
			if (buf[j]   == Card_UID[i][0] && buf[j+1] == Card_UID[i][1] &&
				buf[j+2] == Card_UID[i][2] && buf[j+3] == Card_UID[i][3]) {
				return i + 1;   // 命中, 返回卡号 1/2/3
			}
		}
	}
	return 0;
}

// 串口2每收到一个字节调用一次（在 USART2_IRQHandler 里）
void NFC_user_Handler(u8 ch)
{
	if (NFC_WakeUp_Ok == 0)
	{
		// 唤醒阶段：应收到固定 15 字节唤醒应答
		rx_buf[rx_cnt++] = ch;
		if (rx_cnt >= 15)
		{
			// 判断唤醒应答（先导 00 00 FF 00 FF 00 + 应答 D5 15）
			if (rx_buf[4] == 0xFF && rx_buf[11] == 0xD5 && rx_buf[12] == 0x15)
			{
				NFC_WakeUp_Ok = 1;   // 唤醒成功,进入寻卡流程
				printf("NFC wake up OK\r\n");
			}
			rx_cnt = 0;
		}
	}
		else
		{
			// 唤醒成功，进入寻卡流程：应收到 25 字节应答帧
			rx_buf[rx_cnt++] = ch;
			if (rx_cnt >= 25)
			{
				// 打印收到的帧（串口显示 NFC 读写过程）
				put_HEX_to_UART1(rx_buf, 25);

				// 在整帧中查找预设卡号 UID，命中则置找到卡标志
				NFC_Card_Index = NFC_MatchCard(rx_buf, 25);
				if (NFC_Card_Index != 0)
				{
					NFC_Card_ID[0] = Card_UID[NFC_Card_Index-1][0];
					NFC_Card_ID[1] = Card_UID[NFC_Card_Index-1][1];
					NFC_Card_ID[2] = Card_UID[NFC_Card_Index-1][2];
					NFC_Card_ID[3] = Card_UID[NFC_Card_Index-1][3];
					NFC_find_Card = 1;
				}
				rx_cnt = 0;
			}
		}
}

// 循环寻卡（放到主循环 while() 里）
void NFC_Handler(void)
{
	if (NFC_WakeUp_Ok)   // 已唤醒
	{
		if (NFC_find_Card == 1)
		{
			FoundCard_Handler();   // 找到一张卡
		}
		else if (NFC_find_Card == 0 && NFC_sendcmd_find == 1)
		{
			// 未找到卡，发指令寻卡
			USART2_SendFrame((u8*)NFC_SearchCard, sizeof(NFC_SearchCard));
			NFC_sendcmd_find = 0;
			delay_ms(200);
			NFC_sendcmd_find = 1;   // 重新允许发寻卡指令(持续轮询)
		}
	}
}

// 后灯带整条按 (r,g,b) 点亮
static void R_led_all_color(u8 r, u8 g, u8 b)
{
	u8 i;
	for (i = 1; i <= led_num; i++) {
		R_ws2812_rgb(i, r, g, b);
	}
	R_ws2812_refresh(led_num);
}

// 找到卡时执行的功能函数（学生自定义: 亮<->灭交替, 不同卡不同颜色）
void FoundCard_Handler(void)
{
	NFC_find_Card = 0;           // 清除标识

	if (NFC_Card_Index == 1) { printf("Found Card 1: %02X:%02X:%02X:%02X\r\n",
		NFC_Card_ID[0], NFC_Card_ID[1], NFC_Card_ID[2], NFC_Card_ID[3]); }
	else if (NFC_Card_Index == 2) { printf("Found Card 2: %02X:%02X:%02X:%02X\r\n",
		NFC_Card_ID[0], NFC_Card_ID[1], NFC_Card_ID[2], NFC_Card_ID[3]); }
	else { printf("Found Card 3: %02X:%02X:%02X:%02X\r\n",
		NFC_Card_ID[0], NFC_Card_ID[1], NFC_Card_ID[2], NFC_Card_ID[3]); }

	if (led_flag == 0)           // 反转车灯（流水灯）
	{
		led_flag = 1;
		if      (NFC_Card_Index == 1) R_led_all_color(255, 0, 0);    // 卡1 红色
		else if (NFC_Card_Index == 2) R_led_all_color(0, 255, 0);    // 卡2 绿色
		else                          R_led_all_color(0, 80, 255);   // 卡3 蓝色
	}
	else
	{
		led_flag = 0;
		R_led_CLC();                 // 熄灭
	}
	NFC_sendcmd_find = 1;        // 重新允许发寻卡指令
	delay_ms(200);
}
