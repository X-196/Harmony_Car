# Hi3861 任务12：OpenHarmony 系统驱动实验 —— SHT20 温湿度传感器采集（信号量 + IIC）

## 任务内容

在 Hi3861 上用 **IIC**（I2C0）读取 **SHT20 温湿度传感器**，并用 **信号量（osSemaphore）** 实现任务间**同步**。U+ 任务12 讲解以参考代码为准（未单独列出学生变体，参考即交付实现）。

- **知识点**：OpenHarmony 中信号量 API 使用 + SHT20 相关知识
- **重点/难点**：IIC、信号量的相关 API 使用
- **任务内容**：熟悉信号量概念/运行机制；掌握信号量做任务同步或互斥访问；掌握信号量创建/应用 API；掌握 IIC 读写原理，了解 SHT20 硬件与接线，通过 IIC 采集 SHT20 数据。

**成果**：✅ 用**信号量同步**：thread1 周期释放信号量两次 → thread2（IIC 读 SHT20 温湿度）与 thread3（同步打印）同时获信号量执行；串口周期打印 `temperature = ..  humidity = ..`。

## 目录结构

```
hi3861/task12_sht20/
├── 8.0_Sht20/              # 任务12 工程（模块名 8.0_Sht20:Sht20）
│   ├── Sht20.c             # 信号量同步 + IIC 读 SHT20 温湿度
│   ├── include/hal_bsp_sht20.h   # SHT20 支持包头（I2C 地址 0x80、I2C0）
│   ├── src/hal_bsp_sht20.c       # SHT20 I2C 驱动（SHT20_Init / SHT20_ReadData）
│   └── BUILD.gn
└── reference/app_BUILD.gn  # applications/sample/wifi-iot/app/BUILD.gn（指向 8.0_Sht20:Sht20）
```

## SHT20 相关

- **SHT20**：瑞士 Sensirion 温湿度传感器，精度 ±3%RH / ±0.3℃，**IIC 数字输出**，DFN 封装，寿命 15 年，应用广泛。
- **接线**：本实验用 **I2C0**（同 OLED：GPIO9=SCL、GPIO10=SDA），从机地址 **0x80**。
- **API**：`SHT20_Init()`（初始化）、`SHT20_ReadData(&temp, &humi)`（读温湿度）。

## 信号量（Semaphore）相关

- **概念**：实现任务间通信的机制，可用于**任务同步**或**共享资源的互斥访问**。计数值表示剩余可用资源数（0=不可获取，正值=可获取）。
- **运行机制**：申请成功计数值递减，失败则挂起在等待队列；释放后唤醒等待任务。
- **同步 vs 互斥**：
  - 互斥：创建后计数满，取信号量变空，保证临界资源独占。
  - 同步：创建后置空，任务1 取信号量阻塞，任务2 满足条件后释放 → 任务1 得以运行（两任务同步）。

| API | 功能 |
|---|---|
| `osSemaphoreNew(max_count, initial_count, attr)` | 创建信号量（不能在中断调用） |
| `osSemaphoreAcquire(id, timeout)` | 获取信号量（timeout=0 可从中断调用） |
| `osSemaphoreRelease(id)` | 释放信号量（可从中断调用） |
| `osSemaphoreDelete(id)` | 删除信号量 |

## 核心代码说明（8.0_Sht20/Sht20.c）

- **`i2c_sht20_demo()`**：创建 thread1/thread2/thread3 + `sem1 = osSemaphoreNew(4, 0, NULL)`（初始值 0、最大值 4）；
- **thread1**：每 3s **释放信号量两次**（`osSemaphoreRelease` 两次），让 thread2、thread3 都能获得信号量；
- **thread2**：`SHT20_Init()` 后循环：`osSemaphoreAcquire(sem1, osWaitForever)` 等待 → `SHT20_ReadData(&t,&h)` 读温湿度 → printf；
- **thread3**：循环 `osSemaphoreAcquire(sem1, osWaitForever)` → 打印（与 thread2 同步）；
- **启动**：`APP_FEATURE_INIT(i2c_sht20_demo)`。

## 编译方法（Ubuntu 虚拟机）

```bash
cd ~/harmony/code/code-1.0
export PATH=$PATH:~/gcc_riscv32/bin:~/gn:~/ninja:~/llvm/bin
python3 build.py wifiiot
```

> 编译前确认 `applications/sample/wifi-iot/app/BUILD.gn` 指向 `8.0_Sht20:Sht20`（见 `reference/app_BUILD.gn`）。
> ⚠️ 本任务用 **I2C**，需在 `build/config/usr_config.mk` 设 `CONFIG_I2C_SUPPORT=y`（任务11 已设，本机已启用）。

编译产物：`out/wifiiot/Hi3861_wifiiot_app_allinone.bin`（烧录用，多 bin 合并包）。

## 烧录方法（Windows + HiBurn）

1. 小车 Type-C 连电脑，串口开关拨到 **3861** 端，不要电池供电、关闭电源开关；
2. HiBurn：COM 选 CH340 串口（COM9），波特率 **2000000**；
3. `Select file` 选 **`Hi3861_wifiiot_app_allinone.bin`**；
4. 勾 `Auto burn` → `Connect` → 按小车复位键（RST）；`successful` 后 `Disconnect`。

## 实测结果

- 编译：✅ **`python3 build.py wifiiot` → `BUILD SUCCESS`**（Ubuntu 虚拟机 `192.168.124.129`，链接 `-lSht20`）；
- 产物：`out/wifiiot/Hi3861_wifiiot_app_allinone.bin`，已拷贝到本机 `../output/Hi3861_wifiiot_app_allinone.bin`（md5 `66cfa5ff178f47f484aa1eb61695f07c`）；
- 烧录：🕐 待实机验证（HiBurn 选 COM9、2000000、选 `allinone.bin`、Auto burn + Connect + 按复位键）；
- 现象（预期）：串口周期打印 `Thread1 release sem!` → `temperature = XX.XX  humidity = XX.XX` + `Thread2 get sem!`、`Thread3 get sem!`（信号量同步 + SHT20 温湿度）。

## 踩坑记录

- **必须有 `CONFIG_I2C_SUPPORT=y`**：否则 `undefined reference to hi_i2c_write/init`（与任务11同，I2C 驱动默认未启用）。
- **信号量初始值为 0（同步用）**：`osSemaphoreNew(4, 0, NULL)` —— 初始 0（空），thread2/3 取信号量先阻塞，thread1 释放后它们才同步执行；若做**互斥**则初始值应为最大（满）。
- **释放两次 vs 一次**：释放两次 → thread2/3 都能同时获得信号量（同步执行）；只释放一次 → 两者交替运行。按 u+ 第7课设计释放两次。
- **必须烧 `allinone.bin`**：`_burn.bin` 缺少元数据，HiBurn 握手失败报 `Wait SELoadr ACK overtime`。
