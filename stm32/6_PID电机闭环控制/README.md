# STM32 任务23：PID 电机速度闭环控制（6_PID电机闭环控制）

## 任务内容

在 STM32F103 上实现 **PID 电机速度闭环控制**：读取两轮编码器实测转速，用 PID 控制器调节 PWM，使左右轮实际转速达到目标值 → **两轮转速一致，小车走直线**。

U+ 任务要求：学习 PID 原理（比例/积分/微分），用 STM32 做闭环控制，**调试自己的 PID 参数**（老师强调每台车参数不同）。

## 成果

- ✅ 编码器测速（复用任务22：TIM2/TIM3 编码器接口）
- ✅ **增量式 PID（PI）** 控制器 `Incremental_PI_A`（左轮）/ `Incremental_PI_B`（右轮）
- ✅ `Rs_To_CPR()`：把目标转速(圈/s)换算成编码器目标脉冲数（电机 ppr=700 × 倍频4=2800 脉冲/圈）
- ✅ `System_Control()`：读编码器 → 算目标 → PID 算 PWM → `Set_Pwm`，每 100ms 闭环一次
- ✅ **方向修正**：两轮目标同号（都 +1.0 圈/s）→ 两轮同向向前，走直线（任务21/22 实测同号=前进）
- ✅ **前进 5 秒 → 后退 5 秒** 循环（时间片切换，dir 每 50 次 × 100ms = 5s 翻转）
- ✅ 实机验证走直线 ✅（两轮转速一致）

## PID 原理（U+ 讲解要点）

- **为什么需要闭环**：给两轮相同 PWM，因摩擦/电压/电磁干扰等，实际转速与期望不一致 → 车走弧线。闭环根据实际输出调整输入，缩小误差。
- **PID** = 比例(P) + 积分(I) + 微分(D)。
  - **P 项**：误差 × Kp，决定响应速度，过小有静差、过大震荡。
  - **I 项**：累加误差，消除稳态误差，过多超调。
  - **D 项**：误差变化率，抑制超调、加快稳定。
- **增量式 PID（本工程）**：`pwm += Kp*e(k) + Kd*[e(k)-e(k-1)]`（讲解代码只用了 Kp 比例 + Kd 微分）。
- **参数整定**：先 Kp 后 Kd，按本车实测调，每台车不同。

### 当前 PID 参数（`USER/pid.c`）

```c
float Velocity_KP = 7.0, Velocity_KI = 0.016, Velocity_KD = 0.003;
```
- Kp=7.0（比例，实际生效）、Kd=0.003（微分，实际生效）、Ki=0.016（积分，讲解里定义但当前计算未接入）。

## 代码说明

| 文件 | 作用 |
|---|---|
| `USER/pid.c` | 增量式 PID（Incremental_PI_A/B）、Rs_To_CPR、System_Control |
| `USER/pid.h` | PID 函数声明 |
| `USER/encoder.c/h` | 编码器初始化 + `Read_Encoder(2/3)` 读 TIM2/TIM3 CNT |
| `USER/motor.c/h` | 电机驱动（PWM_Init / Set_Pwm） |
| `USER/main.c` | 初始化编码器/电机PWM/串口/SysTick，主循环空转 |
| `USER/stm32f10x_it.c` | SysTick_Handler 每 100ms 调 System_Control |

### 关键换算

- 电机 ppr=700，倍频4 → 电机转一圈 = 700×4 = **2800 脉冲**（和任务22实测一致）
- `Rs_To_CPR(rads)`：`rads × (2800 / (1000/100ms))` → 每 100ms 应达到的编码器脉冲数
- `System_Control` 每 100ms：读左/右编码器 → 算目标 TageA/B → PID（Incremental_PI_*）算 Motor_A/B PWM → Set_Pwm

## 如何编译与演示

1. Keil MDK5 打开 `USER/Template.uvprojx`（已含 pid.c）。
2. 编译（0 Error）→ St-Link 下载（ST-Link Debugger + STM32F10x_128 算法）。
3. 供电：电机需电池（JP4）+ 串口走 USB 并存。
4. 现象：小车**前进 5 秒 → 后退 5 秒**循环，走直线；串口(115200)每 100ms 打印两轮 coder / Tage / Motor 的 pwm。

### 串口数据（老师检查用）

每 100ms 打印一组：
```
dir=1 left  coder : xxx      // 左轮实测脉冲
dir=1 right coder : xxx      // 右轮实测脉冲
TageA coder : xxx            // 左轮目标脉冲
TageB coder : xxx            // 右轮目标脉冲
Motor_A pwm : xxx            // PID 输出左轮 PWM
Motor_B pwm : xxx            // PID 输出右轮 PWM
```
- `coder` 接近 `Tage`（误差小）→ 闭环正常
- `Motor_A/B` 数值接近 → 两轮转速一致 → 走直

## 关键文件

| 文件 | 说明 |
|---|---|
| `USER/pid.c` | PID 闭环核心（增量式 PI + Rs_To_CPR + System_Control） |
| `USER/encoder.c` | 编码器初始化 + Read_Encoder |
| `USER/main.c` | 主程序初始化 |

*说明：仓库内仅保留源码与工程文件，编译产物由 Keil 编译时自动生成。*
