# Hi3861 任务9：OpenHarmony 系统驱动实验 —— UART 实现信息收发（消息队列 + 蓝牙）

## 任务内容

在 Hi3861 上用 **UART1** 实现信息收发，并结合 **消息队列（osMessageQueue）** 实现任务间通信，蓝牙（JDY-16）透传。U+ 任务9【学生需要完成内容】为：

> 尝试编写在消息队列中**发送多个消息，并依次读出**的收发信息程序。

**成果**：✅ 用 `osMessageQueueNew/Put/Get` 在消息队列中发送**多条消息**并由 UART 任务**依次读出**（FIFO），同时保留 UART 收发（串口发送 + 蓝牙透传接收）。

## 目录结构

```
hi3861/task09_uart_ble/
├── 5.0_Uart_BLE/            # U+ 参考源程序（老师提供原始版本）
│   ├── Uart.c               # 参考版：串口收发 + 消息队列（单条收发）
│   └── BUILD.gn
├── student_5.0_Uart_BLE/    # 学生完成版（本任务验收实现）
│   ├── Uart.c               # 学生版：消息队列发送多条消息并依次读出
│   └── BUILD.gn
└── reference/app_BUILD.gn   # applications/sample/wifi-iot/app/BUILD.gn（指向 student_5.0_Uart_BLE:Uart）
```

## UART / 串口相关

- **串口原理**：串口按位（bit）发送/接收字节；双向通信至少需 **RX**（接收数据，GPIO1）和 **TX**（发送数据，GPIO0）。
- **本实验**：串口 **UART1**，`GPIO0 = UART1_TXD`、`GPIO1 = UART1_RXD`，波特率 9600（8N1）。
- **UART API**：`UartInit(id, param, extraAttr)`、`UartRead(id, data, len)`、`UartWrite(id, data, len)`、`UartSetFlowCtrl(id, flow)`。

## 消息队列相关

- **概念**：任务间通信的数据结构，先进先出（FIFO），支持异步读写与超时；从队列读空时挂起任务，有消息时唤醒。
- **API**：

| 函数 | 功能 |
|---|---|
| `osMessageQueueNew(count, size, attr)` | 创建消息队列（`count` 消息数、`size` 单消息字节数） |
| `osMessageQueuePut(id, msg, prio, timeout)` | 发送消息 |
| `osMessageQueueGet(id, msg, prio, timeout)` | 获取消息（`osWaitForever` 阻塞等待） |
| `osMessageQueueDelete(id)` | 删除消息队列 |

## 核心代码说明（student_5.0_Uart_BLE/Uart.c —— 学生完成版）

与参考版（单条收发）不同，学生版改为**在消息队列中发送多条消息并依次读出**：

- **创建消息队列**：`osMessageQueueNew(MSGQUEUE_OBJECTS, sizeof(MSGQUEUE_OBJ_t), NULL)`（每条消息 = 一个 `MSGQUEUE_OBJ_t` 结构体，含 `Buf` 指针 + `Idx`）；
- **生产者 `thread2`**：先读串口（`UartRead`，蓝牙/上位机发来）放入队列一条；随后**循环放入 `MSG_NUM=5` 条消息**（`"QST msg 0..4"`）到队列，演示多条消息；
- **消费者 `UART_Task`**：`UartWrite` 发送 `"Hello, QST!"`，然后 `osMessageQueueGet(..., osWaitForever)` 依次取出队列消息打印 —— **FIFO 顺序**，`Get msg Idx=0 : QST msg 0`、`Idx=1 : QST msg 1`……
- **启动**：`APP_FEATURE_INIT(UART_ExampleEntry)` 创建消息队列 + UART_Task + thread2。

## 编译方法（Ubuntu 虚拟机）

```bash
cd ~/harmony/code/code-1.0
export PATH=$PATH:~/gcc_riscv32/bin:~/gn:~/ninja:~/llvm/bin
python3 build.py wifiiot
```

> 编译前确认 `applications/sample/wifi-iot/app/BUILD.gn` 中 `features` 指向 `student_5.0_Uart_BLE:Uart`（见 `reference/app_BUILD.gn`）。
> 本任务用 UART，无需 `CONFIG_I2C_SUPPORT`。

编译产物：`out/wifiiot/Hi3861_wifiiot_app_allinone.bin`（烧录用，多 bin 合并包）。

## 烧录方法（Windows + HiBurn）

与任务8 相同：
1. 小车 Type-C 连电脑，串口开关拨到 **3861** 端，不要电池供电、关闭电源开关；
2. HiBurn：COM 选 CH340 串口（COM9），波特率 **2000000**；
3. `Select file` 选 **`Hi3861_wifiiot_app_allinone.bin`**；
4. 勾 `Auto burn` → `Connect` → 按小车复位键（RST）；`successful` 后 `Disconnect`。

## 实测结果

- 编译：✅ **`python3 build.py wifiiot` → `BUILD SUCCESS`**（Ubuntu 虚拟机 `192.168.124.129`，student 版已编译链接 `-lUart`）；
- 产物：`out/wifiiot/Hi3861_wifiiot_app_allinone.bin`（**779656 字节**），已拷贝到本机 `../output/Hi3861_wifiiot_app_allinone.bin`（md5 `95b7add44c8965f70344d89643730982`）；
- 烧录：🕐 待实机验证（HiBurn 选 COM9、2000000、选 `allinone.bin`、Auto burn + Connect + 按复位键）；
- 现象（预期）：串口助手（115200）每隔一段打印一次 `*************UART_example**************` + `Get msg Idx=0 : QST msg 0`…`Idx=4 : QST msg 4`（消息队列依次读出多个消息）；用手机蓝牙 app 连接 JDY-16 发送字符串，可看到 `Uart1 read data:xxx` 并被放入队列读出。

## 踩坑记录

- **串口 1 引脚**：GPIO0=UART1_TXD、GPIO1=UART1_RXD（`IoSetFunc` 复用）；蓝牙模块 JDY-16 透传接到 UART1。
- **消息队列消息大小**：`osMessageQueueNew` 的 `size`＝单消息节点字节数；用 `sizeof(MSGQUEUE_OBJ_t)` 即可（学生版），参考版用固定 100。
- **`osMessageQueueGet` 阻塞**：用 `osWaitForever` 会一直阻塞等待；要非阻塞用 `timeout=0`。
- **必须烧 `allinone.bin`**：`_burn.bin` 缺少元数据，HiBurn 握手失败报 `Wait SELoadr ACK overtime`。
- **串口助手波特率**：板上 UART1 是 9600，但**系统日志（printf）走另一个串口(HiBurn/调试 115200)**；观察消息队列打印请用系统日志串口，蓝牙透传数据走 UART1。
