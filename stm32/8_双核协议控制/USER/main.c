#include "stm32f10x.h"
#include "sys.h"
#include "delay.h"
#include "motor.h"
#include "encoder.h"
#include "pid.h"
#include "colorful_led.h"
#include "usart.h"

/* QST先锋号 任务24：系统通信协议（双核综合）
 *
 * Hi3861(主核) --UART 115200-8N1--> STM32(从核, 本工程)
 *   6字节帧: 0xFC | 左方向 | 左速度(圈/s×100) | 右方向 | 右速度 | 0xFD
 *
 * 本工程职责:
 *   1. USART1 中断解析协议帧(帧头0xFC找齐到帧尾0xFD为一帧)
 *   2. System_Control(100ms): 恢复带符号目标转速 -> PID 闭环驱动电机
 *   3. 主循环 led_task: 按运动状态渲染车灯
 *        前进 -> 左右灯带常亮白(日行灯)
 *        左转 -> 左侧转向灯 400ms 闪烁(琥珀色)
 *        右转 -> 右侧转向灯 400ms 闪烁(琥珀色)
 *        后退 -> 倒车灯(左右常亮, 红边白芯)
 *        停止 -> 灯全灭
 */

#define LED_SLICE_MS   10     // 主循环节拍
#define BLINK_HALF_MS  400    // 转向灯闪烁半周期

/* 灯带布局开关: 实车若为"前后各一条"而不是"左右各一条",
 * 改为 LAYOUT_FRONT_BACK 后左转=前后带左半边闪, 右转=右半边闪 */
#define LAYOUT_LEFT_RIGHT   1   // 1=左右各一条(默认)
#define LAYOUT_FRONT_BACK   2
#define LED_LAYOUT          LAYOUT_LEFT_RIGHT

/* 左转/右转对应的灯珠区间(1~led_num) */
#if LED_LAYOUT == LAYOUT_FRONT_BACK
#define TURN_L_LED_FIRST  1
#define TURN_L_LED_LAST   3
#define TURN_R_LED_FIRST  4
#define TURN_R_LED_LAST   6
#endif

static u32 led_ms = 0;    // 灯效计时(ms)

/* 点亮一条灯带全部灯(amber=1 为琥珀色转向灯, white=1 为白色) */
static void strip_all(u8 left, u8 r, u8 g, u8 b)
{
    u8 i;
    for (i = 1; i <= led_num; i++)
    {
        if (left) L_ws2812_rgb(i, r, g, b);
        else      R_ws2812_rgb(i, r, g, b);
    }
}

#if LED_LAYOUT == LAYOUT_FRONT_BACK
/* 前后布局: 点亮指定半边(1=左半边, 0=右半边) */
static void strip_half(u8 left, u8 left_half, u8 r, u8 g, u8 b)
{
    u8 i, first = left_half ? TURN_L_LED_FIRST : TURN_R_LED_FIRST;
    u8 last  = left_half ? TURN_L_LED_LAST  : TURN_R_LED_LAST;
    for (i = 1; i <= led_num; i++)
    {
        if (i >= first && i <= last)
        {
            if (left) L_ws2812_rgb(i, r, g, b);
            else      R_ws2812_rgb(i, r, g, b);
        }
        else
        {
            if (left) L_ws2812_rgb(i, 0, 0, 0);
            else      R_ws2812_rgb(i, 0, 0, 0);
        }
    }
}
#endif

/* 车灯渲染任务: 按全局 Car_Led_State 渲染两条灯带(主循环每 10ms 调一次) */
static void led_task(void)
{
    u8 blink_on;

    led_ms += LED_SLICE_MS;
    blink_on = ((led_ms / BLINK_HALF_MS) % 2) == 0;   // 转向灯闪烁节拍

    switch (Car_Led_State)
    {
        case CAR_LED_RUN:                    // 前进: 日行灯
            strip_all(1, 255, 255, 255);
            strip_all(0, 255, 255, 255);
            break;

        case CAR_LED_LEFT:                   // 左转: 左侧转向灯闪(琥珀 255,128,0)
#if LED_LAYOUT == LAYOUT_LEFT_RIGHT
            if (blink_on) strip_all(1, 255, 128, 0);
            else          strip_all(1, 0, 0, 0);
            strip_all(0, 0, 0, 0);
#else
            if (blink_on) { strip_half(1, 1, 255, 128, 0); strip_half(0, 1, 255, 128, 0); }
            else          { strip_all(1, 0, 0, 0); strip_all(0, 0, 0, 0); }
#endif
            break;

        case CAR_LED_RIGHT:                  // 右转: 右侧转向灯闪(琥珀)
#if LED_LAYOUT == LAYOUT_LEFT_RIGHT
            strip_all(1, 0, 0, 0);
            if (blink_on) strip_all(0, 255, 128, 0);
            else          strip_all(0, 0, 0, 0);
#else
            if (blink_on) { strip_half(1, 0, 255, 128, 0); strip_half(0, 0, 255, 128, 0); }
            else          { strip_all(1, 0, 0, 0); strip_all(0, 0, 0, 0); }
#endif
            break;

        case CAR_LED_BACK:                   // 后退: 倒车灯(白芯红边)
            strip_all(1, 255, 0, 0);
            strip_all(0, 255, 0, 0);
            L_ws2812_rgb(3, 255, 255, 255);
            L_ws2812_rgb(4, 255, 255, 255);
            R_ws2812_rgb(3, 255, 255, 255);
            R_ws2812_rgb(4, 255, 255, 255);
            break;

        default:                             // 停止: 灯全灭
            strip_all(1, 0, 0, 0);
            strip_all(0, 0, 0, 0);
            break;
    }

    L_ws2812_refresh(led_num);
    R_ws2812_refresh(led_num);
}

int main(void)
{
	RCC->CSR |= 1 << 24;                  // 清除复位标志
	Stm32_Clock_Init(9);                  // 外部时钟8Mhz 9倍频 = 72MHz
	MY_NVIC_PriorityGroupConfig(2);       // 中断优先级分组
	uart_init(115200);                    // 串口初始化(与Hi3861 UART2 通信, 115200-8-N-1)
	JTAG_Set(JTAG_SWD_DISABLE);           // 关闭JTAG接口
	JTAG_Set(SWD_ENABLE);                 // 打开SWD接口

	Encoder_Init_TIM2();                  // 初始化左轮编码器 TIM2 (PA0/PA1)
	Encoder_Init_TIM3();                  // 初始化右轮编码器 TIM3 (PA6/PA7)
	PWM_Init(7199, 9);                    // 定时器初始化 频率1000Hz（TIM4 电机PWM）
	colorful_led_Init();                  // 炫彩灯初始化

	// 滴答定时器：每 1ms 触发一次中断，用于 100ms 周期闭环控制
	SysTick_Config(72000000 / 1000);

	printf("QST -- Task24 dual-core protocol (Hi3861 -> STM32)\r\n");

	/** 主循环：车灯渲染(每10ms一帧)；电机闭环由 SysTick 100ms 调度 System_Control() **/
	while (1)
	{
		led_task();
		delay_ms(LED_SLICE_MS);
	}
}
