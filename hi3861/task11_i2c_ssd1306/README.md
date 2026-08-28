# Hi3861 任务11：OpenHarmony 系统驱动实验 —— OLED 显示字符串（I2C + SSD1306）

## 任务内容

在 Hi3861 上用 **I2C** 驱动 **SSD1306 OLED 屏**显示字符串。U+ 任务11 的【学生需要完成内容】为：

> 掌握 OLED 显示字符相关 API，将 **"鸿蒙先锋号"** 以字符形式显示在 OLED 上。

**成果**：✅ 用 `SSD1306_Init/CLS` 初始化清屏，`SSD1306_ShowChinese24()` 将 "鸿蒙先锋号"（5 个 **24×24** 汉字）居中显示到 OLED（大字号，占满整行宽度）。

## 目录结构

```
hi3861/task11_i2c_ssd1306/
├── 7.0_I2c_Ssd1306/          # U+ 参考源程序（老师提供原始版本）
│   ├── I2c_Ssd1306.c         # 参考版：显示 "QST CAR" + 时钟（ASCII）
│   ├── include/              # SSD1306 支持包头文件（hal_bsp_ssd1306.h/.bmps/.fonts）
│   ├── src/hal_bsp_ssd1306.c # SSD1306 I2C 驱动
│   └── BUILD.gn
├── student_7.0_I2c_Ssd1306/  # 学生完成版（本任务验收实现）
│   ├── I2c_Ssd1306.c         # 学生版：显示 "鸿蒙先锋号"（中文）
│   ├── include/              # 支持包 + 新增 hal_bsp_ssd1306_fonts_cn.h（16×16 中文字库）
│   │   └── hal_bsp_ssd1306_fonts_cn.h   # 鸿/蒙/先/锋/号 的 16×16 点阵 + UTF-8 匹配表
│   ├── src/hal_bsp_ssd1306.c # 支持包驱动 + 新增 SSD1306_ShowChinese()
│   └── BUILD.gn
└── reference/app_BUILD.gn    # applications/sample/wifi-iot/app/BUILD.gn（指向 student_7.0_I2c_Ssd1306）
```

## OLED / IIC 原理

- **OLED**（Organic Light Emitting Diode，有机发光显示器）：更轻薄、功耗低、亮度高、可显示纯黑、可弯曲。本机为 **SSD1306**（128×64，I2C 接口，从机地址 **0x78**）。
- **IIC（I²C）总线**：PHILIPS 公司开发的两线式串行总线，**半双工**。两根线：**SDA**（双向数据）、**SCL**（时钟，由主控产生）；并联设备各有唯一地址。
- **接线**（由 `SSD1306_Init()` 可知，本工程用总线 **I2C0**）：
  - **GPIO9 = I2C0_SCL**（时钟）
  - **GPIO10 = I2C0_SDA**（数据）
  - 波特率 400000（400kbps）

## IIC 相关 API

| 函数 | 功能 | 定义 |
|---|---|---|
| `I2cInit` | 用指定频率初始化 I2C 设备 | `unsigned int I2cInit(WifiIotI2cIdx id, unsigned int baudrate)` |
| `I2cWrite` | 写数据到设备 | `unsigned int I2cWrite(WifiIotI2cIdx id, unsigned short deviceAddr, const WifiIotI2cData *i2cData)` |
| `I2cRead` | 从设备读数据 | `unsigned int I2cRead(WifiIotI2cIdx id, unsigned short deviceAddr, const WifiIotI2cData *i2cData)` |
| `I2cSetBaudrate` | 设置 I2C 通信频率 | `unsigned int I2cSetBaudrate(WifiIotI2cIdx id, unsigned int baudrate)` |

## 核心代码说明（student_7.0_I2c_Ssd1306/I2c_Ssd1306.c —— 学生完成版）

与参考版（ASCII 时钟）不同，学生版改为显示中文"鸿蒙先锋号"（**大字号**）：

- **初始化**：`SSD1306_Init()` 配置 GPIO9/10 复用为 I2C0、初始化 SSD1306；`SSD1306_CLS()` 清屏；
- **显示中文**：`SSD1306_ShowChinese24(4, 2, (uint8_t*)"鸿蒙先锋号")` —— 5 个 **24×24** 汉字共 120px 宽，占满整行宽度（128-120)/2 = 4 居中，从第 2 页开始（垂直较大）；
- **新增中文字库** `include/hal_bsp_ssd1306_fonts_cn.h`：`HZ24Char[][3]` 存各字 UTF-8 编码、`HZ24[][72]` 存 24×24 点阵（每个字 72 字节：列 0~23=页0(行0-7)、24~47=页1(行8-15)、48~71=页2(行16-23)，按列存储）；
- **新增显示函数** `src/hal_bsp_ssd1306.c` 里 `SSD1306_ShowChinese24()`：按 UTF-8 3 字节匹配 `HZ24Char`，命中后用 `SSD1306_SetPos` + `SSD1306_WiteData` 逐列写入三个页（页寻址模式下列自动前进）。

