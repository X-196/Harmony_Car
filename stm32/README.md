# STM32 工程说明（2026-08-25）

## 工程概况

- **工程**：02_串口收发打印（Keil MDK5 工程，`USER/Template.uvprojx`）
- **主控**：STM32F103 系列，外部晶振 8MHz × 9 倍频 = 72MHz
- **串口**：`uart_init(115200)`，用于接收指令实时切换灯光效果
- **调试**：SWD 接口（已关闭 JTAG，保留 SWD）

## 今天（2026-08-25）实现的内容

### 1. 学习 WS2812 炫彩灯驱动思路（`QST_HARDWARE/colorful_led/`）

- **硬件连接**：左右两条 WS2812 RGB 灯带，控制引脚 `PC13`（DIL）/ `PC14`（DIR），推挽输出 50MHz。
- **单总线时序**：用 `__NOP()` 空指令做软件延时模拟 WS2812 时序（0 码 400ns/850ns，1 码 850ns/400ns），不依赖定时器与 PWM。
- **数据结构**：颜色按 **GRB** 顺序写入 `L_ws_data / R_ws_data` 数组，每颗灯 3 字节。
- **帧刷新**：`ws2812_refresh()` 逐位（高位到低位）发送，帧尾拉低 66us 复位。

### 2. 完成：让所有灯亮（ALL LIGHT / Led_All_On） ✅

### 3. 灯光效果创新（在 `USER/main.c` + `colorful_led.c` 中实现） ✅

通过**串口命令实时切换**效果模式，收到指令立即生效、无需复位：

| 串口指令 | 效果模式 | 说明 |
|---|---|---|
| `1` | HELLO | 问候图案 |
| `2` | ALL LIGHT | 全部灯亮（基础实现） |
| `3` | FASTROUND | 彩色灯带快速旋转 |
| `4` | SLOWROUND | 彩色灯带慢速旋转 |
| `5` | FASTBL | 快速呼吸/闪烁 |
| `6` | SLOWBL | 慢速呼吸/闪烁 |

- 效果采用**多帧渲染机制**（`led_effect_init / led_effect_run / led_effect_set_mode`），主循环每 10ms 渲染一帧（`delay_ms(10)`），非阻塞、可随时切换。
- 另有跑马灯 `L_runingled()`（6 颗灯正向 1→6、反向 6→1 往复流动，每 100ms 一格）和 `L_led_mode()` 的 18 种循环花样（每个花样 1s 自动切换）。

## 关键文件导航

| 文件 | 说明 |
|---|---|
| `USER/main.c` | 主程序：初始化 + 串口指令切换灯效 + 逐帧驱动 |
| `QST_HARDWARE/colorful_led/colorful_led.c` | WS2812 驱动 + 效果模式实现（今日核心） |
| `QST_HARDWARE/colorful_led/colorful_led.h` | 颜色宏定义、效果模式枚举、函数声明 |
| `QST_HARDWARE/SYSTEM_CONTROL/control_system.c/h` | 运动控制模块（工程附带） |
| `SYSTEM/` | delay / sys / usart 基础外设 |
| `CORE/` | 启动文件与 core 支持 |
| `STM32F10x_FWLib/` | STM32 标准外设库 |

## 如何编译与演示

1. 用 **Keil MDK5** 打开 `USER/Template.uvprojx`，编译下载到小车主板（SWD 接口）。
2. 打开串口助手（115200、8N1），发送 `1`~`6` 即可实时切换灯光效果。
3. 灯光会按所选模式持续运行，验证"所有灯亮 + 多种动态效果"。

*说明：仓库内仅保留源码与工程文件，编译产物（OBJ/Listings）由 Keil 编译时自动生成。*
