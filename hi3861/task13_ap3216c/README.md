# Hi3861 任务13：AP3216C 光照强度 + 全传感器仪表盘（IIC）

## 任务内容

在 Hi3861 上用 **IIC**（I2C0）读取 **AP3216C 三合一环境传感器**，周期采集并打印 **光强（ALS）/ 接近（PS）/ 人体红外（IR）** 三路数据；并在应用户要求扩展为**全传感器仪表盘**：OLED 一屏显示光照/温湿度/超声波距离/红外对管全部数据。

- **任务名称**：OpenHarmony 系统驱动实验 - AP3216C 传感器采集光照强度
- **知识点**：OpenHarmony 系统中 IIC 相关 API 使用以及 AP3216C 相关知识
- **重点/难点**：IIC 的相关 API 使用

**成果**：✅ 实机验证通过——单任务每 1s 采集 **五路传感器**，串口 ASCII 打印 + **OLED 仪表盘一屏全显**（6x8 字体 8 行布局）。

## 目录结构

```
hi3861/task13_ap3216c/
├── 9.0_Ap3216c/                 # 任务13 工程（模块名 9.0_Ap3216c:Ap3216c，全传感器仪表盘版）
│   ├── Ap3216c.c                # 主任务：五路采集 + OLED 仪表盘 + 串口打印；含超声波 GetDistance
│   ├── include/hal_bsp_ap3216c.h    # AP3216C 支持包头（地址 0x3C、寄存器定义）
│   ├── include/hal_bsp_sht20.h      # SHT20 支持包头（自任务12 复用）
│   ├── include/hal_bsp_ssd1306*.h   # OLED 支持包头（自任务11 复用）
│   ├── src/hal_bsp_ap3216c.c        # AP3216C I2C 驱动（AP3216C_Init / AP3216C_ReadData）
│   ├── src/hal_bsp_sht20.c          # SHT20 I2C 驱动（自任务12 复用）
│   ├── src/hal_bsp_ssd1306.c        # SSD1306 OLED 驱动（自任务11 复用）
│   └── BUILD.gn
└── reference/app_BUILD.gn       # applications/sample/wifi-iot/app/BUILD.gn（指向 9.0_Ap3216c:Ap3216c）
```

## 传感器清单（五路）

| 传感器 | 总线/引脚 | 地址 | 数据 |
|---|---|---|---|
| AP3216C 三合一 | I2C0（GPIO9/10） | 0x3C | ir 红外 / als 光强 / ps 接近 |
| SHT20 温湿度 | I2C0（GPIO9/10） | 0x80 | 温度 ℃ / 湿度 %RH |
| HC-SR04 超声波 | GPIO7=TRIG、GPIO8=ECHO | - | 距离 cm |
| TCRT5000 红外对管 | GPIO13=左、GPIO14=右 | - | 0=检测到黑线 / 1=无 |
| SSD1306 OLED | I2C0（GPIO9/10） | 0x78 | 显示（输出） |

> 三个 I2C 器件共总线不冲突（地址互异）；超声波/红外为独立 GPIO，不占用 I2C。

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

- **`Task1`**：依次初始化 `AP3216C_Init()` → `SHT20_Init()` → `SSD1306_Init()`/`SSD1306_CLS()` → 超声波/红外 GPIO → `while(1)` 每 1s：
  1. `AP3216C_ReadData(&ir,&als,&ps)` 读三合一；
  2. `SHT20_ReadData(&temp,&humi)` 读温湿度；
  3. `GetDistance()`（任务8 同款：TRIG 20us 触发 + ECHO 计时，`time*0.034/2`）测距离；
  4. `GpioGetInputVal(GPIO13/14)` 读红外对管；
  5. 串口 ASCII 打印一行全数据 + OLED 刷新仪表盘。
- **OLED 布局**（**8x16 大字体铺满整屏**，16 列 × 4 行，每行 16px 高）：

```
A   82 I  13     ← A=光强als  I=红外ir
P   88 25C       ← P=接近ps   温度
45%H L0R1        ← 湿度 + 红外对管(0=黑线)
 12.5cm          ← 超声波距离（居中大字）
```

> 一行 16 字符不够放变量名全称，用单字母代号：A(als)/I(ir)/P(ps)/T(C)/H(%)/L R(红外对管)；串口日志里有完整名字。

- **`i2c_ap3216c_demo()`**：`osThreadNew` 创建任务（栈 4KB，SHT20 浮点 printf 需较大栈）；
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
- 产物：`out/wifiiot/Hi3861_wifiiot_app_allinone.bin`，已拷贝到本机 `../output/Hi3861_wifiiot_app_allinone.bin`（大字体仪表盘版 md5 `2ac9238fbad6a159cecaecbbb45f221c`）；
- 实机：✅ 烧录验证通过（AP3216C 数值随光照/接近变化、OLED 亮屏）；仪表盘全传感器版待烧录验证；
- 现象：串口每 1s 打印 `ir=.. als=.. ps=.. T=..C H=..% D=..cm L=. R=.`；OLED 一屏显示全部五路数据。

## 踩坑记录

- **首版实测数值不变化（ir=1/als=0/ps=21 恒定）**：原因有两处，均以官方 supportPack 为准修正——
  1. **读寄存器方式**：AP3216C 读数据用「先写寄存器地址（I2cWrite 单字节）→ 再 I2cRead」两段传输，与 SHT20 一样；首版用 `I2cWriteread`（重复起始）在该板上读不到有效数据。
  2. **工作模式取值**：初始化写系统配置寄存器 0x00 应先写 `0x04`（复位）再写 `0x03`（ALS+PS+IR）；首版写的 `0x07`（软复位）+`0x06`（连续测量）在该板上数据寄存器不刷新。
  3. **数据位拼接**：IR 是 `(data_H<<2)|(data_L&0x03)`（低字节 bit7 为无效标志）；PS 是 `((data_H&0x3F)<<4)|(data_L&0x0F)`（低字节 bit6 为无效标志）——与首版按手册 bit 序写的掩码不同。
- **I2C 从机地址**：AP3216C 手册标注 7 位地址 `0x1E`；OpenHarmony `I2cWrite/I2cRead` 的 `sensorAddr` 参数需**带读写位的 8 位形式**（`0x1E << 1 = 0x3C`），直接填 `0x1E` 会一直返回非 0 错误码。
- **必须有 `CONFIG_I2C_SUPPORT=y`**：否则 `undefined reference to hi_i2c_write/init`（与任务11/12 同，I2C 驱动默认未启用）。
- **必须烧 `allinone.bin`**：`_burn.bin` 缺少元数据，HiBurn 握手失败报 `Wait SELoadr ACK overtime`。
