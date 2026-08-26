# Hi3861 任务5：OpenHarmony 系统调试实验（Hello World）

## 任务内容

在 Hi3861 开发板上完成 **OpenHarmony 第一个程序（Hello World）**：创建工程 → 书写双任务代码 → 修改 BUILD.gn → 编译 → 烧录到开发板并运行。

**成果**：✅ 编译成功 + **实机烧录成功**，串口输出 `Hello World!` / `Hello QST!` 交替打印。

## 目录结构

```
hi3861/task05_helloworld/
├── 1.0_Hello_World/          # 任务5核心工程（桌面系统 app 样例）
│   ├── hello_world.c         # 双任务 HelloWorld 源码（thread1/thread2）
│   └── BUILD.gn              # 工程编译配置
└── reference/                # 编译过程修改的参考文件
    ├── app_BUILD.gn          # applications/sample/wifi-iot/app/BUILD.gn（加入新工程）
    ├── config.ini            # build/lite/config.ini（工具链路径已修复版）
    └── config.ini.bak        # 原始配置备份
```

## 核心代码说明（hello_world.c）

- 使用 `ohos_init.h` + `cmsis_os2.h`（OpenHarmony LiteOS 内核接口）
- `Hello_World()` 通过 `osThreadNew` 创建两个任务：
  - `thread1`：每 1 秒打印 `Hello World!`（任务1正在运行）
  - `thread2`：每 3 秒打印 `Hello QST!`（任务2正在运行）
- 通过 `APP_FEATURE_INIT(Hello_World)` 注册为系统启动特性，上电自动运行

## 编译方法（Ubuntu 虚拟机）

```bash
cd ~/harmony/code/code-1.0
export PATH=$PATH:~/gcc_riscv32/bin:~/gn:~/ninja:~/llvm/bin
python3 build.py wifiiot
```

编译产物：
- `out/wifiiot/Hi3861_wifiiot_app_burn.bin`（app）
- `out/wifiiot/Hi3861_wifiiot_app_allinone.bin`（**烧录用，多 bin 合并包**）

## 烧录方法（Windows + HiBurn）

1. 小车用 Type-C 数据线连电脑，串口开关拨到 **3861** 端，**不要电池供电，关闭电源开关**
2. HiBurn：COM 选设备管理器中的 CH340 串口（COM9），`Setting` 波特率 **2000000**
3. `Select file` 选 **`Hi3861_wifiiot_app_allinone.bin`**（如果使用 `_burn.bin` 会报 `Wait SELoadr ACK overtime`）
4. 勾选 `Auto burn` → 点 `Connect` → **及时按下小车复位键（RST）**
5. 出现 `============` 及 `successful` 即烧录成功，点 `Disconnect`
6. 串口助手（COM、115200）观察输出；烧录前确保无其他程序占用串口

## 踩坑记录

- **必须烧 `allinone.bin`**：`_burn.bin` 缺少 name/FileIndex/BurnSize 等元数据，HiBurn 握手失败报 `Wait SELoadr ACK overtime`。
- **工具链配置**：源码默认 `config.ini` 指向内置 prebuilts 工具链；本机使用独立安装的工具链（`~/gcc_riscv32`、`~/gn`、`~/ninja`），需在 `build/lite/config.ini` 的 `[ndk]` 段配置绝对路径（见 `reference/config.ini`）。
- **编译环境**：非交互 SSH 不加载 `.bashrc`，编译前需显式 `export PATH`。
