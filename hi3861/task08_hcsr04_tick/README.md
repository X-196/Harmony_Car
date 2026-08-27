# Hi3861 任务8：OpenHarmony 系统驱动实验 —— GPIO 驱动超声波（HC-SR04 + Tick）

## 任务内容

在 Hi3861 开发板上用 **GPIO 驱动 HC-SR04 超声波测距模块**，结合 **系统 Tick** 完成定时测距。U+ 任务8 的【学生需要完成内容】为：

> 掌握超声波相关的信息，实现创建 **2 个软件定时器**：
> - 定时器1：控制超声波 **3 秒**间隔测一次距离；
> - 定时器2：控制打印当前 **tick 值**。

**成果**：✅ 用 `osTimerNew` 创建 2 个软件定时器，定时器1 每 3s 触发一次 `GetDistance()` 测距并串口打印距离，定时器2 每 1s 打印当前 `hi_get_tick()`；编译成功（`BUILD SUCCESS`）。

## 目录结构

```
hi3861/task08_hcsr04_tick/
├── 4.0_Hcsr04_Tick/          # U+ 参考源程序（老师提供原始版本）
│   ├── Hcsr04.c              # 参考版：while(1) + osDelay(200) 循环测距
│   └── BUILD.gn              # 工程编译配置
├── student_4.0_Hcsr04_Tick/  # 学生完成版（本任务验收实现）
│   ├── Hcsr04.c              # 学生版：2 个软件定时器（测距3s + 打印tick 1s）
│   └── BUILD.gn              # 工程编译配置
└── reference/                # 编译过程修改的参考文件
    └── app_BUILD.gn          # applications/sample/wifi-iot/app/BUILD.gn（指向 student_4.0_Hcsr04_Tick）
```

## HC-SR04 超声波测距原理

- **接线**：HC-SR04 模块通过 **GPIO7（TRIG）** 和 **GPIO8（ECHO）** 连接 Hi3861（查看小车原理图确认）。
- **测距原理**：超声波发射装置发出超声波并开始计时，声波在空气中以 **340m/s** 传播，碰到障碍物后反射回接收器，停止计时得到时间 `t`，距离 `s = 340 * t / 2`。
- **驱动流程**：
  1. 单片机 I/O 口 **TRIG** 触发：给至少 **10us** 的高电平；
  2. 模块自动发送 **8 个 40kHz** 方波，自动检测是否有信号返回；
  3. 有信号返回时，模块通过 **ECHO** 输出一个高电平，高电平持续时间即为波从发射到返回的时间；
  4. 测距距离 `= (高电平时间 × 340m/s) / 2`。代码中换算为厘米：`distance = time_us * 0.034 / 2`。

## Tick（系统滴答）相关

- **概念**：系统滴答（SysTick，也叫时钟节拍/系统心跳），是操作系统以固定频率中断、供任务切换与所有与时间有关事件（线程延时、时间片轮转、定时器超时）使用的时钟源。
- **支持功能**：① 任务轮转 ② 软件定时器创建 ③ 获取系统当前时钟。
- **相关 API**（`hi_time.h`）：

| 函数 | 功能 | 返回 |
|---|---|---|
| `hi_get_real_time()` | 获取系统实时时间（单位 s） | 系统实时时间 |
| `hi_set_real_time(seconds)` | 将系统实时时间设为该值 | 0 成功 / 非0 失败 |
| `hi_get_tick()` | 获取系统 tick 值（32bit） | 系统 tick 值 |

> 本工程系统 tick 频率为 **100Hz**（1 tick = 10ms），因此 `osDelay(200)` = 2s、3s = **300 ticks**、1s = **100 ticks**（与参考版 `osDelay(200)` 注释「测量周期2s」一致）。

## 核心代码说明（student_4.0_Hcsr04_Tick/Hcsr04.c —— 学生完成版）

与参考版（`while(1) + osDelay(200)` 循环）不同，学生版按要求改为 **2 个软件定时器**：

