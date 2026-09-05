
# 任务17：双探头黑线 DFS 岔路循迹

本任务让小车沿黑线从起点探索到终点。遇到左/右岔路时，优先尝试左侧；如果该分支进入死胡同，小车倒车返回上一个路口，再尝试右侧分支。整体控制思路对应数据结构中的深度优先搜索（DFS）。

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

## 硬件和通信

- Hi3861：GPIO13 为左侧 TCRT5000，GPIO14 为右侧 TCRT5000。
- Hi3861：GPIO11/12 复用 UART2，与 STM32 双核协议控制板通信。
- UART2：115200、8N1；帧格式为 `AA | CMD | LEN | PAYLOAD | CHECK`。
- STM32 必须烧录项目中的任务24双核协议版本，并确认与当前 UART2 协议一致。
- 上电前先确认 STM32 是任务24双核版，避免与其他双核协议版本配对失败。

## DFS 控制逻辑

1. 正常循迹时，单探头检测到黑线则进行方向修正。
2. 两个探头同时检测到线，认为到达岔路；当前版本只处理左/右分支，不单独遍历直行分支。
3. 每个新路口压入一个栈元素：
   - 第一次尝试左分支；
   - 左分支失败后返回该路口；
   - 再尝试右分支；
   - 左右都失败则弹栈，继续回溯到父路口。
4. 分支中重新捕获过线路后，如果双探头持续双黑，认为进入死胡同，进入 `TRACE_BACKTRACK` 并倒车。
5. 栈耗尽、栈满或倒车超时，进入 `TRACE_FAILED` 并停车。

## 重要限制

当前只有两个底部红外探头，因此“双黑”也可能表示较长的直线、传感器暂时丢线或实际死胡同。死胡同判断是启发式的，必须现场标定。当前硬件接口也没有独立的终点标志，所以程序不会自动推断终点；建议后续增加终点特殊黑线图案、第三路传感器或编码器/里程辅助判断。

## 主要参数

参数集中在 `15.0_Line_DFS/line_dfs.c` 文件头部：

| 参数 | 默认值 | 作用 |
|---|---:|---|
| `TRACE_ENABLE_MOTION` | `0` | 安全开关，默认强制 STOP |
| `DRIVE_SPEED` | `80` | 正常循迹速度 |
| `TURN_SPEED` | `40` | 原地转向速度 |
| `TURN_TIMEOUT_TICKS` | `300` | 转向最大 3 秒 |
| `BACKTRACK_SPEED` | `45` | 倒车速度 |
| `BACKTRACK_TIMEOUT_TICKS` | `800` | 单次倒车最大 8 秒 |
| `TRACE_DEAD_END_TICKS` | `80` | 双黑持续约 800ms 判为死胡同 |
| `TRACE_STACK_MAX` | `16` | 最大 DFS 路口深度 |

Hi3861 项目 tick 为 100Hz，即 1 tick 约 10ms。建议按“轮子悬空 → 低速单路口 → 含死胡同小地图 → 完整赛道”的顺序测试。

## 编译和烧录

1. 将 `15.0_Line_DFS/` 上传到 OpenHarmony 工程 `applications/sample/wifi-iot/app/` 对应目录。
2. 参考 `reference/app_BUILD.gn`，让 app 的 feature 指向：

   ```gn
   "15.0_Line_DFS:line_dfs",
   ```

3. 在 OpenHarmony 编译环境执行：

   ```bash
   python3 build.py wifiiot
   ```

4. 将 `Hi3861_wifiiot_app_allinone.bin` 用 HiBurn 烧录。
5. 先保持 `TRACE_ENABLE_MOTION=0` 观察传感器和状态日志；确认协议、传感器电平和方向后，再在轮子离地状态下打开运动。

## 关键日志

- `junction depth=N: try left`：压入新路口并尝试左侧。
- `dead end; backtracking`：检测到死胡同，进入倒车回溯。
- `junction depth=N: try right`：返回路口后尝试右侧。
- `junction exhausted`：当前路口左右分支均失败，弹栈回到父路口。
- `BACKTRACK timeout` / `FAILED`：保护性停车。

## 验证清单

- [ ] 编译输出 `BUILD SUCCESS`。
- [ ] `TRACE_ENABLE_MOTION=0` 时 STM32 始终收到 STOP。
- [ ] 轮子悬空时左转、右转、倒车方向与日志一致。
- [ ] 单路口左右分支均能识别。
- [ ] 左侧死胡同能倒回原路口。
- [ ] 返回路口后能尝试右侧分支。
- [ ] 多层路口不会超过 `TRACE_STACK_MAX`。
- [ ] 传感器失效、转向超时、倒车超时都会停车。

本任务沿用现有 `Trace_Following_v6_3.c` 实现整理而来，默认不改变 STM32 工程。
