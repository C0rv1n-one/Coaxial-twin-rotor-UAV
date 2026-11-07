# Coaxial-twin-rotor-UAV

同轴双桨无人机（Coaxial Twin-Rotor UAV）项目仓库。

两个旋翼上下同轴安装、反向旋转，靠反扭矩相互抵消，因此不需要尾桨来平衡偏航——
机身更紧凑，同等桨盘面积下推力密度更高。这个仓库用来放该项目的设计资料、代码与测试记录。

## 目录结构

```
.
├── receiver/          遥控器接收端固件（STM32F103C8T6，CubeMX + Keil MDK）
├── Remoter_Ctrl3.6/   遥控器发射端固件（ANO_Remoter，Keil MDK）
├── F3/                SPRacingF3 飞控资料：CLI dump、参考源码、CP210x 脚本
├── docs/              设计文档：总体方案、气动估算、结构图、实验记录
├── firmware/          飞控固件（如使用 PX4 / ArduPilot 自研模块，放这里）
├── hardware/          电路原理图、PCB、BOM、机械件图纸
├── sim/               仿真模型与参数辨识脚本
└── tools/             标定、日志分析、数据处理脚本
```

> 目录按需创建，不用的先不建。Git 不跟踪空目录。

## 遥控链路（iBUS 下行 + MSP 姿态回传）

`receiver/` 用 nRF24L01 收 `Remoter_Ctrl3.6/` 发来的 ANO 遥控帧，
一路按 iBUS 协议发给飞控当遥控输入，另一路用 MSP 读回姿态显示到 OLED。

```
发射端 ──2.4G/ANO──> receiver(nRF24L01) ──USART2/iBUS──> F3 UART2 RX   （摇杆下行）
                                       └─USART3/MSP────> F3 UART3      （姿态回传）
```

接线（两边都是 3.3V 电平，必须共地，不要接 5V）：

| receiver | F3 飞控 | 说明 |
|---|---|---|
| PA2 (USART2_TX) | UART2 RX (A15) | 摇杆下行，iBUS |
| PB10 (USART3_TX) | UART3 RX (B11) | MSP 请求 |
| PB11 (USART3_RX) | UART3 TX (B10) | 姿态回传 |
| GND | GND | 必须共地 |

两路串口都是 115200 8N1。协议细节见 `receiver/Core/Inc/ibus.h` 与 `msp.h`。

飞控端在 Betaflight CLI 里配一次（SPRacingF3 / BF 4.0.6 实测，已写入板子）：

```
feature -RX_PPM
feature RX_SERIAL
set serialrx_provider = IBUS
serial 1 64 115200 57600 0 115200    # UART2 = RX_SERIAL
serial 2 1 115200 57600 0 115200     # UART3 = MSP
save
```

> 为什么摇杆不走 MSP：BF 4.0.6 把 `USE_RX_MSP` 包在
> `#if ((FLASH_SIZE > 256) || (FEATURE_CUT_LEVEL < 1))` 里，SPRacingF3 是 F303
> （256KB、cut level 6），没编进去；`feature RX_MSP` 存了也白存，重启时
> `validateAndFixConfig()` 会把它清掉并回落到默认的 `RX_PPM`。
> 3.3.3 的 `USE_RX_MSP` 是无条件的，支持，但要先刷固件。

- 下行 100Hz iBUS：32 字节帧 = `0x20 0x40` + 14 通道小端 + 校验（`0xFFFF - 前 30 字节和`）。
  飞控侧 `map AETR1234`：ch1=roll ch2=pitch ch3=throttle ch4=yaw，ch5~ch8 取 AUX1~AUX4。
- 上行 10Hz 请求 `MSP_ATTITUDE(108)`，OLED 第 4 行每 2 秒在姿态和 AUX1/电池之间切换。
- 遥控器掉线时 receiver 两路都停发，飞控按自己的 failsafe 处理（Betaflight 为 DROP）。

### 台架实测结果（2026-11-05）