| 定时器 | 创建 | 周期 | 回调行为 |
|---|---|---|---|
| `timer_measure_id` | `osTimerNew(TimerMeasureCallback, osTimerPeriodic, ...)` | 3s（300 ticks） | 调 `GetDistance()` 测距并 `printf("distance is %.1f (cm)")` |
| `timer_tick_id` | `osTimerNew(TimerTickCallback, osTimerPeriodic, ...)` | 1s（100 ticks） | `printf("tick value is %u", hi_get_tick())` |

- 关键 API：`osTimerNew()` 创建软件定时器、`osTimerStart(timer_id, ticks)` 启动（ticks 为单位，100Hz 下 300=3s、100=1s）；
- `GetDistance()`：`hi_io_set_func`/`GpioSetDir` 配 GPIO7(TRIG,输出)/GPIO8(ECHO,输入)；GPIO7 给出 20us 高电平触发；忙等 ECHO 高电平用 `hi_get_us()` 计时，得到高电平持续时间 `time`，再 `distance = time * 0.034 / 2`；
- 用 `APP_FEATURE_INIT(Hcsr04)` 启动任务，函数内 `WatchDogDisable()` 关闭看门狗后创建并启动 2 个定时器。

## 编译方法（Ubuntu 虚拟机）

```bash
cd ~/harmony/code/code-1.0
export PATH=$PATH:~/gcc_riscv32/bin:~/gn:~/ninja:~/llvm/bin
python3 build.py wifiiot
```

> 编译前需确认 `applications/sample/wifi-iot/app/BUILD.gn` 中 `features` 指向 `student_4.0_Hcsr04_Tick:Hcsr04`（见 `reference/app_BUILD.gn`）。

编译产物：
- `out/wifiiot/Hi3861_wifiiot_app_burn.bin`（app）
- `out/wifiiot/Hi3861_wifiiot_app_allinone.bin`（**烧录用，多 bin 合并包**）

## 烧录方法（Windows + HiBurn）

与任务7 相同：
1. 小车用 Type-C 数据线连电脑，串口开关拨到 **3861** 端，不要电池供电、关闭电源开关；
2. HiBurn：COM 选 CH340 串口（COM9），波特率 **2000000**；
3. `Select file` 选 **`Hi3861_wifiiot_app_allinone.bin`**；
4. 勾选 `Auto burn` → 点 `Connect` → 按小车复位键（RST）；
5. 出现 `============` 及 `successful` 即成功，点 `Disconnect`；
6. 串口助手（115200）观察输出。

## 实测结果

- 编译：✅ **`python3 build.py wifiiot` → `BUILD SUCCESS`**（Ubuntu 虚拟机 `192.168.124.129`，student 版已确认编译并链接 `-lHcsr04`）；
- 产物：`out/wifiiot/Hi3861_wifiiot_app_allinone.bin`（**778472 字节**）；
- 服务端产物已拷贝到本机 `../output/Hi3861_wifiiot_app_allinone.bin`（md5 `132839df795e177894da30601b0d40a4`，供 HiBurn 烧录）；
- 烧录：🕐 待实机验证（HiBurn 选 COM9、2000000、选 `allinone.bin`、Auto burn + Connect + 按复位键进入下载模式）；
- 运行现象（预期）：串口每 3s 打印一次 `distance is X.X (cm)`，每 1s 打印一次 `tick value is N`。

## 踩坑记录

- **必须烧 `allinone.bin`**：`_burn.bin` 缺少元数据，HiBurn 握手失败报 `Wait SELoadr ACK overtime`。
- **烧录前关掉 Ubuntu 虚拟机**：VM 会占用串口，导致 HiBurn 连不上。
- **tick 周期按 100Hz 计算**：`osTimerStart` 参数单位是 tick（10ms），3s=300、1s=100，不要直接填 3000/1000（那是 ms）。
- **定时器回调勿阻塞**：`GetDistance()` 内为忙等 ECHO 高电平，本任务按参考版原样使用；实际应尽量缩短或移出回调。
