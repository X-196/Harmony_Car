# Harmony_Car —— QST「先锋号」鸿蒙智能小车

本项目是 **QST「先锋号」鸿蒙智能小车** 的课程 / 实训项目仓库，基于 **OpenHarmony + STM32** 双平台开发，目标是完成从开发环境搭建、外设驱动到整车运动控制的全流程学习与实机验证。

## 项目简介

- **整车型号**：QST「先锋号」鸿蒙智能小车
- **核心硬件**：Hi3861（OpenHarmony 主控）+ STM32F103（外设与电机控制）
- **开发方式**：Hi3861 侧在 Ubuntu 虚拟机中基于 OpenHarmony 源码编译，通过 HiBurn 烧录；STM32 侧使用 Keil MDK5 开发，通过 SWD 下载
- **当前进度**：
  - 阶段一（OpenHarmony / Hi3861）：任务 3 / 4 / 5 / 7 / 8 完成（共 10 个任务）
  - 阶段二（OpenHarmony / Hi3861）：任务 11 完成（I2C OLED 显示）
  - 阶段三（STM32）：任务 19（串口收发 + WS2812 炫彩灯）、任务 21（PWM 驱动电机）、任务 22（TIMER 编码器测速）、任务 23（PID 电机速度闭环控制）完成

## 项目结构

```
Harmony_Car/
├── README.md                # 项目总说明（本文件）
├── diary/                   # 开发日志（按日期记录任务完成情况）
│   ├── 2026-08-25.md        # WS2812 炫彩灯学习与创新、Linux 环境配置
│   └── 2026-08-26.md        # 任务3/4/5/7/21 完成记录
├── hi3861/                  # Hi3861 / OpenHarmony 侧源码与资料
│   ├── task05_helloworld/   # 任务5：OpenHarmony 系统调试实验（Hello World）
│   │   ├── 1.0_Hello_World/ # 双任务 HelloWorld 工程（hello_world.c + BUILD.gn）
│   │   └── reference/       # 编译环境参考配置（BUILD.gn、config.ini 等）
│   ├── task07_sg90_mutex/   # 任务7：GPIO 驱动舵机（SG90 + 互斥锁多任务）
│   │   ├── 3.0_SG90_Mutex/      # U+ 参考源程序
│   │   ├── student_3.0_SG90_Mutex/  # 学生完成版（最终烧录跑通）
│   │   └── reference/       # app/BUILD.gn 参考
│   ├── task08_hcsr04_tick/  # 任务8：GPIO 驱动超声波（HC-SR04 + 软件定时器）
│   │   ├── 4.0_Hcsr04_Tick/      # U+ 参考源程序
│   │   ├── student_4.0_Hcsr04_Tick/  # 学生完成版（2 个软件定时器）
│   │   └── reference/       # app/BUILD.gn 参考
│   └── task11_i2c_ssd1306/  # 任务11：I2C 驱动 OLED（SSD1306）显示字符串
│       ├── 7.0_I2c_Ssd1306/      # U+ 参考源程序
│       ├── student_7.0_I2c_Ssd1306/  # 学生完成版（SSD1306_ShowChinese 显示鸿蒙先锋号）
│       └── reference/       # app/BUILD.gn 参考
└── stm32/                   # STM32 侧 Keil MDK5 工程
    ├── 02_串口收发打印/      # 任务19：串口收发 + WS2812 炫彩灯效果
    ├── 4_PWM驱动电机/        # 任务21：PWM 驱动直流电机（前进 2s → 后退 2s）
    ├── 5_Timer编码器测速/    # 任务22：TIMER 编码器测速（读转速 + 线速度换算）
    └── 6_PID电机闭环控制/    # 任务23：PID 电机速度闭环控制（走直线）
```

## 硬件平台

| 模块 | 说明 |
|---|---|
| 主控（Hi3861） | 海思 Hi3861，OpenHarmony 开发板，串口烧录（HiBurn） |
| 主控（STM32） | STM32F103，外部晶振 8MHz × 9 倍频 = 72MHz，SWD 调试 |
| 电机驱动 | L9110S，方向引脚 PB13(BIN) / PB14(AIN) |
| 电机 PWM | TIM4（CH1 = PB6 / CH2 = PB7），频率 1000Hz，占空比可调 |
| 编码器 | 左右轮霍尔编码器，TIM2(PA0/PA1) 左轮、TIM3(PA6/PA7) 右轮，编码器接口 TI12 模式 |
| 炫彩灯带 | 左右两条 WS2812 RGB 灯带（各 24 颗灯，6 颗工作灯），PC13 / PC14 控制 |
| 串口 | CH340 USB 转串口（COM9），Hi3861 115200 / 烧录 2000000 |

