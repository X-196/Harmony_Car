# Hi3861 任务13：OpenHarmony 系统驱动实验 —— AP3216C 传感器采集光照强度（IIC）

## 任务内容

在 Hi3861 上用 **IIC**（I2C0）读取 **AP3216C 三合一环境传感器**，周期采集并打印 **光强（ALS）/ 接近（PS）/ 人体红外（IR）** 三路数据。U+ 任务13 讲解以参考代码为准（未单独列出学生变体，参考即交付实现）。

- **任务名称**：OpenHarmony 系统驱动实验 - AP3216C 传感器采集光照强度
- **知识点**：OpenHarmony 系统中 IIC 相关 API 使用以及 AP3216C 相关知识
- **重点/难点**：IIC 的相关 API 使用
- **任务内容**：熟悉 IIC 总线相关概念及特点；掌握 IIC 读/写数据原理；了解 AP3216C 硬件与接线原理；掌握通过 IIC 进行 AP3216C 数据采集及相关 API 函数使用。

**成果**：✅ 单任务循环采集：`AP3216C_Init()` 初始化后每 1s 读取一次 `AP3216C_ReadData(&ir,&als,&ps)`，串口周期打印 `人体红外传感器(ir) = ..  光强传感器(als) = ..  接近传感器(ps) = ..`。

## 目录结构

```
hi3861/task13_ap3216c/
├── 9.0_Ap3216c/                 # 任务13 工程（模块名 9.0_Ap3216c:Ap3216c）
│   ├── Ap3216c.c                # 任务创建 + 循环读 ir/als/ps 并打印
│   ├── include/hal_bsp_ap3216c.h    # AP3216C 支持包头（地址 0x3C、寄存器定义）
│   ├── src/hal_bsp_ap3216c.c        # AP3216C I2C 驱动（AP3216C_Init / AP3216C_ReadData）
│   └── BUILD.gn
└── reference/app_BUILD.gn       # applications/sample/wifi-iot/app/BUILD.gn（指向 9.0_Ap3216c:Ap3216c）
```

## AP3216C 相关

- **AP3216C**：三合一环境传感器，内部集成：
  - **ALS**（Ambient Light Sensor）数字环境光传感器 —— 采集光照强度（任务主题）
  - **PS**（Proximity Sensor）接近传感器
  - **IR LED**（Infrared Radiation LED）红外 LED —— 人体红外检测
  - 通过 **IIC 接口**与 Hi3861 相连。
- **接线**：本实验用 **I2C0**（同 OLED/SHT20：GPIO9=SCL、GPIO10=SDA），从机地址 **0x3C**（7 位地址 0x1E 左移 1 位）。
- **寄存器**：
  - 系统配置 `0x00`：`0x07` 软复位、`0x06` ALS+PS+IR 连续测量模式
  - IR 数据 `0x0A/0x0B`（10 位有效）；ALS 数据 `0x0C/0x0D`（16 位）；PS 数据 `0x0E/0x0F`（10 位 + 接近标志）
- **API**：`AP3216C_Init()`（GPIO 复用 + I2C 初始化 + 软复位 + 连续测量）、`AP3216C_ReadData(&ir,&als,&ps)`（读三路数据）。

## 核心代码说明（9.0_Ap3216c/Ap3216c.c）

- **`Task1`**：`AP3216C_Init()` → `while(1)` 循环 `AP3216C_ReadData(&ir,&als,&ps)` 读三路数据 → `printf` 打印 → `sleep(1)` 每 1s 一次；
- **`i2c_ap3216c_demo()`**：`osThreadNew` 创建任务（栈 1024、`osPriorityNormal`）；
- **启动**：`APP_FEATURE_INIT(i2c_ap3216c_demo)`。

## 编译方法（Ubuntu 虚拟机）

```bash
cd ~/harmony/code/code-1.0
export PATH=$PATH:~/gcc_riscv32/bin:~/gn:~/ninja:~/llvm/bin
python3 build.py wifiiot
```

> 编译前确认 `applications/sample/wifi-iot/app/BUILD.gn` 指向 `9.0_Ap3216c:Ap3216c`（见 `reference/app_BUILD.gn`）。
> ⚠️ 本任务用 **I2C**，需在 `build/config/usr_config.mk` 设 `CONFIG_I2C_SUPPORT=y`（任务11 已设，本机已启用）。

编译产物：`out/wifiiot/Hi3861_wifiiot_app_allinone.bin`（烧录用，多 bin 合并包）。

## 烧录方法（Windows + HiBurn）

1. 小车 Type-C 连电脑，串口开关拨到 **3861** 端，不要电池供电、关闭电源开关；
2. HiBurn：COM 选 CH340 串口（COM9），波特率 **2000000**；
3. `Select file` 选 **`Hi3861_wifiiot_app_allinone.bin`**；
4. 勾 `Auto burn` → `Connect` → 按小车复位键（RST）；`successful` 后 `Disconnect`。

## 实测结果

- 编译：✅ **`python3 build.py wifiiot` → `BUILD SUCCESS`**（Ubuntu 虚拟机 `192.168.124.129`，链接 `-lAp3216c`）；
- 产物：`out/wifiiot/Hi3861_wifiiot_app_allinone.bin`，已拷贝到本机 `../output/Hi3861_wifiiot_app_allinone.bin`（md5 `e5e79d6c6e852e108641a804c88e599d`）；
- 烧录：🕐 待实机验证（HiBurn 选 COM9、2000000、选 `allinone.bin`、Auto burn + Connect + 按复位键）；
- 现象（预期）：串口每 1s 打印一行 `人体红外传感器(ir) = x  光强传感器(als) = x  接近传感器(ps) = x`；用手遮挡/照亮传感器、手掌靠近时三路数值相应变化。

## 踩坑记录

- **I2C 从机地址**：AP3216C 手册标注 7 位地址 `0x1E`；OpenHarmony `I2cWrite/I2cRead` 的 `sensorAddr` 参数需**带读写位的 8 位形式**（`0x1E << 1 = 0x3C`），直接填 `0x1E` 会一直返回非 0 错误码。
- **读寄存器方式**：AP3216C 读数据 = 先写寄存器地址 + 重复起始（Repeated Start）读 1 字节，用 **`I2cWriteread`（注意是小写 r）**，写错成 `I2cWriteRead` 会报 `implicit declaration`（-Werror 直接编译失败）；分开 `I2cWrite`+`I2cRead` 两次传输对 AP3216C 无效。
- **必须有 `CONFIG_I2C_SUPPORT=y`**：否则 `undefined reference to hi_i2c_write/init`（与任务11/12 同，I2C 驱动默认未启用）。
- **必须烧 `allinone.bin`**：`_burn.bin` 缺少元数据，HiBurn 握手失败报 `Wait SELoadr ACK overtime`。