> 说明：原支持包字库 `F6x8/F8X16` 只有 ASCII，无法显示中文；学生版在 BSP 中扩展了 24×24 中文字库与 `SSD1306_ShowChinese24()`。OLED 为 128×64 点阵屏，字号越大越清晰。

## 编译方法（Ubuntu 虚拟机）

```bash
cd ~/harmony/code/code-1.0
export PATH=$PATH:~/gcc_riscv32/bin:~/gn:~/ninja:~/llvm/bin
python3 build.py wifiiot
```

> 编译前确认 `applications/sample/wifi-iot/app/BUILD.gn` 中 `features` 指向 `student_7.0_I2c_Ssd1306:I2c_Ssd1306`（见 `reference/app_BUILD.gn`）。

> ⚠️ **关键：必须先启用 I2C 驱动**，否则链接报 `undefined reference to hi_i2c_write/init/set_baudrate`。在虚拟机 OpenHarmony 源码里把
> `vendor/hisi/hi3861/hi3861/build/config/usr_config.mk` 中的 `# CONFIG_I2C_SUPPORT is not set` 改为 `CONFIG_I2C_SUPPORT=y`
> （`build/make_scripts/config.mk` 与 `platform/drivers/module_config.mk` 都据此把 I2C 驱动编进 `libdrv`）。

编译产物：`out/wifiiot/Hi3861_wifiiot_app_allinone.bin`（烧录用，多 bin 合并包）。

## 烧录方法（Windows + HiBurn）

与任务8 相同：
1. 小车 Type-C 连电脑，串口开关拨到 **3861** 端，不要电池供电、关闭电源开关；
2. HiBurn：COM 选 CH340 串口（COM9），波特率 **2000000**；
3. `Select file` 选 **`Hi3861_wifiiot_app_allinone.bin`**；
4. 勾 `Auto burn` → 点 `Connect` → 按小车复位键（RST）；出现 `successful` 即成功，点 `Disconnect`。

## 实测结果

- 编译：✅ **`python3 build.py wifiiot` → `BUILD SUCCESS`**（Ubuntu 虚拟机 `192.168.124.129`，student 版已编译链接 `-lI2c_Ssd1306`）；
- 产物：`out/wifiiot/Hi3861_wifiiot_app_allinone.bin`（**780968 字节**），已拷贝到本机 `../output/Hi3861_wifiiot_app_allinone.bin`（md5 `e1aebc1934a7949af978d2271b32c412`）；
- 烧录：✅ **HiBurn 实机烧录成功**（COM9、2000000、选 `allinone.bin`、Auto burn + Connect + 按复位键）；
- 现象（实测）：OLED 上居中显示 **"鸿蒙先锋号"** 五个汉字（**24×24 大字号，占满整行宽度**）。✅

## 踩坑记录

- **必须烧 `allinone.bin`**：`_burn.bin` 缺少元数据，HiBurn 握手失败报 `Wait SELoadr ACK overtime`。
- **首次 I2C 编译报 `undefined reference to hi_i2c_write/init/set_baudrate`**：因为 I2C 驱动默认没启用——需在 `vendor/hisi/hi3861/hi3861/build/config/usr_config.mk` 设 `CONFIG_I2C_SUPPORT=y`（本任务11 首次用到 I2C，前几个 GPIO 任务无需）；改后需重新构建。
- **OLED 显示中文需中文字库**：参考版字库只有 ASCII（`F6x8/F8X16`），`SSD1306_ShowStr` 对中文无效；学生版新增 16×16 中文字库 + `SSD1306_ShowChinese()`。
- **中文字库编码**：`HZ16Char` 存 UTF-8 三字节，`SSD1306_ShowChinese()` 用 `str[0..2]` 匹配；若直接传 GBK/GB2312 编码会匹配不到。
- **I2C 接线**：GPIO9=SCL、GPIO10=SDA（I2C0，从机地址 `0x78`）；若显示空白，检查接线与地址。
