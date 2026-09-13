# 2027 工创赛 RFID 电动车控制固件

仓库：<https://github.com/stm32-hit-team/2027-control-firmware>

车上那块主控是 STM32F103C6T6。程序读 RFID 卡上的文字，再让语音模块念出来。读到卡时，`PA8` 比赛灯会亮约 500 ms。电机默认会转：上电先等 3 秒，再约 3 秒爬到巡航转速。跑多久后停，由 `PB1` 旋钮决定。

日常开发用 PlatformIO，看第 6 节。队友只用 Keil 时，不要在本分支里手搭工程。请直接打开 [`keil`](https://github.com/stm32-hit-team/2027-control-firmware/tree/keil) 分支，那里是一份能直接用 Keil 打开、和 PlatformIO 无关的工程。说明见第 7 节，以及 [工创赛控制/08-Keil工程.md](工创赛控制/08-Keil工程.md)。

更细的说明在 [`工创赛控制/`](工创赛控制/index.md)。

---

## 1. 说明文档

用 Obsidian 打开 `工创赛控制/`，或在 GitHub 上点下面的链接。

| 页 | 讲什么 |
|---|---|
| [文档首页](工创赛控制/index.md) | 从哪看起 |
| [总览](工创赛控制/00-总览.md) | 产品做什么、分几层、数据怎么走 |
| [硬件对照](工创赛控制/01-硬件对照.md) | 插座、开关、单片机脚 |
| [主循环](工创赛控制/02-主循环.md) | 上电后程序怎么转 |
| [读卡到播报](工创赛控制/03-读卡到播报.md) | 一张卡从读到念 |
| [模块原理](工创赛控制/07-模块原理.md) | RFID、语音、缓启动、电机、灯 |
| [函数索引](工创赛控制/04-函数索引.md) | 每个函数做什么 |
| [编译和烧录](工创赛控制/05-编译烧录.md) | PlatformIO 怎么编、怎么烧 |
| [转到 Keil](工创赛控制/08-Keil工程.md) | `keil` 分支怎么用，以及和本分支怎么对应 |
| [故障排查](工创赛控制/06-故障排查.md) | 灯、卡、语音、电机常见问题 |

源文件对照：

- [src/main.c](工创赛控制/src-main.c.md)
- [src/app.c](工创赛控制/src-app.c.md)
- [src/board_stm32.c](工创赛控制/src-board_stm32.c.md)
- [include/app.h](工创赛控制/include-app.h.md)
- [include/app_config.h](工创赛控制/include-app_config.h.md)
- [include/board.h](工创赛控制/include-board.h.md)

---

## 2. 代码里有哪些部分

应用层在 `src/`、`include/`。读卡协议和状态机在 `lib/rfid_core/`，那是库，不要改。

```
main.c          上电入口：先硬件，再业务，然后一直转
app.c           读到卡就立刻发文字，并点灯
announce.c      立刻播报。没有队列，也不等念完
soft_start.c    把旋钮 ADC 换成停车毫秒数
board_stm32.c   时钟、脚、串口、旋钮、电机、灯
rfid_protocol   串口帧的拼和拆（库）
rfid_reader     寻卡、读块、去重、确认拿走（库）
tts_service     库里的语音队列。应用层不用
```

分层：

```
main → app → rfid_reader / announce
         ↘ board → soft_start
                ↘ STM32 HAL
```

改业务只动 `src/` 和 `include/`。能调的数字都在 `include/app_config.h`。

---

## 3. 各部分怎么工作

更细的图和状态机见 [模块原理](工创赛控制/07-模块原理.md)。这里只写要点。

### 3.1 RFID

读卡器接 `H1`，走 USART1。上电先用 9600 发一条改速命令，再改成 115200 工作。

串口帧以 `0x7F` 开头。数据里如果也有 `0x7F`，就写成两个 `0x7F`。校验是逐字节异或。拼帧和拆帧在 `rfid_protocol`。

`rfid_reader` 是状态机，不阻塞：

1. 空闲时约每 40 ms 问一次「有没有卡」。
2. 读到 UID 后，依次读 4、5、6、8 号块，拼出文字。
3. 文字必须是合法 GB2312 或可打印 ASCII。乱码不会送给语音。
4. 应用层立刻发文字。串口没发出去，状态机会稍后重试。
5. 同一张卡一直放着只念一次。判重看 UID，不看文字。
6. 连续约 3 次读不到，才认为拿走。拿走再放，会再念。

USART1 收到的字节先进入 128 字节环形缓冲。主循环再喂给状态机。

### 3.2 语音（TTS）

语音模块接 `U7`，走 USART2，一直是 9600，只发不收。`SW1` 管模块电源，不接单片机脚。

库里有 `tts_service`，带队列，还会按字数估念完时间。比赛要「读到就念，下一张卡打断上一句」，所以应用层改用 `announce.c`：

- 开机约 500 ms 后发 `<S>3`，把语速定下来。
- 读到卡就按显式长度发文字。不靠字符串结尾，因为 GB2312 里可能有 `0x00`。
- 没有队列，也不等念完。下一张不同 UID 的卡会马上改念。

### 3.3 缓启动和停车旋钮

「缓启动」在这份固件里是两件事，不要混：

1. **晚启动 + 爬升**在 `board_stm32.c`。上电后固定等 3 秒，电机先不动；再花约 3 秒把 PWM 比较值从 0 升到 600。周期是 999。
2. **停车时间**在 `soft_start.c`。上电时从 `PB1` 采 20 次 ADC，取平均。按省赛那套分压公式估电位器电阻，再映射到 10 秒～10 分钟。之后不再采。

旋钮只管「跑多久后停」，不管「等多久才起步」。起步永远是 3 秒。

电机接 DRV8837，PWM 在 `PA0`、`PA1`。比较值为 0 时，两路都写成周期值，电机两端没有压差。前进时一路为 0，另一路为比较值。驱动芯片的唤醒脚不在 MCU 上。只出 PWM，硬件没唤醒时电机不转。

### 3.4 比赛灯

读到卡并成功发出文字后，`PA8` 拉低，灯亮约 500 ms。到点由 `board_process` 熄灭。极性按低电平点亮来写。

### 3.5 主循环

`main` 里没有 `delay`。每一圈倒空 RFID 缓冲，推进读卡、开机语速、关灯和电机，然后 `WFI` 睡到下一次中断。唤醒源是 SysTick 和 USART1。

---

## 4. 硬件对照

引脚来自嘉立创网表，不是丝印图。芯片是 STM32F103C6T6，32 KB Flash，10 KB RAM。完整表见 [硬件对照](工创赛控制/01-硬件对照.md)。

| 板上器件 | 单片机脚 | 用途 |
|---|---|---|
| `U11` STM32F103C6T6 | — | 主控 |
| `H1` RFID 插座 | `PA9` TX、`PA10` RX | USART1。先 9600 改速，再 115200 |
| `U7` 语音插座 | `PA2` TX、`PA3` RX | USART2，9600，只发不收 |
| `led灯1` | `PA8` | 比赛灯，低电平点亮 |
| `R11` / `R4` / `C4` | `PB1` | 停车时间旋钮，上电读一次 |
| `U2` DRV8837 | `PA0`、`PA1` | 电机 PWM |
| `SWD` | `PA13`、`PA14`、`NRST` | 下载口，程序不碰 |
| `SW3` | `NRST`、地 | 复位键 |
| `SW1` | 不接 MCU | 播报器电源 |

接线注意：

- 读卡器 TX 接单片机 `PA10`，读卡器 RX 接 `PA9`。
- 语音模块 RX 接 `PA2`，模块 TX 接 `PA3`。
- `SW1` 关上时模块没电。程序仍会往串口发，但听不见。
- 时钟按外部 8 MHz 晶振、PLL 到 72 MHz。晶振不起振时程序会停住。

首次上板必须架空车轮，并准备断电。

---

## 5. 目录和参数

```
firmware/                 Git 仓库根（也是本 README 所在目录）
  include/                应用层头文件，参数在 app_config.h
  src/                    应用层源文件
  lib/rfid_core/          读卡协议和状态机，不要改
  test/native/            本机单元测试
  scripts/                本机测试脚本
  工创赛控制/             说明文档
  platformio.ini          PlatformIO 工程（本分支）
```

常用参数，改 `include/app_config.h` 后重新编译：

| 名字 | 默认 | 含义 |
|---|---|---|
| `APP_RFID_SETUP_BAUD_RATE` | 9600 | 给读卡器发改速命令时用 |
| `APP_RFID_BAUD_RATE` | 115200 | RFID 工作波特率 |
| `APP_TTS_BAUD_RATE` | 9600 | 语音波特率 |
| `APP_RFID_POLL_INTERVAL_MS` | 40 | 空闲时多久问一次卡 |
| `APP_RFID_REMOVAL_CONFIRMATIONS` | 3 | 连续几次读不到算拿走 |
| `APP_TTS_SET_SPEED_ON_STARTUP` | 1 | 开机是否发语速 |
| `APP_LED_MARK_DURATION_MS` | 500 | 比赛灯亮多久 |
| `APP_MOTOR_START_LATE_MS` | 3000 | 上电后多久起步 |
| `APP_MOTOR_RAMP_MS` | 3000 | 爬升用多久 |
| `APP_MOTOR_TARGET_COMPARE` | 600 | 目标比较值，周期 999 |
| `APP_MOTOR_STOP_MIN_MS` | 10000 | 旋钮最短停车 |
| `APP_MOTOR_STOP_MAX_MS` | 600000 | 旋钮最长停车 |
| `APP_ENABLE_MOTOR` | 1 | 电机是否打开 |

完整表见 [include/app_config.h](工创赛控制/include-app_config.h.md)。

---

## 6. 用 PlatformIO 编译和烧录

适合 Mac、Linux，Windows 也可以。步骤摘要如下。细节见 [编译和烧录](工创赛控制/05-编译烧录.md)。

### 6.1 安装

1. 安装 [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html)，或在 VS Code / Cursor 里装 PlatformIO 插件。
2. 安装 ST-Link 驱动。macOS 一般不用额外驱动。
3. 克隆本仓库后进入仓库根目录。

```sh
git clone https://github.com/stm32-hit-team/2027-control-firmware.git
cd 2027-control-firmware
```

仓库是私有的。克隆失败时，让组织管理员把你的 GitHub 账号加进 `stm32-hit-team`。

默认分支是 `main`，这是 PlatformIO 工程。Keil 工程在 `keil` 分支。

### 6.2 本机测试（不插板）

需要本机有 `clang`。

```sh
sh scripts/run_host_tests.sh
```

看到 `All native firmware tests passed.` 就过了。

### 6.3 交叉编译和烧录

这块板用 C6，不要用 C8 环境：

```sh
pio run -e stm32f103c6
pio run -e stm32f103c6 -t upload
```

生成文件：`.pio/build/stm32f103c6/firmware.bin` 和 `firmware.hex`。

`platformio.ini` 里 `upload_protocol` 已是 `stlink`。SWD 接 `PA13`、`PA14`、`NRST`、地、3.3 V。下载器是 CMSIS-DAP 时，把那一行改成 `cmsis-dap`。

**不要把 C8 固件刷到 C6 板上。** C8 按 64 KB Flash 链接，这块板只有 32 KB。

烧录成功后芯片会复位。约 3 秒后电机可能转，先架空车轮。

### 6.4 现场检查

1. 上电约 3 秒，电机起步；再约 3 秒到巡航。
2. 打开 `SW1`。把卡放到读卡区，灯应闪一下并马上播报。
3. 同一张卡一直放着只念一次。拿走再放，会再念。
4. 旋钮改变的是多久停，不是等多久才起步。

---

## 7. 转到 Keil

本分支是 PlatformIO 工程，**不要**直接用 Keil 打开仓库根目录。

已经准备好的工程在 [`keil`](https://github.com/stm32-hit-team/2027-control-firmware/tree/keil) 分支。那份工程：

- 用 Keil 5（MDK-ARM）打开 `MDK-ARM/2027-control.uvprojx` 即可
- 里面没有 `platformio.ini`，也不依赖 PlatformIO
- 应用源文件和 `main` 分支同一套：`src/`、`include/`、`lib/rfid_core/`
- HAL、CMSIS、启动文件已经放在工程里

```sh
git clone https://github.com/stm32-hit-team/2027-control-firmware.git
cd 2027-control-firmware
git checkout keil
```

然后用 Keil 打开 `MDK-ARM/2027-control.uvprojx`，Rebuild，再用 ST-Link 下载。

芯片按 **STM32F103C6** 配，Flash 32 KB，RAM 10 KB。不要改成 C8。

`main` 上改了业务代码之后，要更新 Keil 工程时，把同一批 `src/`、`include/`、`lib/rfid_core/` 拷到 `keil` 分支对应位置即可。HAL 和启动文件不用动。逐步说明见 [转到 Keil](工创赛控制/08-Keil工程.md)。

不要打开仓库旁边那份「2027 省赛」Keil 工程来编这份固件。省赛工程带 FreeRTOS，业务也不是这一套。

---

## 8. 常见问题

**灯不亮、电机不转、串口没动静**  
晶振没起振，或没烧进去。程序配时钟失败会停住。

**约 3 秒后车会走，刷卡没反应**  
读卡器要接 `H1`。工作波特率是 115200。本程序上电会再发一次改速命令。

**车很久不走**  
起步固定 3 秒。若 3 秒后仍不转，查驱动唤醒、电池、PWM 脚 `PA0` / `PA1`。不要把旋钮当成晚启动。

**能读卡但不播报**  
打开 `SW1`。语音接 `U7`。卡片必须是合法 GB2312。

**灯常亮或不亮**  
比赛灯按低电平点亮。先查 `PA8` 极性。

更多见 [故障排查](工创赛控制/06-故障排查.md)。

---

## 9. 协作约定

- 业务改动放在 `src/`、`include/`、`test/`。
- 不要改 `lib/rfid_core/`。
- 不要改 STM32 HAL、CMSIS、Cube 厂商库。
- 改参数优先改 `include/app_config.h`。
- 提交前尽量跑：`sh scripts/run_host_tests.sh`
- 日常开发推 `main`。给 Keil 用的工程推 `keil`。

仓库是 GitHub 私有库。需要权限时，找组织 `stm32-hit-team` 的管理员把你加上。
