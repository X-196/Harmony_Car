# Hi3861 任务7：OpenHarmony 系统驱动实验 —— GPIO 驱动舵机（SG90 + 互斥锁）

## 任务内容

在 Hi3861 开发板上用 **GPIO 输出 PWM 脉冲**驱动 **SG90 舵机**，并结合 **RTOS 互斥锁（Mutex）** 实现同优先级的多任务联动，完成舵机「左转 45° → 右转 45° → 居中」的顺序控制。

**成果**：✅ 编译成功（`BUILD SUCCESS`）+ **实机烧录成功**，串口按顺序输出三任务运行日志，舵机按「左转 45° → 右转 45° → 居中」循环动作。

## 目录结构

```
hi3861/task07_sg90_mutex/
├── 3.0_SG90_Mutex/          # U+ 参考源程序（老师提供原始版本）
│   ├── SG90.c               # 参考版：线程1(90°)/线程2(角度打印)/线程3(180°)，三线程不同优先级
│   └── BUILD.gn             # 工程编译配置
├── student_3.0_SG90_Mutex/  # 学生完成版（最终烧录并跑通）
│   ├── SG90.c               # 学生版：同优先级三线程 + 互斥锁，左转45°/右转45°/居中
│   └── BUILD.gn             # 工程编译配置
└── reference/               # 编译过程修改的参考文件
    ├── app_BUILD.gn         # applications/sample/wifi-iot/app/BUILD.gn（指向 student_3.0_SG90_Mutex）
    └── （烧录产物 output/ 见 ../../hi3861/task07_sg90_mutex/output/，本地验证用，不入库）
```

## SG90 舵机硬件原理

- **接线**：SG90 舵机信号线接 Hi3861 的 **GPIO2**（查看小车原理图确认）。
- **控制信号**：MCU 需产生 **周期 20ms** 的脉冲信号，通过高电平宽度（0.5ms ~ 2.5ms）控制舵机转动角度：

| 脉宽 | 角度 |
|---|---|
| 0.5ms | 0° |
| 1.0ms | 45° |
| 1.5ms | 90°（居中）|
| 2.0ms | 135° |
| 2.5ms | 180° |

- **软件方式**：本任务不使用硬件 PWM 定时器，而是用 GPIO 软件方式模拟——`set_angle(duty)` 让 GPIO2 先输出 `duty` 微秒高电平，再输出 `(20000 - duty)` 微秒低电平，重复 10 次即完成一次指定角度的动作。

## 核心代码说明（student_3.0_SG90_Mutex/SG90.c —— 最终跑通版）

用 **互斥锁** 实现**同优先级**三个任务的联动（U+ 任务7 验收要求）：

| 任务 | 优先级 | 行为 |
|---|---|---|
| 任务1 | 25 | 串口输出 1 次「任务1开始运行」，舵机左转 45°（1.0ms）|
| 任务3 | 25 | 在任务1 运行 3 秒后再运行，串口输出 2 次「任务3开始运行」，舵机右转 45°（2.0ms）|
| 任务2 | 25 | 在任务3 运行后立即运行，串口输出 3 次「任务2开始运行」，舵机居中 90°（1.5ms）|

- 关键 API：`osMutexNew()` 创建互斥锁，`osMutexAcquire(mutex_id, osWaitForever)` / `osMutexRelease(mutex_id)` 加锁解锁；
- 三个任务**同优先级**，用互斥锁保证同一时刻只有一个任务操作舵机（串行化对共享资源「舵机」的访问）；
- 用 `osDelay()` 错开启动时序（任务3 延时 400、任务2 延时 500），确保按「任务1 → 任务3 → 任务2」顺序执行；
- 角度映射：`engine_left_45()`=1.0ms=45°、`engine_right_45()`=2.0ms=135°、`engine_center()`=1.5ms=90°。

## 编译方法（Ubuntu 虚拟机）

```bash
cd ~/harmony/code/code-1.0
export PATH=$PATH:~/gcc_riscv32/bin:~/gn:~/ninja:~/llvm/bin
python3 build.py wifiiot
```

> 编译前需确认 `applications/sample/wifi-iot/app/BUILD.gn` 中 `features` 指向 `student_3.0_SG90_Mutex:SG90`（见 `reference/app_BUILD.gn`）。

编译产物：
- `out/wifiiot/Hi3861_wifiiot_app_burn.bin`（app）
- `out/wifiiot/Hi3861_wifiiot_app_allinone.bin`（**烧录用，多 bin 合并包**）

## 烧录方法（Windows + HiBurn）

与任务5 相同：
1. 小车用 Type-C 数据线连电脑，串口开关拨到 **3861** 端，不要电池供电、关闭电源开关；
2. HiBurn：COM 选 CH340 串口（COM9），波特率 **2000000**；
3. `Select file` 选 **`Hi3861_wifiiot_app_allinone.bin`**（用 `_burn.bin` 会报 `Wait SELoadr ACK overtime`）；
4. 勾选 `Auto burn` → 点 `Connect` → 按小车复位键（RST）；
5. 出现 `============` 及 `successful` 即成功，点 `Disconnect`；
6. 串口助手（115200）观察输出，查看舵机动作。

## 实测结果

- 编译：`python3 build.py wifiiot` → **`BUILD SUCCESS`**；
- 产物：`Hi3861_wifiiot_app_allinone.bin`（**778856 字节**，确认是学生版产物 `student_3.0_SG90_Mutex`）；
- 烧录：HiBurn 烧录成功；
- 运行现象：串口依次输出「任务1开始运行」「任务3开始运行」「任务2开始运行」，舵机按 **左转 45° → 右转 45° → 居中** 循环动作 ✅。

## 踩坑记录

- **必须烧 `allinone.bin`**：`_burn.bin` 缺少 name/FileIndex/BurnSize 元数据，HiBurn 握手失败报 `Wait SELoadr ACK overtime`。
- **烧录前关掉 Ubuntu 虚拟机**：VM 会占用串口，导致 HiBurn 连不上。
- **串口须连到 Windows 主机**而非虚拟机。
- **角度语义需按 U+ 页面确认**：「左转45°/右转45°/居中」按绝对位置理解——45°/135°/90°（对应 1.0/2.0/1.5ms）。
