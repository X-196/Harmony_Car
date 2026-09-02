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
 * Hi3861(主核) --GPIO11 软件UART 9600-8N1--> STM32(从核, 本工程)
 *   V2 10字节帧: 0xFC | 0x02 | 0x0A | 左轮int16LE | 右轮int16LE | seq | XOR | 0xFD
 *
 * 本工程职责:
 *   1. USART1 中断解析协议帧(帧头0xFC找齐到帧尾0xFD为一帧)
 *   2. System_Control(100ms): 恢复带符号目标转速 -> PID 闭环驱动电机
 *   3. 主循环 led_task: 按运动状态渲染车灯
 *
 * 灯带布局(实机验证): 前后各一条 WS2812 灯带(每条 6 珠)
 *   PC13(DIL) -> 前灯带   PC14(DIR) -> 后灯带
 *   前进 -> 前灯白(大灯) + 后灯红(尾灯)
 *   左转 -> 前后灯带的"左半边"各3珠闪琥珀色(转向灯)
 *   右转 -> 前后灯带的"右半边"各3珠闪琥珀色(转向灯)
 *   后退 -> 后灯带红边白芯(倒车灯), 前灯灭
 *   停止 -> 后灯带亮红(刹车灯), 前灯灭
 */

#define LED_SLICE_MS   10     // 主循环节拍(ms)
#define BLINK_HALF_MS  350    // 转向灯闪烁半周期(≈1.4Hz, 接近真车转向灯节拍)

/* 车身"左侧"对应每条灯带的哪一半: 1=低编号 1~3 珠, 0=高编号 4~6 珠
 * 前后两条带安装方向可能相反。烧录后若转向灯亮错半边:
 *   前/后只有一条亮错 -> 只把对应宏取反(1<->0)
 *   前后都亮错(亮了右侧) -> 两个宏都取反 */
#define FRONT_STRIP_LEFT_IS_LOW   0   // 前灯带: 左半边 = 4~6 号灯(实机标定: 初始 1 时左右反, 2026-09-01 翻转)
#define REAR_STRIP_LEFT_IS_LOW    0   // 后灯带: 左半边 = 4~6 号灯(实机标定: 与前带同翻)

/* 转向灯颜色: 琥珀色 */
#define AMBER_R 255
#define AMBER_G 128
#define AMBER_B 0

static u32 led_ms = 0;    // 灯效计时(ms)

/* 点亮整条灯带(front=1 前灯带/PC13, front=0 后灯带/PC14) */
static void strip_all(u8 front, u8 r, u8 g, u8 b)
{
    u8 i;
    for (i = 1; i <= led_num; i++)
    {
        if (front) L_ws2812_rgb(i, r, g, b);
        else       R_ws2812_rgb(i, r, g, b);
    }
}

/* 前后布局: 点亮指定车身侧的半条灯带(car_left=1 左半边), 其余半边灭 */
static void strip_side(u8 front, u8 car_left, u8 r, u8 g, u8 b)
{
    u8 i, on;
    u8 low_is_left = front ? FRONT_STRIP_LEFT_IS_LOW : REAR_STRIP_LEFT_IS_LOW;
    u8 is_low_half = car_left ? low_is_left : (u8)(low_is_left ? 0 : 1);

    for (i = 1; i <= led_num; i++)
    {
        on = (u8)((is_low_half && i <= 3) || (!is_low_half && i >= 4));
        if (front) L_ws2812_rgb(i, on ? r : 0, on ? g : 0, on ? b : 0);
        else       R_ws2812_rgb(i, on ? r : 0, on ? g : 0, on ? b : 0);
    }
}

/* 车灯渲染任务: 按全局 Car_Led_State 渲染两条灯带(主循环每 10ms 调一次) */
static void led_task(void)
{
    u8 blink_on;

    led_ms += LED_SLICE_MS;
    blink_on = ((led_ms / BLINK_HALF_MS) % 2) == 0;   // 转向灯闪烁节拍

    switch (Car_Led_State)
    {
        case CAR_LED_RUN:                    // 前进: 大灯 + 尾灯(暗红, 不抢转向灯)
            strip_all(1, 180, 180, 180);
            strip_all(0, 60, 0, 0);
            break;

        case CAR_LED_LEFT:                   // 左转: 前后灯带左半边闪(琥珀), 右半边灭
            if (blink_on)
            {
                strip_side(1, 1, AMBER_R, AMBER_G, AMBER_B);
                strip_side(0, 1, AMBER_R, AMBER_G, AMBER_B);
            }
            else
            {
                strip_all(1, 0, 0, 0);
                strip_all(0, 0, 0, 0);
            }
            break;

        case CAR_LED_RIGHT:                  // 右转: 前后灯带右半边闪(琥珀), 左半边灭
            if (blink_on)
            {
                strip_side(1, 0, AMBER_R, AMBER_G, AMBER_B);
                strip_side(0, 0, AMBER_R, AMBER_G, AMBER_B);
            }
            else
            {
                strip_all(1, 0, 0, 0);
                strip_all(0, 0, 0, 0);
            }
            break;

        case CAR_LED_BACK:                   // 后退: 倒车灯(后灯带红边白芯, 前灯灭)
            strip_all(1, 0, 0, 0);
            strip_all(0, 255, 0, 0);
            R_ws2812_rgb(2, 255, 255, 255);
            R_ws2812_rgb(3, 255, 255, 255);
            R_ws2812_rgb(4, 255, 255, 255);
            R_ws2812_rgb(5, 255, 255, 255);
            break;

        default:                             // 停止/刹车: 后带亮红(刹车灯), 前灯灭
            strip_all(1, 0, 0, 0);
            strip_all(0, 255, 0, 0);
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
	uart_init(9600);                      // 串口初始化(与Hi3861 GPIO11 软件UART通信, 9600-8-N-1)
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
