
# 任务17：双探头黑线 DFS 岔路走迷宫（AA 双向协议 + STATUS 遥测）

本任务让小车沿黑线从起点走迷宫到终点。遇到左/右岔路时优先尝试左侧（DFS 深度优先，左优先）；如果该分支进入死胡同，小车倒车返回上一个路口，再尝试另一分支。整个迷宫只有**两个岔路口**；**不需要识别终点**，走完（栈耗尽/超时）即停。

## 目录结构

```text
task17_line_dfs/
├── 15.0_Line_DFS/
│   ├── BUILD.gn
│   └── line_dfs.c
├── reference/
│   └── app_BUILD.gn
└── README.md
```

## 双核协议（AA | CMD | LEN | PAYLOAD | CHECK）

参考 ADVOT/STM-harmonyos-car 的协议。`CHECK = (CMD + LEN + ΣPAYLOAD) & 0xFF`（模 256 和，不含 SOF），总帧长 = `LEN + 4`。

| 命令 | CMD | LEN | PAYLOAD | 说明 |
|---|---|---:|---|---|
| SET_SPEED | `0x01` | `4` | 左轮 int16LE \| 右轮 int16LE | 左右轮目标速度（单位 `0.01圈/s`，即 80=0.8圈/s），钳位 ±150 |
| STOP | `0x02` | `0` | — | 停车 |
| PING | `0x03` | `0` | — | 心跳 |
| GET_STATUS | `0x04` | `0` | — | 请求遥测（Hi3861 每 `STATUS_POLL_TICKS`=100ms 发一次） |
| STATUS | `0x82` | `13` | odoL s32LE \| odoR s32LE \| spdL s16LE \| spdR s16LE \| flags u8 | STM32 回传：左右轮累计里程(脉冲) + 每 100ms 实测速度 + 运动保持位 |

- Hi3861 `GPIO11` 软UART TX @9600 发 AA 帧；`GPIO12` 软UART RX @9600 收 STATUS。传输层为软件位倒 UART（`SOFTUART_BIT_US=104`）。
- STM32 侧为 `stm32/8_双核协议控制`（USART1 PA9/PA10 @9600），需**重烧**为本次 AA 双向版本；`control_system.c` 未编入工程，实际控制环在 `USER/pid.c` 的 `System_Control`。
- ⚠️ 本次改协议后，之前用 V2 `FC|02|0A|…|FD` 的上位机（task14/15/16 的固件）已一并改为 AA，因此重烧 STM32 后仍可配对。

## DFS 控制逻辑（两个岔路口）

1. 正常循迹：单探头在线 → 转向修正；双探头都离线 → 直行。
2. 两个探头同时在线（**双白**，即黑线同时压住两探头）→ 认为到达岔路，压栈并**先尝试左分支**（左优先）。
3. 分支中重新咬线后，若**进入该分支后 1 分钟内再次出现双白**，判为死亡分支 → 倒车回到上一路口。
   - 对应规则：`一分钟之内碰到两次双white就是死胡同`（`DEAD_END_DW_WINDOW_TICKS = 6000`，1 分钟 @ 100Hz）。
   - 另保留 `TRACE_DEAD_END_TICKS`（800ms 持续双黑）作为双探头同时离线的兜底死胡同判定。
4. 倒车回路口时，若该路口的另一分支（右）未试过 → 转右再试；若左右都失败 → 弹栈回溯到父路口。
5. 栈耗尽 / 栈满 / 倒车超时 → `TRACE_FAILED` 并停车。
6. 起点未进入任何分支前，第一次双白仍按「新路口」处理（压栈 + 左转），不会误判为死胡同。

> 说明：当前只有两个底部探头，「双白=岔路 / 双白+1分钟内再次双白=死胡同」是启发式，与迷宫几何强相关（正确长路径 > 1 分钟、死胡同短分支 < 1 分钟）。请按真实迷宫标定 `DEAD_END_DW_WINDOW_TICKS`。

## 主要参数

集中在 `15.0_Line_DFS/line_dfs.c` 头部：

