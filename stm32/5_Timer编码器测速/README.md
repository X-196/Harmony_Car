# STM32 任务22：TIMER 编码器测速（5_Timer编码器测速）

## 任务内容

在 STM32F103 上使用 **TIMER 编码器模式** 读取电机编码器脉冲，实现测速。
U+ 任务要求：**把电机的速度转换成编码器的值打印出来**，为后续闭环（PID）控制做准备。

本工程在前述任务21（PWM驱动电机）基础上，新增 **TIM2/TIM3 编码器测速**，并实测换算转速与线速度。

## 成果

- ✅ 编码器初始化（TIM2 / TIM3，编码器接口模式 TI12，预分频 0，ARR=65535）
- ✅ SysTick 1ms 中断 + 每 20ms 采样一次编码器 CNT
- ✅ 串口打印左右轮：计数值 / 转速(rev/s) / 线速度(cm/s)
- ✅ 电机开环驱动（前进 2s → 停 1s → 后退 2s → 停 1s），验证测速
- ✅ 实测标定：轮子转一圈编码器脉冲数 = **2800**（替代理论推算 1440）
- ✅ 实测：周长 = **14.25 cm**（用户提供）

## 硬件与原理

- **主控**：STM32F103，外部晶振 8MHz × 9 倍频 = 72MHz
- **编码器**（左右电机上的霍尔编码器）：
  - 左轮：TIM2，PA0/PA1（编码器 A/B 相）
  - 右轮：TIM3，PA6/PA7（编码器 A/B 相）
- **编码器模式**：`TIM_EncoderMode_TI12`（TI12，即 T1/T2 双沿计数），提高采样精度
- **电机驱动**：L9110S，PWM 用 TIM4（PB6=CH1 左、PB7=CH2 右），方向 PB13/PB14

### 测速与线速度换算

```
一圈脉冲数  = 实测 2800（编码器线数/减速比/倍频综合，实测标定）
每秒脉冲数 = 20ms 内脉冲增量 × 50
转速(圈/s) = 每秒脉冲数 ÷ 2800
线速度(cm/s) = 转速(圈/s) × 周长(14.25 cm)
```

> 说明：`ENC_PULSE_PER_REV` 用实测 **2800** 而非理论推算（1440），因各车编码器参数不同，故以实测为准。

## 代码说明

| 文件 | 作用 |
|---|---|
| `USER/encoder.c` | 编码器初始化（TIM2/TIM3）+ 编码器接口配置 |
| `USER/encoder.h` | `ENCODER_TIM_PERIOD`、`Encoder_Init_TIM2/TIM3` 声明 |
| `USER/main.c` | 主程序：编码器初始化 + 电机驱动 + 20ms 采样测速打印 |
| `USER/stm32f10x_it.c` | `SysTick_Handler`：1ms 计数，每 20ms 读 TIM2/TIM3 的 CNT，置采样标志 |
| `USER/motor.c/h` | 电机驱动（任务21复用）：`PWM_Init` / `Set_Pwm` |

### 关键换算（`USER/main.c`）

```c
#define ENC_PULSE_PER_REV 2800    // 实测：轮子转一圈编码器脉冲数
#define WHEEL_CIRC_CM     1425    // 轮子周长 14.25 cm（×100）
```

每 20ms 采样后：
```c
dl = left_cnt - last_l;            // 左轮 20ms 脉冲增量
rev_l_x100 = dl * 50 * 100 / 2800; // 转速(圈/s)×100
v_l_x10 = rev_l_x100 * 1425 /100/10; // 线速度(cm/s)×10
```

串口每 20ms 输出一行：
```
L cnt=xxx R cnt=xxx | L x.xx rev/s x.x cm/s | R x.xx rev/s x.x cm/s
```

## 如何编译与演示

1. 用 **Keil MDK5** 打开 `USER/Template.uvprojx`（工程已含 `encoder.c` 在 USER 组）。
2. 编译下载到小车主板（SWD 接口）。
3. 供电运行：电机**前进 2 秒 → 停 → 后退 2 秒 → 停**循环；同时串口(115200)每 20ms 打印左右轮计数值、转速、线速度。
4. 转动轮子/让电机转，看串口 `cnt` 变化与 `rev/s`、`cm/s` 是否合理。

> 注意：电机驱动需电池供电（M-5V 来自电池经 LM2596 降压）；串口通信可走 USB（TypeC）。二者独立供电轨。

## 关键文件

| 文件 | 说明 |
|---|---|
| `USER/main.c` | 主程序：编码器 + 电机 + 20ms 测速打印 |
| `USER/encoder.c` | 编码器初始化（TIM2/TIM3） |
| `USER/encoder.h` | 编码器常量与函数声明 |
| `USER/stm32f10x_it.c` | SysTick 1ms 采样中断 |

*说明：仓库内仅保留源码与工程文件，编译产物（OBJ/Listings）由 Keil 编译时自动生成，不入库。*
