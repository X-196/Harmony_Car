# Hi3861 任务10：第一阶段综合实验（多模块联动）

## 任务内容

把前几个任务（红外、超声波、舵机、蓝牙、UART、消息队列）**综合联动**，完成综合实验1。U+ 任务10 目标：

> 1. **舵机左右旋转测距**（SG90 舵机 + HC-SR04 超声波）；
> 2. **前 15 秒**进行**红外对管寻线**；
> 3. **15 秒后**开始**蓝牙通信**；
> 4. **任务3** 运行，串口打印**消息队列**信息；
> 5. **任务1、2 交替运行**。

> 说明：U+ 任务10 **只有任务描述、无 `step` 分步指导**，本地也无 `Sum_Experiment_First` 参考；经用户确认按**描述 + 已做任务模块**综合实现。

**成果**：✅ 用一个工程综合 **舵机测距**（task1）+ **红外寻线/蓝牙**（task2，前15s 红外、后蓝牙）+ **消息队列**（task3，多发多读），多任务并用 RTOS 调度（任务1/2 交替）。

## 目录结构

```
hi3861/task10_sum_experiment/
├── 6.0_Sum_Experiment_First/   # 综合实验1 工程（模块名按 reference app_BUILD.gn 约定）
│   ├── Sum_Experiment_First.c  # 3 个任务：舵机测距 / 红外-蓝牙 / 消息队列
│   └── BUILD.gn
└── reference/
    └── app_BUILD.gn            # applications/sample/wifi-iot/app/BUILD.gn（指向 6.0_Sum_Experiment_First:Sum_Experiment_First）
```

## 综合用到的模块与接线

| 模块 | 引脚 | 来源任务 |
|---|---|---|
| SG90 舵机 | GPIO2（20ms 软 PWM，0.5~2.5ms=0°~180°） | 任务7 |
| HC-SR04 超声波 | GPIO7=TRIG、GPIO8=ECHO | 任务8 |
| 红外对管（TCRT5000） | GPIO13=TC_OUT_L、GPIO14=TC_OUT_R | 任务6 |
| 蓝牙（JDY-16）+ UART1 | GPIO0=UART1_TXD、GPIO1=UART1_RXD，9600/8N1 | 任务9 |
| 消息队列 | `osMessageQueueNew/Put/Get` | 任务9 |

## 实现说明（6.0_Sum_Experiment_First/Sum_Experiment_First.c）

`APP_FEATURE_INIT(Sum_Experiment_First)` 初始化 GPIO（舵机/超声波/红外/UART1）并创建 **3 个任务 + 1 个消息队列**：

| 任务 | 行为 |
|---|---|
| **task1（舵机测距）** | 舵机左45°(1.0ms)→测距、右135°(2.0ms)→测距、中90°(1.5ms)→测距，循环打印 `servo L/R/C dist=X.X cm` |
| **task2（红外→蓝牙）** | 用 `hi_get_tick()` 计时（100Hz）：**前 15s（1500 tick）** 读 GPIO13/14 打印 `IR trace L=x R=y`（寻线）；**15s 后** `UartRead` 收蓝牙数据并放入消息队列 |
| **task3（消息队列）** | 连续放 5 条消息 `SUM msg 0..4`，再 `osMessageQueueGet` 依次读出打印（演示消息队列） |

- 系统 tick 频率 **100Hz**（1 tick=10ms），15s = **1500 tick**；
- 多任务同优先级 25，由 RTOS 调度实现"任务1、2 交替运行"；
- 舵机用**软件 PWM**（`hi_udelay` 模拟），超声波用**忙等 ECHO 高电平**（`hi_get_us` 计时）。

## 编译方法（Ubuntu 虚拟机）

```bash
cd ~/harmony/code/code-1.0
export PATH=$PATH:~/gcc_riscv32/bin:~/gn:~/ninja:~/llvm/bin
python3 build.py wifiiot
```

> 编译前确认 `applications/sample/wifi-iot/app/BUILD.gn` 指向 `6.0_Sum_Experiment_First:Sum_Experiment_First`（见 `reference/app_BUILD.gn`）。
> 本任务用 GPIO/UART，无需 `CONFIG_I2C_SUPPORT`。

编译产物：`out/wifiiot/Hi3861_wifiiot_app_allinone.bin`（烧录用，多 bin 合并包）。

## 烧录方法（Windows + HiBurn）

1. 小车 Type-C 连电脑，串口开关拨到 **3861** 端，不要电池供电、关闭电源开关；
2. HiBurn：COM 选 CH340 串口（COM9），波特率 **2000000**；
3. `Select file` 选 **`Hi3861_wifiiot_app_allinone.bin`**；
4. 勾 `Auto burn` → `Connect` → 按小车复位键（RST）；`successful` 后 `Disconnect`。

## 实测结果

- 编译：✅ **`python3 build.py wifiiot` → `BUILD SUCCESS`**（Ubuntu 虚拟机 `192.168.124.129`，链接 `-lSum_Experiment_First`）；
- 产物：`out/wifiiot/Hi3861_wifiiot_app_allinone.bin`（**780232 字节**），已拷贝到本机 `../output/Hi3861_wifiiot_app_allinone.bin`（md5 `b33ee40c290157fc83d3ded49f16cbea`）；
- 烧录：✅ **HiBurn 实机烧录成功**（COM9、2000000、选 `allinone.bin`、Auto burn + Connect + 按复位键）；
- 现象（实测）：串口交替出现 `servo L/R/C dist=X.X cm`（舵机测距）、`IR trace L=x R=y`（前15s 寻线）、`MQ Get ...`（消息队列），15s 后蓝牙发数据出现 `BLE recv:xxx`。✅

## 踩坑记录

- **任务1/2 交替**：三任务同优先级 25，靠 RTOS 时间片调度交替；舵机/超声波里的忙等（`hi_udelay`/while 读 ECHO）会占 CPU，其余任务仍能运行（抢占式）。
- **15s 计时**：用 `hi_get_tick()`（100Hz），15s=1500 tick；不要在忙等里判断（会卡住）。
- **舵机/超声波忙等**：`servo_pulse` 与 `get_distance` 是阻塞行为，放在 task1 里不影响 task2/3（抢占调度）。
- **必须烧 `allinone.bin`**：`_burn.bin` 缺少元数据，HiBurn 握手失败报 `Wait SELoadr ACK overtime`。
- **UART1 波特率 9600**：蓝牙 JDY-16 需匹配 9600；系统日志（printf）走调试串口 115200，观察打印用系统日志串口。