| 参数 | 默认值 | 作用 |
|---|---:|---|
| `TRACE_ENABLE_MOTION` | `1` | 运动开关（实机跑前先置 0 观察日志） |
| `DRIVE_SPEED` | `80` | 正常循迹速度（0.01圈/s） |
| `TURN_SPEED` | `40` | 原地转向速度 |
| `TURN_TIMEOUT_TICKS` | `300` | 转向超时 3s |
| `REVERSE_SPEED` | `40` | 倒车速度 |
| `REVERSE_TIMEOUT_TICKS` | `800` | 单次倒车超时 8s |
| `DEAD_END_DW_WINDOW_TICKS` | `6000` | 分支内「再次双白」判死胡同的 1 分钟窗口 |
| `TRACE_DEAD_END_TICKS` | `80` | 双黑持续 ~800ms 兜底判死胡同 |
| `TRACE_STACK_MAX` | `16` | 最大 DFS 路口深度 |
| `SPEED_LIMIT` | `150` | SET_SPEED 速度钳位 |

Hi3861 tick 为 100Hz（1 tick ≈ 10ms）。

## 编译和烧录

1. 上传 `15.0_Line_DFS/` 到 OpenHarmony `applications/sample/wifi-iot/app/` 对应目录；`reference/app_BUILD.gn` 让 feature 指向 `"15.0_Line_DFS:line_dfs"`。
2. Ubuntu 编译：`python3 build.py wifiiot`。
3. 用 HiBurn 烧 `Hi3861_wifiiot_app_allinone.bin`。
4. **STM32 侧需要重烧** `stm32/8_双核协议控制`（Keil 编译后 SWD 下载），否则 Hi3861 发的 AA 帧无人应答、车不动作。

## 关键日志（串口 / BLE 调试）

- `junction depth=N: try left`：压入新路口并尝试左侧。
- `dead end (double-white x2); reversing`：分支内 1 分钟再次双白 → 死胡同，倒车。
- `dead end; reversing`：双黑持续时间兜底 → 死胡同，倒车。
- `junction depth=N: try right`：返回路口后尝试右侧。
- `junction exhausted` / `FAILED`：左右都失败/栈耗尽，保护性停车。
- `odo=…/… ack=…/…`：`ack=ok/bad` 是 STATUS 帧校验收/失败计数，用于确认双向链路。

## 实机测试清单（按序）

- [ ] `TRACE_ENABLE_MOTION=0` 烧录：日志显示「control started」且每 100ms `GET_STATUS` 发出；STM32 侧应看到 STATUS 回传，Hi3861 日志 `ack=ok` 递增（双向链路通）。
- [ ] 轮子悬空（`TRACE_ENABLE_MOTION=1`）：
  - [ ] 双探头同时压线 → 状态转 `TURN_L`（左优先），日志 `try left`。
  - [ ] 左转到底后 `CAPTURE` 咬线，回到 `FOLLOW`。
  - [ ] 手动模拟死胡同（分支内再次双白）→ `dead end (double-white x2); reversing`，随后 `try right`。
- [ ] 地面贴黑线单路口：左分支能进、能判定死胡同倒回、再试右分支。
- [ ] 两岔路口小迷宫：左优先 DFS 走通，两死胡同均能被倒车回溯并选另一分支（是否自动停视栈耗尽而定）。
- [ ] 传感器失效/转向超时/倒车超时均会停车（保护性）。
- [ ] 全迷宫实测跑通后，把 `DEAD_END_DW_WINDOW_TICKS`、`DRIVE_SPEED`、`TURN_SPEED`、`REVERSE_SPEED` 按实际场地微调。

## 说明

- 速度语义沿用现有标定口径（`int16 = 圈/s×100`），只需改协议帧封包，无需重新标定 PID / 运动常量。
- STM32 侧以 `USER/pid.c` 为准（`control_system.c` 未编入工程，本次未改动）。
- 本次改动同时把 task14/task15/task16 的 V2 帧构建改为 AA，保证重烧 STM32 后兼容。
