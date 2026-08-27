#include "pid.h"
#include "encoder.h"
#include "motor.h"
#include <stdio.h>

/* 电机 A=左轮，B=右轮 */
int L_coder, R_coder;         // 编码器实测值
int Motor_A, Motor_B;         // 电机 PWM 变量
int OverflowTime = 100;       // 闭环周期(ms)，与 SysTick 采样周期一致

/**************************************************************************
函数功能：增量式 PI 控制器 A（左轮）
入口参数：编码器测量值，目标速度
返回  值：电机 PWM
说明：增量式 PI：pwm += Kp*e(k) + Kd*[e(k)-e(k-1)]
      （讲解说明：速度闭环只用 PD/PI，此处按讲解用 Kp 比例 + Kd 微分）
**************************************************************************/
int Incremental_PI_A(int Encoders_A, int Target_A)
{
    float Velocity_KP = 7.0, Velocity_KI = 0.016, Velocity_KD = 0.003;
    static int Pwm_A = 0;
    static int Integral_A = 0;
    static float Error_prev_A = 0;
    float MaxIntegral, MinIntegral;
    float Error_A = (float)(Target_A - Encoders_A);   // 偏差

    Integral_A += (int)Error_A;                        // 积分项累加
    MaxIntegral = (float)(7199 / Velocity_KI);
    MinIntegral = -(float)(7199 / Velocity_KI);
    if (Integral_A > MaxIntegral) Integral_A = (int)MaxIntegral;
    else if (Integral_A < MinIntegral) Integral_A = (int)MinIntegral;

    // 增量式输出：Kp*e + Kd*(e - e_prev)（讲解代码只用了这两项）
    Pwm_A += (int)(Velocity_KP * Error_A + Velocity_KD * (Error_A - Error_prev_A));

    if (Pwm_A > 7199) Pwm_A = 7199;
    else if (Pwm_A < -7199) Pwm_A = -7199;

    Error_prev_A = Error_A;    // 保存上一次偏差
    return Pwm_A;
}

/**************************************************************************
函数功能：增量式 PI 控制器 B（右轮）
入口参数：编码器测量值，目标速度
返回  值：电机 PWM
说明：与 A 参数一致（不同电机可分别调 Kp，做到两轮同时达到目标转速）
**************************************************************************/
int Incremental_PI_B(int Encoders_B, int Target_B)
{
    float Velocity_KP = 7.0, Velocity_KI = 0.016, Velocity_KD = 0.003;
    static int Pwm_B = 0;
    static int Integral_B = 0;
    static float Error_prev_B = 0;
    float MaxIntegral, MinIntegral;
    float Error_B = (float)(Target_B - Encoders_B);

    Integral_B += (int)Error_B;
    MaxIntegral = (float)(7199 / Velocity_KI);
    MinIntegral = -(float)(7199 / Velocity_KI);
    if (Integral_B > MaxIntegral) Integral_B = (int)MaxIntegral;
    else if (Integral_B < MinIntegral) Integral_B = (int)MinIntegral;

    Pwm_B += (int)(Velocity_KP * Error_B + Velocity_KD * (Error_B - Error_prev_B));

    if (Pwm_B > 7199) Pwm_B = 7199;
    else if (Pwm_B < -7199) Pwm_B = -7199;

    Error_prev_B = Error_B;
    return Pwm_B;
}

/**************************************************************************
函数功能：目标转速(圈/s) 换算成 编码器目标脉冲数(每 OverflowTime ms)
入口参数：rads 转速(圈/s)，范围约 -1.5 ~ 1.5
返回  值：每 OverflowTime ms 应达到的编码器脉冲数
说明：电机 ppr=700，倍频4 => 电机1转产生 700*4=2800 脉冲
      100ms 内脉冲数 = rads * (2800 / (1000/OverflowTime))
**************************************************************************/
int Rs_To_CPR(float rads)
{
    int CRP = 0;
    CRP = (int)(rads * ((700 * 4) / (1000 / OverflowTime)));
    return CRP;
}

/**************************************************************************
函数功能：系统闭环控制函数
说明：读编码器 → 算目标编码器脉冲 → PID 算 PWM → Set_Pwm
      每 OverflowTime(100ms) 调用一次
      外加时间片：前进 5 秒 → 后退 5 秒 交替（50 次 x 100ms = 5s）
**************************************************************************/
void System_Control(void)
{
    int TageA = 0, TageB = 0;   // 理论目标编码器值
    static int dir = 1;         // 1=前进，-1=后退
    static int step_count = 0;  // 时间片计数
    float target = 1.0f;        // 目标速度(圈/s)

    // 时间片切换：每 50 次调用(50x100ms=5s) 翻转方向
    step_count++;
    if (step_count >= 50)
    {
        step_count = 0;
        dir = -dir;             // 前进<->后退
    }
    target = (float)dir * 1.0f; // 前进 +1.0，后退 -1.0

    // 读取 OverflowTime ms 时间内两轮编码器脉冲数
    L_coder = Read_Encoder(2);
    R_coder = Read_Encoder(3);
    printf("dir=%d left  coder : %d\r\n", dir, L_coder);
    printf("dir=%d right coder : %d\r\n", dir, R_coder);

    // 计算目标速度(圈/s)对应的编码器脉冲数：两轮同号，同向行走
    TageA = Rs_To_CPR(target);
    TageB = Rs_To_CPR(target);
    printf("TageA  coder : %d\r\n", TageA);
    printf("TageB  coder : %d\r\n", TageB);

    // 速度闭环：PID 计算两轮 PWM
    Motor_A = Incremental_PI_A(L_coder, TageA);
    Motor_B = Incremental_PI_B(R_coder, TageB);
    printf("Motor_A  pwm : %d\r\n", Motor_A);
    printf("Motor_B  pwm : %d\r\n", Motor_B);

    Set_Pwm(Motor_A, Motor_B);    // 驱动电机
}
