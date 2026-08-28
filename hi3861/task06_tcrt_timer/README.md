# Hi3861 任务6：OpenHarmony 系统驱动实验 —— 红外对管收发（TCRT5000 + 软件定时器）

## 任务内容

在 Hi3861 上用 **GPIO** 读取**红外对管（TCRT5000 循迹传感器）**信号，并用 **软件定时器（osTimer）** 周期性探测、打印结果。

- **知识点**：OpenHarmony 中软件定时器的使用、红外发射接收器相关知识
- **重点/难点**：软件定时器的使用
- **任务内容**：熟悉 GPIO 概念；掌握红外对管的硬件原理、点亮红外对管与接收红外信号的软件操作、GPIO API；熟悉软件定时器概念与运行机制，掌握**创建一个软件定时器**及其 API 使用。

> 说明：U+ 的任务6**分步指导（`step` 字段）未在平台发布**（页面只显示任务描述），本实现按**任务描述 + 小车原理图**的通用做法完成。

## 目录结构

```
hi3861/task06_tcrt_timer/
├── 2.0_TCRT_Timer/         # 任务6 工程（模块名按 reference app_BUILD.gn 约定）
│   ├── TCRT.c              # 红外对管收发 + 软件定时器（osTimer）
│   └── BUILD.gn            # 工程编译配置
└── reference/
    └── app_BUILD.gn        # applications/sample/wifi-iot/app/BUILD.gn（指向 2.0_TCRT_Timer:TCRT）
```

## 红外对管（TCRT5000）原理与接线

- **原理**：TCRT5000 红外对管 = 红外**发射管**（LED）+ 红外**接收管**（光敏三极管）。发射管发出红外线，遇到反射物（黑线/障碍）反射回来，接收管导通。配合 **LM393 比较器**输出数字电平。
- **原理图（本小车`双路红外循迹传感器`）**：
  - 红外发射管经 **3.3VD + 120R** 恒流供电（**硬件常亮**，软件不控制发射）；
  - 接收信号经 **LM393** 输出：`TC_OUT_L → IO13`、`TC_OUT_R → IO14`；
  - 检测到反射（黑线/障碍）→ 输出**低电平**；无障碍 → **高电平**。
- **软件操作**：把 GPIO13/GPIO14 配为**输入**，用 `GpioGetInputVal()` 读取左右红外信号。

## 软件定时器（osTimer）API

| 函数 | 功能 |
|---|---|
| `osTimerNew(func, type, arg, attr)` | 创建一个软件定时器（`osTimerPeriodic`=周期，`osTimerOnce`=单次） |
| `osTimerStart(id, ticks)` | 启动定时器（period=ticks，tick 频率 100Hz，1 tick=10ms） |
| `osTimerStop(id)` / `osTimerDelete(id)` | 停止 / 删除定时器 |

> 软件定时器由系统定时器线程管理，回调中应避免阻塞（常用作周期采样）。

## 核心代码说明（2.0_TCRT_Timer/TCRT.c）

- **初始化**：`GpioInit()`；`IoSetFunc()` 把 GPIO13/GPIO14 复用为 GPIO，`GpioSetDir(..., WIFI_IOT_GPIO_DIR_IN)` 设为输入；
- **定时器回调** `TcrTimerCallback`：`GpioGetInputVal(13/14, &val)` 读左右红外信号，串口打印 `IR L=x R=y` 及状态（both/left/right reflect / clear）；
- **创建定时器**：`osTimerNew(TcrTimerCallback, osTimerPeriodic, NULL, NULL)` + `osTimerStart(tcrt_timer_id, 50)`（**50 ticks = 500ms** 读一次）；
- **启动**：`APP_FEATURE_INIT(TCRT)`。

## 编译方法（Ubuntu 虚拟机）

```bash
cd ~/harmony/code/code-1.0
export PATH=$PATH:~/gcc_riscv32/bin:~/gn:~/ninja:~/llvm/bin
python3 build.py wifiiot
```

> 编译前确认 `applications/sample/wifi-iot/app/BUILD.gn` 中 `features` 指向 `2.0_TCRT_Timer:TCRT`（见 `reference/app_BUILD.gn`）。

编译产物：`out/wifiiot/Hi3861_wifiiot_app_allinone.bin`（烧录用，多 bin 合并包）。

> 本任务用 GPIO，无需 `CONFIG_I2C_SUPPORT`（那是任务11 I2C 才需要的配置）。

## 烧录方法（Windows + HiBurn）

与任务8 相同：
1. 小车 Type-C 连电脑，串口开关拨到 **3861** 端，不要电池供电、关闭电源开关；
2. HiBurn：COM 选 CH340 串口（COM9），波特率 **2000000**；
3. `Select file` 选 **`Hi3861_wifiiot_app_allinone.bin`**；
4. 勾 `Auto burn` → `Connect` → 按小车复位键（RST）；`successful` 后 `Disconnect`；
5. 复位运行，串口助手（115200）每 500ms 打印一次 `IR L=x R=y ...`。

## 实测结果

- 编译：✅ **`python3 build.py wifiiot` → `BUILD SUCCESS`**（Ubuntu 虚拟机 `192.168.124.129`，已编译链接 `-lTCRT`）；
- 产物：`out/wifiiot/Hi3861_wifiiot_app_allinone.bin`（**778888 字节**），已拷贝到本机 `../output/Hi3861_wifiiot_app_allinone.bin`（md5 `a87224b8fc397f1a4730ce0aacd4e416`）；
- 烧录：✅ **HiBurn 实机烧录成功**（COM9、2000000、选 `allinone.bin`、Auto burn + Connect + 按复位键）；
- 现象（实测）：串口每 500ms 打印 `IR L=x R=y ...`；用反射物挡红外传感器时对应位电平翻转。✅

## 踩坑记录

- **红外发射管为硬件常亮**：本车红外发射经 3.3VD + 120R 直连，软件不需要（也无法）控制发射；软件只读接收信号。
- **LM393 输出极性**：检测到反射=低电平、无障碍=高电平（若相反，读到的电平取反理解即可）。
- **必须烧 `allinone.bin`**：`_burn.bin` 缺少元数据，HiBurn 握手失败报 `Wait SELoadr ACK overtime`。
- **软件定时器周期单位**：`osTimerStart` 参数单位是 tick（10ms），500ms=50、1s=100，不要直接填毫秒。
