# Hi3861 模块总览（OpenHarmony / Hi3861 侧）

本目录存放 **Hi3861（OpenHarmony）** 侧的工程源码与资料。开发流程：在 Ubuntu 虚拟机中基于 OpenHarmony 源码编译，产物用 HiBurn 串口烧录到开发板烧录到实车验证。

## 任务进度

| 任务 | 目录 | 内容 | 状态 |
|---|---|---|---|
| 任务5 | `task05_helloworld/` | OpenHarmony 第一个程序 Hello World（双任务串口打印）| ✅ 编译 + 实机烧录成功 |
| 任务7 | `task07_sg90_mutex/` | GPIO 驱动舵机（SG90）+ 互斥锁多任务联动 | ✅ 编译 + 实机烧录成功 |

## 任务5：OpenHarmony 系统调试实验（Hello World）

在 Hi3861 开发板上完成 **OpenHarmony 第一个程序（Hello World）**：创建工程 → 书写双任务代码 → 修改 BUILD.gn → 编译 → 烧录到开发板并运行。

**成果**：✅ 编译成功 + **实机烧录成功**，串口输出 `Hello World!` / `Hello QST!` 交替打印。

详见 [`task05_helloworld/README.md`](task05_helloworld/README.md)（内容在文件顶部——工程内暂无单独 README，见目录结构）。

## 任务7：OpenHarmony 系统驱动实验（GPIO 驱动舵机 + 互斥锁）

在 Hi3861 上用 **GPIO 软件 PWM** 驱动 **SG90 舵机**，结合 **RTOS 互斥锁** 实现同优先级多任务联动，完成「左转 45° → 右转 45° → 居中」顺序控制。

**成果**：✅ 编译成功（`BUILD SUCCESS`）+ **实机烧录成功**，串口依次输出「任务1开始运行」「任务3开始运行」「任务2开始运行」，舵机按左转45°→右转45°→居中循环动作。

- 关键点：SG90 信号线接 GPIO2；20ms 周期、0.5~2.5ms 高电平控制角度（1.0ms=45°、1.5ms=90°、2.0ms=135°）；`osMutexNew/Acquire/Release` 实现互斥，三任务同优先级、用 `osDelay` 错开时序。
- 详见 [`task07_sg90_mutex/README.md`](task07_sg90_mutex/README.md)。

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
