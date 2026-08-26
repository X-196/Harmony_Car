# STM32 任务21：PWM 驱动电机（4_PWM驱动电机）

## 任务内容

在 STM32F103 上产生 **PWM 信号驱动直流电机**，让「QST先锋号」小车动起来。
官方任务要求：**更改速度值，观察两个电机的速度变化**。

老师补充要求（丰富运动功能）：**小车先前进 2 秒 → 再后退 2 秒**。

## 成果

- ✅ 完成 PWM 驱动电机（TIM4 输出 PWM，L9110S 驱动芯片）
- ✅ 小车按设定速度前进
- ✅ 实现"前进 2 秒 → 后退 2 秒"循环运动（丰富运动功能）

## 硬件与原理

- **主控**：STM32F103，外部晶振 8MHz × 9 倍频 = 72MHz
- **PWM**：TIM4（通用定时器），PB6=CH1、PB7=CH2 复用推挽输出
- **电机驱动**：L9110S 芯片，方向引脚 PB13(BIN)/PB14(AIN)
- **PWM 频率**：`PWM频率 = 时钟频率/(PSC+1)/(ARR+1) = 72M/(9+1)/(7199+1) = 1000Hz`
- **占空比**：`占空比 = 比较值/(ARR+1)`，本工程 `Set_Pwm(2500,2500)` ≈ 35%

## 代码说明

### 1. `USER/motor.h` — 宏定义
```c
#define AIN  PBout(14)   // B14 方向引脚（PWMA）
#define BIN  PBout(13)   // B13 方向引脚（PWMB）
#define PWMA TIM4->CCR1  // 左轮（TIM4_CH1 = PB6）
#define PWMB TIM4->CCR2  // 右轮（TIM4_CH2 = PB7）
```

### 2. `USER/motor.c` — 电机驱动模块
| 函数 | 作用 |
|---|---|
| `Motor_Init()` | 初始化方向引脚 PB13/PB14（推挽输出） |
| `PWM_Init(arr, psc)` | 初始化 TIM4 PWM，`PWM_Init(7199, 9)` 频率 1000Hz |
| `Set_Pwm(moto1, moto2)` | 设置左右轮速度：**>0 前进，<0 后退**，值 ≤ ARR |

`Set_Pwm` 逻辑（正负值决定方向 + 反相 PWM）：
```c
void Set_Pwm(int moto1, int moto2) {
    if (moto2 >= 0) { AIN = 0; PWMA = myabs(moto2); }       // 右轮前进
    else            { AIN = 1; PWMA = 7199 - myabs(moto2); } // 右轮后退
    if (moto1 >= 0) { BIN = 0; PWMB = myabs(moto1); }       // 左轮前进
    else            { BIN = 1; PWMB = 7199 - myabs(moto1); } // 左轮后退
}
```

### 3. `USER/main.c` — 主程序（前进2秒→后退2秒）
```c
while (1) {
    Set_Pwm(2500, 2500);      // 前进（左右轮正速度）
    delay_ms(1000);
    delay_ms(1000);           // 前进 2 秒
    Set_Pwm(-2500, -2500);    // 后退（左右轮负速度）
    delay_ms(1000);
    delay_ms(1000);           // 后退 2 秒
}
```

## 如何编译与演示

1. 用 **Keil MDK5** 打开 `USER/Template.uvprojx`（工程已含 `motor.c` 在 USER 组）。
2. 编译下载到小车主板（SWD 接口）。
3. 供电运行：小车**前进 2 秒 → 后退 2 秒**反复循环。
4. 想调速：改 `main.c` 里的 `Set_Pwm(2500, 2500)` 数值即可（如 1000 慢、5000 快）。

> 注意：小车通过数据线连接电脑时可直接供电；运行时建议用电池供电（驱动电机靠电池）。

## 关键文件

| 文件 | 说明 |
|---|---|
| `USER/main.c` | 主程序：PWM初始化 + 前进/后退2秒循环 |
| `USER/motor.c` | 电机驱动：Motor_Init / PWM_Init / Set_Pwm |
| `USER/motor.h` | 方向引脚与 PWM 寄存器宏定义 |
| `CORE/stm32f10x.h` | STM32 寄存器与类型定义（CMSIS） |

*说明：仓库内仅保留源码与工程文件，编译产物由 Keil 编译时自动生成。*