## 已完成功能

### STM32 侧（Keil MDK5）

- **任务19 · 02_串口收发打印**：串口收发（115200）实时控制 WS2812 炫彩灯
  - 基础功能：所有灯全亮（ALL LIGHT）
  - 多彩效果：快速 / 慢速旋转（FASTROUND / SLOWROUND）、快速 / 慢速呼吸（FASTBL / SLOWBL）
  - 创新效果：跑马灯往复流动、18 种花样循环灯光秀
  - 串口指令 `1`~`6` 实时切换效果，无需复位
- **任务21 · 4_PWM驱动电机**：PWM 驱动直流电机让小车动起来
  - TIM4 输出 1000Hz PWM，占空比控制速度（`Set_Pwm` 正值前进、负值后退）
  - 老师补充要求：**前进 2 秒 → 后退 2 秒** 循环运动，已实机跑通
- **任务22 · 5_Timer编码器测速**：TIMER 编码器模式读取电机转速
  - TIM2/TIM3 编码器接口模式（TI12）读左右轮编码器（左 PA0/PA1、右 PA6/PA7）
  - SysTick 1ms 中断 + 每 20ms 采样一次 CNT，串口打印左右轮计数值/转速(rev/s)/线速度(cm/s)
  - 实测标定：轮子一圈脉冲数 **2800**，轮子周长 **14.25cm** 换算线速度
  - 电机开环驱动（前进 2s → 停 → 后退 2s → 停）验证测速，已实机跑通
- **任务23 · 6_PID电机闭环控制**：PID 电机速度闭环控制（走直线）
  - 编码器测速基础上，用增量式 PID（Kp=7.0、Kd=0.003）读两轮实际转速、调 PWM 使左右轮转速一致
  - `Rs_To_CPR()` 把目标转速(圈/s)换算成编码器脉冲数（电机 ppr=700 × 倍频4=2800）
  - 方向修正：两轮目标同号（+1.0 圈/s）→ 两轮同向向前走直线（实测左右轮转向相反已修正）
  - 前 5 秒 → 后退 5 秒 循环；实机验证走直线 ✅

### Hi3861 侧（OpenHarmony）

- **任务5 · task05_helloworld**：OpenHarmony 第一个程序（Hello World）
  - 双任务编程：`thread1` 每 1s 打印 `Hello World!`，`thread2` 每 3s 打印 `Hello QST!`
  - 已完成 **编译成功 + 实机烧录成功**，串口输出验证通过
- **任务7 · task07_sg90_mutex**：OpenHarmony 系统驱动实验（GPIO 驱动舵机 + 互斥锁）
  - GPIO2 软件 PWM 驱动 SG90 舵机（20ms 周期、0.5~2.5ms 脉宽 = 0°~180°）
  - 用 `osMutex` 互斥锁实现**同优先级**三任务对舵机的串行化访问，`osDelay` 错开时序
  - 串口依次输出「任务1/3/2开始运行」，舵机按**左转 45° → 右转 45° → 居中**循环动作
  - 已完成 **编译成功（`BUILD SUCCESS`）+ 实机烧录成功**
- **任务8 · task08_hcsr04_tick**：OpenHarmony 系统驱动实验（GPIO 驱动超声波 + 软件定时器）
  - HC-SR04 超声波接 GPIO7(TRIG)/GPIO8(ECHO)；`hi_get_us()` 计时高电平→`distance=time*0.034/2`
  - 创建 **2 个软件定时器**：定时器1 每 3s 测距一次，定时器2 每 1s 打印当前 `hi_get_tick()` 值
  - tick 频率 100Hz（1tick=10ms）：`osTimerStart(..., 300)`=3s、`(...,100)`=1s
  - ✅ 编译成功（`BUILD SUCCESS`）+ **实机烧录运行成功**
- **任务11 · task11_i2c_ssd1306**：OpenHarmony 系统驱动实验（I2C 驱动 OLED + 显示字符串）
  - IIC 总线（GPIO9=SCL、GPIO10=SDA，I2C0，从机地址 `0x78`）；`I2cInit/I2cWrite/I2cSetBaudrate`
  - 学生版新增 **16×16 中文字库** + `SSD1306_ShowChinese()` 显示 **"鸿蒙先锋号"**（参考版字库只有 ASCII）
  - ⚠️ 编译前须设 `CONFIG_I2C_SUPPORT=y`（`build/config/usr_config.mk`），否则 `undefined reference to hi_i2c_write`
  - ✅ 编译成功（`BUILD SUCCESS`），烧录待实机验证
