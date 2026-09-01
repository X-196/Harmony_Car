#include "pid.h"
#include "encoder.h"
#include "motor.h"
#include "usart.h"
#include <stdio.h>
#include <string.h>

/* 电机 A=左轮，B=右轮 */
int L_coder, R_coder;         // 编码器实测值
int Motor_A, Motor_B;         // 电机 PWM 变量
int OverflowTime = 100;       // 闭环周期(ms)，与 SysTick 采样周期一致

/* 双核协议目标(带符号, 圈/s)与车灯状态(供主循环渲染) */
float Target_MotorA = 0, Target_MotorB = 0;   // 左/右轮目标转速
volatile u8 Car_Led_State = CAR_LED_STOP;     // 车灯状态, 协议帧到达时更新
volatile u32 Car_Led_Tick = 0;                // 状态最后一次变化的时间片计数

/* 车灯状态说明(转向灯渲染在主循环 led_task 中进行):
 *   CAR_LED_STOP   停止        灯全灭
 *   CAR_LED_RUN    前进        日行灯: 左右两条常亮白
 *   CAR_LED_LEFT   左转        左侧转向灯闪(琥珀色), 右侧灭
 *   CAR_LED_RIGHT  右转        右侧转向灯闪(琥珀色), 左侧灭
 *   CAR_LED_BACK   后退        倒车灯: 左右两条常亮白(红边)
 */

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
函数功能：系统闭环控制函数（每 OverflowTime=100ms 由 SysTick 调用一次）
说明：1. 消费双核协议帧 -> 恢复带符号目标转速 -> 更新车灯状态
      2. 读编码器 -> 算目标编码器脉冲 -> PID 算 PWM -> Set_Pwm
**************************************************************************/
void System_Control(void)
{
    int TageA = 0, TageB = 0;   // 理论目标编码器值

    /******获取解析数据帧********/
    if (uart_rec_flag)                   //收到一帧数据
    {
        Target_MotorA = CAR_buff[1] / 100.0f;   //左轮转速绝对值(圈/s)
        Target_MotorB = CAR_buff[3] / 100.0f;   //右轮转速绝对值

        //转速值还原(方向参数)
        if (CAR_buff[0] == 1) Target_MotorA = -1 * Target_MotorA;
        if (CAR_buff[2] == 1) Target_MotorB = -1 * Target_MotorB;

        //车灯状态判定(供主循环渲染转向灯/倒车灯)
        if (Target_MotorA < 0 && Target_MotorB < 0)          // 两轮都后退 -> 倒车
            Car_Led_State = CAR_LED_BACK;
        else if (Target_MotorA == 0 && Target_MotorB == 0)   // 全零 -> 停止
            Car_Led_State = CAR_LED_STOP;
        else if (Target_MotorB > Target_MotorA)              // 右轮快 -> 车头向左 -> 左转
            Car_Led_State = CAR_LED_LEFT;
        else if (Target_MotorA > Target_MotorB)              // 左轮快 -> 车头向右 -> 右转
            Car_Led_State = CAR_LED_RIGHT;
        else                                                  // 两轮同速正转 -> 直行
            Car_Led_State = CAR_LED_RUN;
        Car_Led_Tick = 0;

        uart_rec_flag = 0;
        memset((void*)CAR_buff, 0, 4);    //清除 等待获取下一帧

        printf("Frame: A=%.2f B=%.2f led=%d\r\n", (double)Target_MotorA, (double)Target_MotorB, Car_Led_State);
    }

    // 读取 OverflowTime ms 时间内两轮编码器脉冲数
    L_coder = Read_Encoder(2);
    R_coder = Read_Encoder(3);

    // 计算目标速度(圈/s)对应的编码器脉冲数
    TageA = Rs_To_CPR(Target_MotorA);
    TageB = Rs_To_CPR(Target_MotorB);

    // 速度闭环：PID 计算两轮 PWM
    Motor_A = Incremental_PI_A(L_coder, TageA);
    Motor_B = Incremental_PI_B(R_coder, TageB);

    Set_Pwm(Motor_A, Motor_B);    // 驱动电机

    Car_Led_Tick++;               // 车灯时间片计数(供转向灯闪烁节拍)
}
