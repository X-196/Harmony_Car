# Hi3861 模块总览（OpenHarmony / Hi3861 侧）

本目录存放 **Hi3861（OpenHarmony）** 侧的工程源码与资料。开发流程：在 Ubuntu 虚拟机中基于 OpenHarmony 源码编译，产物用 HiBurn 串口烧录到开发板烧录到实车验证。

## 任务进度

| 任务 | 目录 | 内容 | 状态 |
|---|---|---|---|
| 任务5 | `task05_helloworld/` | OpenHarmony 第一个程序 Hello World（双任务串口打印）| ✅ 编译 + 实机烧录成功 |
| 任务6 | `task06_tcrt_timer/` | 红外对管收发（TCRT5000）+ 软件定时器 | ✅ 编译 + 实机烧录成功 |
| 任务7 | `task07_sg90_mutex/` | GPIO 驱动舵机（SG90）+ 互斥锁多任务联动 | ✅ 编译 + 实机烧录成功 |
| 任务8 | `task08_hcsr04_tick/` | GPIO 驱动超声波（HC-SR04）+ 软件定时器 + tick | ✅ 编译 + 实机烧录成功 |
| 任务9 | `task09_uart_ble/` | UART 信息收发（消息队列 + 蓝牙 JDY-16） | ✅ 编译 + 实机烧录成功 |
| 任务10 | `task10_sum_experiment/` | 第一阶段综合实验（舵机+超声波+红外+蓝牙+消息队列） | ✅ 编译 + 实机烧录成功 |
| 任务11 | `task11_i2c_ssd1306/` | I2C 驱动 OLED（SSD1306）显示字符串 | ✅ 编译 + 实机烧录成功 |

## 任务5：OpenHarmony 系统调试实验（Hello World）

在 Hi3861 开发板上完成 **OpenHarmony 第一个程序（Hello World）**：创建工程 → 书写双任务代码 → 修改 BUILD.gn → 编译 → 烧录到开发板并运行。

**成果**：✅ 编译成功 + **实机烧录成功**，串口输出 `Hello World!` / `Hello QST!` 交替打印。

详见 [`task05_helloworld/README.md`](task05_helloworld/README.md)（内容在文件顶部——工程内暂无单独 README，见目录结构）。

## 任务6：OpenHarmony 系统驱动实验（红外对管收发 + 软件定时器）

在 Hi3861 上用 **GPIO** 读取**红外对管（TCRT5000 循迹传感器）**信号，并用 **软件定时器（osTimer）** 周期性探测、打印结果。U+ 任务6 只有任务描述（分步指导未发布），本实现按**描述 + 原理图**完成。

**成果**：✅ **编译成功（`BUILD SUCCESS`）+ 实机烧录运行成功**（GPIO13/14 读红外 + `osTimerNew` 每 500ms 打印 `IR L=x R=y`）。

- 关键点：红外发射管 3.3VD+120R 硬件常亮；LM393 输出 `TC_OUT_L→IO13`、`TC_OUT_R→IO14`；`GpioGetInputVal` 读信号；软件定时器 tick 频率 100Hz（50 ticks=500ms）。
- 详见 [`task06_tcrt_timer/README.md`](task06_tcrt_timer/README.md)。

## 任务7：OpenHarmony 系统驱动实验（GPIO 驱动舵机 + 互斥锁）

在 Hi3861 上用 **GPIO 软件 PWM** 驱动 **SG90 舵机**，结合 **RTOS 互斥锁** 实现同优先级多任务联动，完成「左转 45° → 右转 45° → 居中」顺序控制。

**成果**：✅ 编译成功（`BUILD SUCCESS`）+ **实机烧录成功**，串口依次输出「任务1开始运行」「任务3开始运行」「任务2开始运行」，舵机按左转45°→右转45°→居中循环动作。

- 关键点：SG90 信号线接 GPIO2；20ms 周期、0.5~2.5ms 高电平控制角度（1.0ms=45°、1.5ms=90°、2.0ms=135°）；`osMutexNew/Acquire/Release` 实现互斥，三任务同优先级、用 `osDelay` 错开时序。
- 详见 [`task07_sg90_mutex/README.md`](task07_sg90_mutex/README.md)。

## 任务8：OpenHarmony 系统驱动实验（GPIO 驱动超声波 + 软件定时器）

在 Hi3861 上用 **GPIO 驱动 HC-SR04 超声波测距模块**，结合 **系统 Tick** 完成定时测距。U+ 任务8【学生需要完成内容】：创建 **2 个软件定时器**——定时器1 控制超声波 **3 秒**间隔测一次距离，定时器2 控制打印当前 **tick 值**。

**成果**：✅ **编译成功（`BUILD SUCCESS`）+ 实机烧录运行成功**（`osTimerNew` 创建 2 个软件定时器：3s 测距 + 1s 打印 tick），串口每 3s 打印 `distance is X.X (cm)`、每 1s 打印 `tick value is N`。