## 环境与工具链

| 工具 | 用途 |
|---|---|
| Ubuntu 20.04 虚拟机 | OpenHarmony 源码编译（RaiDrive 挂载、VSCode Remote-SSH 免密连接） |
| Hi3861 工具链 | `gcc_riscv32` / `gn` / `ninja` / `llvm`，需在 `build/lite/config.ini` 配置绝对路径 |
| HiBurn | Windows 下 Hi3861 串口烧录（必须烧 `allinone.bin`） |
| Keil MDK5 + ST-Link | STM32 工程编译与 SWD 下载 |
| 串口助手 | 串口调试与灯效控制（115200、8N1） |

## 编译与运行

### STM32 侧

1. 用 Keil MDK5 打开对应工程的 `USER/Template.uvprojx`；
2. 编译（0 Error）后通过 ST-Link / SWD 下载到小车主板；
3. 「02_串口收发打印」：打开串口助手（115200、8N1），发送 `1`~`6` 切换灯效；
4. 「4_PWM驱动电机」：上电后小车自动执行前进 2s → 后退 2s 循环（调速：修改 `main.c` 中 `Set_Pwm` 的数值）；
5. 「5_Timer编码器测速」：上电后电机前进 2s → 停 → 后退 2s → 停循环，串口(115200)每 20ms 打印左右轮计数值、转速(rev/s)、线速度(cm/s)。
6. 「6_PID电机闭环控制」：上电后小车前进 5s → 后退 5s 循环（PID 调两轮转速一致），串口(115200)每 100ms 打印左右轮 coder/Tage/Motor pwm。

### Hi3861 侧

1. 虚拟机中进入 OpenHarmony 源码，执行 `python3 build.py wifiiot` 编译；
2. 用 HiBurn 选择 `Hi3861_wifiiot_app_allinone.bin` 烧录（COM9、2000000、Auto burn）；
3. 串口助手（115200）观察输出（任务5：`Hello World!` / `Hello QST!`；任务7：任务1/3/2 运行日志 + 舵机动作；任务8：每 3s 一条 `distance is X.X (cm)` + 每 1s 一条 `tick value is N`）；任务11：观察 **OLED 屏**是否居中显示"鸿蒙先锋号"（无需串口，现象在屏上）。

> 详细步骤与踩坑记录见各子目录 README：
> - [`hi3861/README.md`](hi3861/README.md) —— Hi3861 模块总览（任务5 编译 / 烧录 / 踩坑，任务7 舵机 + 互斥锁，任务8 超声波 + 软件定时器）
> - [`hi3861/task07_sg90_mutex/README.md`](hi3861/task07_sg90_mutex/README.md) —— 任务7 GPIO 驱动舵机 + 互斥锁
> - [`hi3861/task08_hcsr04_tick/README.md`](hi3861/task08_hcsr04_tick/README.md) —— 任务8 GPIO 驱动超声波 + 软件定时器
> - [`hi3861/task11_i2c_ssd1306/README.md`](hi3861/task11_i2c_ssd1306/README.md) —— 任务11 I2C 驱动 OLED 显示字符串
> - [`stm32/README.md`](stm32/README.md) —— STM32 任务19 串口 + 炫彩灯
> - [`stm32/4_PWM驱动电机/README.md`](stm32/4_PWM驱动电机/README.md) —— 任务21 PWM 驱动电机
> - [`stm32/5_Timer编码器测速/README.md`](stm32/5_Timer编码器测速/README.md) —— 任务22 TIMER 编码器测速
> - [`stm32/6_PID电机闭环控制/README.md`](stm32/6_PID电机闭环控制/README.md) —— 任务23 PID 电机速度闭环控制

## 开发日志

每日开发记录见 [`diary/`](diary/)：

- [2026-08-25](diary/2026-08-25.md)：WS2812 炫彩灯驱动思路学习与灯光效果创新；Linux 开发环境配置（虚拟机 + SSH）
- [2026-08-26](diary/2026-08-26.md)：任务3/4/5（OpenHarmony 环境 + Hello World 烧录）、任务7（GPIO 舵机 + 互斥锁）与任务21（PWM 电机）完成

## 后续计划

- 阶段一剩余任务：任务6、9、10 —— 红外对管收发、UART 信息收发、阶段综合实验；阶段二：任务11 已完成，后续 12 起（温湿度/光照/云平台等）待发布。
- 阶段三任务 22（TIMER 编码器测速）、任务 23（PID 电机速度闭环控制）已完成 | 剩下 24~28：更多传感器与运动控制
