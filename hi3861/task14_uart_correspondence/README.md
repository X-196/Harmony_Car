# 任务24：系统通信协议（12.0_UART_Correspondence）

Hi3861（主核）通过 UART2 把**自研 6 字节运动控制协议**发给 STM32（从核），小车自动跑起来：前进/后退/左转/右转循环演示，同时 STM32 侧左转打左转向灯、右转打右转向灯、后退亮倒车灯。

## 通信协议

物理层：UART TTL、**115200-8-N-1**（GPIO_11=UART2_TXD、GPIO_12=UART2_RXD ↔ STM32 USART1 PA9/PA10）

| 字段 | Byte | 值 | 功能 |
|---|---|---|---|
| 帧头 | 1 | `0xFC` | 判断数据帧头部 |
| 左轮方向 | 2 | 0 / 1 | 0 正转（前进）、1 反转（后退） |
| 左轮速度 | 3 | 0~150 | 精度 0.01，单位 圈/s（实际转速 ×100） |
| 右轮方向 | 4 | 0 / 1 | 同上 |
| 右轮速度 | 5 | 0~150 | 同上 |
| 帧尾 | 6 | `0xFD` | 判断数据帧尾部 |

## 实现（`correspondence.c`）

- `stm32motor_control(motorA, motorB)`：讲解版协议打包——负值拆出方向位、限幅 ±150、装帧、`UartWrite(WIFI_IOT_UART_IDX_2, buf, 6)`
- 动作封装：`car_forward(100,100)`、`car_backward(-100,-100)`、`car_left(50,150)`（左轮慢右轮快→车头向左）、`car_right(150,50)`、`car_stop(0,0)`
- **学生任务**（U+：更改 3861 的程序，验证前进、后退、左转、右转）：`car_demo` 任务循环 **前进 2s → 左转 2s → 右转 2s → 后退 2s → 停止 2s**，每步串口 0 打印动作名
- 帧只在动作开始时发一次（STM32 锁存目标持续闭环），调试 printf 走串口 0 与 UART2 分离互不干扰

## 编译与烧录

1. 上传 `12.0_UART_Correspondence/` 到虚拟机 `applications/sample/wifi-iot/app/`，改 `app/BUILD.gn` 指向 `12.0_UART_Correspondence:correspondence`（参考 `reference/app_BUILD.gn`）
2. `python3 build.py wifiiot` → 产物拷回 `output/Hi3861_wifiiot_app_allinone.bin`
3. HiBurn 烧录；STM32 侧配套工程见 `stm32/8_双核协议控制/`（协议解析 + PID 闭环 + 转向灯）
4. 上电后小车循环：前进 → 左转（左转向灯闪）→ 右转（右转向灯闪）→ 后退（倒车灯）→ 停止（灯灭）。**跑动需电池供电**

## 接线

Hi3861 GPIO_11(TX) → STM32 PA10(RX)，GPIO_12(RX) ← STM32 PA9(TX)，GND 共地。