- 关键点：HC-SR04 接 GPIO7(TRIG)/GPIO8(ECHO)；`hi_get_us()` 计时高电平时长→`distance=time*0.034/2`；`osTimerNew/osTimerStart` 创建软件定时器，tick 频率 100Hz（1tick=10ms，3s=300、1s=100）；`hi_get_tick()` 读系统 tick 值。
- 详见 [`task08_hcsr04_tick/README.md`](task08_hcsr04_tick/README.md)。

## 任务9：OpenHarmony 系统驱动实验（UART 信息收发 + 消息队列 + 蓝牙）

在 Hi3861 上用 **UART1** 实现信息收发，并结合 **消息队列（osMessageQueue）** 实现任务间通信，蓝牙（JDY-16）透传。U+ 任务9【学生需要完成内容】：在消息队列中**发送多个消息并依次读出**。

**成果**：✅ **编译成功（`BUILD SUCCESS`）+ 实机烧录运行成功**（`osMessageQueueNew/Put/Get` 发送多条消息并 FIFO 依次读出）。

- 关键点：串口 **UART1**（GPIO0=TX、GPIO1=RX，9600/8N1）；`UartInit/Read/Write`；`osMessageQueueNew/Put/Get`（生产者 thread2 放 5 条消息+串口数据，消费者 UART_Task 依次读出）；蓝牙 JDY-16 透传接 UART1。
- 详见 [`task09_uart_ble/README.md`](task09_uart_ble/README.md)。

## 任务10：第一阶段综合实验（多模块联动）

综合**舵机测距 + 红外寻线/蓝牙 + 消息队列**，完成第一阶段综合实验。U+ 任务10 只有任务描述（无分步指导），按描述综合实现。

**成果**：✅ **编译成功（`BUILD SUCCESS`）+ 实机烧录运行成功**（task1 舵机左右测距、task2 前15s 红外寻线/后蓝牙、task3 消息队列多发多读，三任务 RTOS 调度）。

- 关键点：舵机 GPIO2、超声波 GPIO7/8、红外 GPIO13/14、蓝牙+UART1 GPIO0/1(9600)+消息队列；15s 用 `hi_get_tick()`(100Hz)=1500 tick；三任务同优先级 25 时间片调度。
- 详见 [`task10_sum_experiment/README.md`](task10_sum_experiment/README.md)。

## 任务11：OpenHarmony 系统驱动实验（I2C 驱动 OLED + 显示字符串）

在 Hi3861 上用 **I2C** 驱动 **SSD1306 OLED 屏**显示字符串。U+ 任务11【学生需要完成内容】：将 **"鸿蒙先锋号"** 以字符形式显示在 OLED 上。

**成果**：✅ **编译成功（`BUILD SUCCESS`）+ 实机烧录显示成功**（`SSD1306_ShowChinese()` 显示 5 个 24×24 汉字）。

- 关键点：IIC 总线（GPIO9=SCL、GPIO10=SDA，I2C0，从机地址 `0x78`）；`I2cInit/I2cWrite/I2cSetBaudrate`；参考版字库只支持 ASCII，学生版新增 16×16 中文字库 `HZ16` + `SSD1306_ShowChinese()`；**编译前须设 `CONFIG_I2C_SUPPORT=y`**（否则 `undefined reference to hi_i2c_write`）。
- 详见 [`task11_i2c_ssd1306/README.md`](task11_i2c_ssd1306/README.md)。

## 通用编译方法（Ubuntu 虚拟机）

```bash
cd ~/harmony/code/code-1.0
export PATH=$PATH:~/gcc_riscv32/bin:~/gn:~/ninja:~/llvm/bin
python3 build.py wifiiot
```

编译产物：
- `out/wifiiot/Hi3861_wifiiot_app_burn.bin`（app）
- `out/wifiiot/Hi3861_wifiiot_app_allinone.bin`（**烧录用，多 bin 合并包**）

## 通用烧录方法（Windows + HiBurn）

1. 小车用 Type-C 数据线连电脑，串口开关拨到 **3861** 端，不要电池供电、关闭电源开关；
2. HiBurn：COM 选 CH340 串口（COM9），波特率 **2000000**；
3. `Select file` 选 **`Hi3861_wifiiot_app_allinone.bin`**；
4. 勾选 `Auto burn` → 点 `Connect` → 按小车复位键（RST）；
5. 出现 `successful` 即烧录成功，点 `Disconnect`；
6. 串口助手（115200）观察输出。

## 通用踩坑记录

- **必须烧 `allinone.bin`**：`_burn.bin` 缺少 name/FileIndex/BurnSize 元数据，HiBurn 握手失败报 `Wait SELoadr ACK overtime`。
- **烧录前关掉 Ubuntu 虚拟机**：VM 会占用串口，导致 HiBurn 连不上。
- **串口须连到 Windows 主机**而非虚拟机。
- **工具链配置**：源码默认 `build/lite/config.ini` 指向内置 prebuilts 工具链；本机使用独立工具链（`~/gcc_riscv32`、`~/gn`、`~/ninja`、`~/llvm`），需在 `config.ini` 的 `[ndk]` 段配置绝对路径（参考 `task05_helloworld/reference/config.ini`）。
- **编译环境**：非交互 SSH 不加载 `.bashrc`，编译前需显式 `export PATH`。
