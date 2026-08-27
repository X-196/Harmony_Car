#include "stm32f10x.h"
#include "sys.h"
#include "motor.h"
#include "encoder.h"
#include "colorful_led.h"

// 全局测速变量（在 SysTick_Handler 中更新，主循环读取）
volatile int32_t left_cnt  = 0;   // left encoder CNT (TIM2)
volatile int32_t right_cnt = 0;   // right encoder CNT (TIM3)
volatile uint8_t sample_flag = 0; // set every 20ms

#define SAMPLE_MS         20        // sample period 20ms
#define WHEEL_CIRC_CM     1425      // wheel circumference 14.25cm (x100, in 1/100 cm)
#define ENC_PULSE_PER_REV 2800      // 实测：轮子转一圈编码器脉冲数 2800（替代理论推算 1440）

int32_t last_l = 0;
int32_t last_r = 0;

// 简单的软件延时（毫秒）
static void delay_ms_custom(unsigned int ms)
{
	unsigned int i;
	for (i = 0; i < ms; i++) {
		volatile unsigned int n = 7200;   // ~1ms @72MHz, 粗略
		while (n--) { }
	}
}

int main(void)
{
	int32_t dl, dr;
	long rev_l_x100, rev_r_x100;
	long v_l_x10, v_r_x10;
	int step;                    // 0=前进, 1=停, 2=后退, 3=停

	RCC->CSR |= 1 << 24;
	Stm32_Clock_Init(9);
	MY_NVIC_PriorityGroupConfig(2);
	uart_init(115200);
	JTAG_Set(JTAG_SWD_DISABLE);
	JTAG_Set(SWD_ENABLE);

	PWM_Init(7199, 9);           // 电机 PWM 初始化（TIM4，1000Hz）
	Encoder_Init_TIM2();         // 左轮编码器 TIM2 (PA0/PA1)
	Encoder_Init_TIM3();         // 右轮编码器 TIM3 (PA6/PA7)
	colorful_led_Init();
	SysTick_Config(72000000 / 1000);   // 1ms 中断，用于 20ms 采样

	printf("QST Timer Encoder Test\r\n");

	step = 0;
	while (1)
	{
		// 电机节奏：前进 2s -> 停 1s -> 后退 2s -> 停 1s
		if (step == 0)      { Set_Pwm(1800, 1800); }
		else if (step == 1) { Set_Pwm(0, 0); }
		else if (step == 2) { Set_Pwm(-1800, -1800); }
		else                { Set_Pwm(0, 0); }

		if (sample_flag)
		{
			sample_flag = 0;

			dl = left_cnt - last_l;
			last_l = left_cnt;
			dr = right_cnt - last_r;
			last_r = right_cnt;

			rev_l_x100 = (long)dl * 50 * 100 / ENC_PULSE_PER_REV;
			rev_r_x100 = (long)dr * 50 * 100 / ENC_PULSE_PER_REV;
			v_l_x10 = rev_l_x100 * WHEEL_CIRC_CM / 100 / 10;
			v_r_x10 = rev_r_x100 * WHEEL_CIRC_CM / 100 / 10;

			printf("L cnt=%d R cnt=%d | L %ld.%02ld rev/s %ld.%01ld cm/s | R %ld.%02ld rev/s %ld.%01ld cm/s\r\n",
				(int)left_cnt, (int)right_cnt,
				rev_l_x100/100, rev_l_x100%100,
				v_l_x10/10, v_l_x10%10,
				rev_r_x100/100, rev_r_x100%100,
				v_r_x10/10, v_r_x10%10);
		}

		// 每 20ms 走一步（约 50Hz 步进），100步=2s
		delay_ms_custom(20);
		if (++step > 3) step = 0;
	}
}