| 项 | 结果 |
|---|---|
| nRF24L01 收 ANO 帧 | ✅ 对频成功，OLED 第 1 行 `RC C:0E CH:065 M0` |
| iBUS 下发（PA2 → UART2 RX） | ✅ BF 接收机页通道随杆动，`RX rate` 拉到 100 |
| MSP 姿态回传（UART3） | ✅ OLED 第 4 行姿态随机身转动实时变化 |
| 手柄电量 | ✅ ANO 帧 AUX5 字段，OLED 第 4 行 `B:` 为整数伏（4.05V 显示 4） |
| 飞控接收机配置 | ✅ BF 4.0.6，`feature RX_SERIAL` + `serialrx_provider = IBUS` |

### 踩坑记录

1. **USART2 中断必须手动使能**。`HAL_UART_Transmit_IT()` 靠 TXE 中断逐字节搬运，
   CubeMX 当初只为 USART3 开了中断，USART2 的 NVIC 没使能 → 一个字节都发不出去，
   `gState` 永远停在 `BUSY_TX`，`IBUS_Task()` 每次都从“上一帧没发完”分支返回。
   修复：`IBUS_Init()` 里补 `HAL_NVIC_SetPriority(USART2_IRQn, 4, 0)` + `HAL_NVIC_EnableIRQ()`，
   并在 `ibus.c` 内实现 `USART2_IRQHandler()` 调 `HAL_UART_IRQHandler(&huart2)`。
2. **手柄校准**：校准时油门没打满两端，会把行程压成 1000~1568、中位落在 1045。
   校准要把油门从最低缓慢推到最高、各停 1 秒，其余三杆同样打满一次。
3. **nRF24L01 供电**：发射瞬间电流从 15mA 跳到 100mA+，面包板与细杜邦线的接触电阻
   会让模块瞬间掉电复位，表现为“动一下线就断、`CH` 从 0 扫到 125 一直扫不到”。
   根治办法是 VCC-GND 并 10µF（配 100nF 更好），模块别插面包板。
4. **编码混用**：`msp.c`、`ibus.c`、`rc.c` 是 GBK（Keil 默认），`main.c`、`rc.h`、
   README 是 UTF-8。用脚本改文件时要按各自编码读写，否则中文注释会变成乱码。
5. **CubeMX 会重写 uvprojx**：重新生成代码后要确认 `msp.c`、`ibus.c` 还在工程文件列表里。

## 编译与烧录

Keil 命令行（MDK V5.24a，编译器 ARMCC V5.06u5）：

```
D:\Keil5\UV4\UV4.exe -b receiver\MDK-ARM\receiver.uvprojx -j0 -o build.log
```

产物 `receiver/MDK-ARM/receiver/receiver.hex`（约 43KB / 64KB Flash，RAM 约 2.3KB / 20KB）。
烧录用 ST-Link 或串口 ISP 均可；烧完把 STM32 的 `PA2`、`PB10`、`PB11`、`GND` 接回飞控。

## 当前状态

- [x] 遥控链路台架打通：nRF24L01 收 ANO 帧 → iBUS 下发飞控 → MSP 读回姿态上 OLED
- [ ] 飞行模式 / AUX 通道分配（自稳、定高、解锁开关）
- [ ] 解锁与电机转向校验（务必先卸桨）
- [ ] 总体方案与参数确定（桨径、轴距、起飞重量、推重比）
- [ ] 上下桨转速差与反扭矩配平策略
- [ ] 结构件设计 / 3D 打印件出图
- [ ] 电调与电机选型、BOM 冻结
- [ ] 悬停测试与数据记录

## 开发约定

- 提交信息用 `类型: 说明`，例如 `docs: 补充气动估算`、`feat: 加入反扭矩配平`。
- 大文件（视频、CAD 源文件、数据集）不要直接进仓库，用网盘或 Git LFS。
- 密钥、飞控密码、个人配置一律不进仓库（已在 `.gitignore` 中排除）。

## 常用命令

```bash
git status                 # 看改动
git add .                  # 暂存
git commit -m "docs: 说明"  # 提交
git push                   # 推到 GitHub
git log --oneline --graph  # 看历史
```
