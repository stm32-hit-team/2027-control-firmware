# 2027 工创赛 RFID 电动车控制固件

仓库地址：<https://github.com/stm32-hit-team/2027-control-firmware>

这是车上那块 STM32F103C6T6 的控制程序。单片机读 RFID 卡上的文字，再从语音模块念出来。读到卡时，`PA8` 比赛灯会亮一小会儿。电机默认会转。

日常开发可以用 PlatformIO。队友如果只用 Keil，看本文「转到 Keil」一节即可。

更细的说明在 `工创赛控制/` 目录里。

---

## 1. 上电后做什么

1. 配好 72 MHz 时钟、灯、两路串口、旋钮 ADC、电机 PWM。
2. USART1 先用 9600 给读卡器发改速命令，再改成 115200 工作。
3. USART2 按 9600 给语音模块发 `<S>3`（开机约 500 ms 后）。
4. 固定等 3 秒，再约 3 秒把电机爬到目标转速。
5. 之后约每 40 ms 问一次「有没有卡」。
6. 读到新卡就立刻播报，并点亮 `PA8` 约 500 ms。
7. 同一张卡一直放着只念一次。连续约 3 次读不到，才认为拿走。
8. 跑到旋钮定下的时间后停转。范围默认 10 秒到 10 分钟。

卡片文字必须是合法 GB2312 或可打印 ASCII。乱码不会送给语音。

`lib/rfid_core` 是库，不要改。库里还有语音队列，应用层不用，改成立刻播报。

---

## 2. 硬件对照

引脚来自嘉立创网表，不是丝印图。芯片是 STM32F103C6T6，32 KB Flash，10 KB RAM。

| 板上器件 | 单片机脚 | 用途 |
|---|---|---|
| `U11` STM32F103C6T6 | — | 主控 |
| `H1` RFID 插座 | `PA9` TX、`PA10` RX | USART1。先 9600 改速，再 115200 |
| `U7` 语音插座 | `PA2` TX、`PA3` RX | USART2，9600，只发不收 |
| `led灯1` | `PA8` | 比赛灯，低电平点亮 |
| `R11` / `R4` / `C4` | `PB1` | 停车时间旋钮，上电读一次 |
| `U2` DRV8837 | `PA0`、`PA1` | 电机 PWM |
| `电机` / `电机1` | 驱动芯片 OUT1、OUT2 | 电机接线柱 |
| `SWD` | `PA13`、`PA14`、`NRST` | 下载口，程序不碰 |
| `SW3` | `NRST`、地 | 复位键 |
| `SW1` | 不接 MCU | 播报器电源 |

接线注意：

- 读卡器 TX 接单片机 `PA10`，读卡器 RX 接 `PA9`。
- 语音模块 RX 接 `PA2`，模块 TX 接 `PA3`。
- `SW1` 关上时模块没电。程序仍会往串口发，但听不见。
- 电机驱动唤醒脚不在 MCU 上。只出 PWM，硬件没唤醒时电机不转。
- 时钟按外部 8 MHz 晶振、PLL 到 72 MHz。晶振不起振时程序会停住。

首次上板必须架空车轮，并准备断电。

---

## 3. 目录

```
firmware/
  include/          应用层头文件，参数在 app_config.h
  src/              应用层源文件
  lib/rfid_core/    读卡协议和状态机，不要改
  test/native/      本机单元测试
  scripts/          本机测试脚本
  工创赛控制/       更细的中文说明
  platformio.ini    PlatformIO 工程
```

应用层主要文件：

| 文件 | 作用 |
|---|---|
| `src/main.c` | 上电入口 |
| `src/app.c` | 读到卡立刻发文字、点灯 |
| `src/announce.c` | 立刻播报，不排队 |
| `src/soft_start.c` | 把旋钮 ADC 换成停车毫秒数 |
| `src/board_stm32.c` | 时钟、脚、串口、旋钮、电机 |
| `include/app_config.h` | 能调的数字 |

---

## 4. 改参数

改 `include/app_config.h`，然后重新编译。

| 名字 | 默认 | 含义 |
|---|---|---|
| `APP_RFID_SETUP_BAUD_RATE` | 9600 | 给读卡器发改速命令时用 |
| `APP_RFID_BAUD_RATE` | 115200 | RFID 工作波特率 |
| `APP_TTS_BAUD_RATE` | 9600 | 语音波特率 |
| `APP_RFID_POLL_INTERVAL_MS` | 40 | 空闲时多久问一次卡 |
| `APP_RFID_RESPONSE_TIMEOUT_MS` | 80 | 等回复超时 |
| `APP_RFID_REMOVAL_POLL_MS` | 80 | 确认卡还在的间隔 |
| `APP_RFID_REMOVAL_CONFIRMATIONS` | 3 | 连续几次读不到算拿走 |
| `APP_TTS_SET_SPEED_ON_STARTUP` | 1 | 开机是否发语速 |
| `APP_LED_MARK_DURATION_MS` | 500 | 比赛灯亮多久 |
| `APP_MOTOR_START_LATE_MS` | 3000 | 上电后多久起步 |
| `APP_MOTOR_RAMP_MS` | 3000 | 爬升用多久 |
| `APP_MOTOR_TARGET_COMPARE` | 600 | 目标比较值，周期 999 |
| `APP_MOTOR_STOP_MIN_MS` | 10000 | 旋钮最短停车 |
| `APP_MOTOR_STOP_MAX_MS` | 600000 | 旋钮最长停车 |
| `APP_ENABLE_MOTOR` | 1 | 电机是否打开 |

旋钮只管停车，不管起步。起步固定 3 秒。

同一张卡还在场上时，按 UID 去重，不会连念。这不是按文字去重。

---

## 5. 用 PlatformIO 编译和烧录

适合 Mac、Linux，Windows 也可以。

### 5.1 安装

1. 安装 [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html)，或在 VS Code / Cursor 里装 PlatformIO 插件。
2. 安装 ST-Link 驱动。macOS 一般不用额外驱动。
3. 把工程放到本机后进入 `firmware` 目录。

```sh
git clone https://github.com/stm32-hit-team/2027-control-firmware.git
cd 2027-control-firmware
```

仓库是私有的。克隆失败时，让组织管理员把你的 GitHub 账号加进 `stm32-hit-team`。

### 5.2 本机测试（不插板）

需要本机有 `clang`。

```sh
sh scripts/run_host_tests.sh
```

看到 `All native firmware tests passed.` 就过了。

### 5.3 交叉编译

这块板用 C6，不要用 C8 环境：

```sh
pio run -e stm32f103c6
```

生成文件：`.pio/build/stm32f103c6/firmware.bin` 和 `firmware.hex`。

### 5.4 烧录

`platformio.ini` 里 `upload_protocol` 已是 `stlink`。SWD 接 `PA13`、`PA14`、`NRST`、地、3.3 V。

```sh
pio run -e stm32f103c6 -t upload
```

下载器是 CMSIS-DAP 时，把 `platformio.ini` 里那一行改成 `cmsis-dap`。

烧录成功后芯片会复位。约 3 秒后电机可能转，先架空车轮。

### 5.5 现场检查

1. 上电约 3 秒，电机起步；再约 3 秒到巡航。
2. 打开 `SW1`。把卡放到读卡区，灯应闪一下并马上播报。
3. 同一张卡一直放着只念一次。拿走再放，会再念。
4. 旋钮改变的是多久停，不是等多久才起步。

---

## 6. 常见问题

**灯不亮、电机不转、串口没动静**  
晶振没起振，或没烧进去。程序配时钟失败会停住。

**约 3 秒后车会走，刷卡没反应**  
读卡器要接 `H1`。工作波特率是 115200。先烧过省赛程序的模块一般已经是这个值。本程序上电会再发一次改速命令。

**车很久不走**  
起步固定 3 秒。若 3 秒后仍不转，查驱动唤醒、电池、PWM 脚 `PA0`/`PA1`。不要把旋钮当成晚启动。

**能读卡但不播报**  
打开 `SW1`。语音接 `U7`。卡片必须是合法 GB2312。

**灯常亮或不亮**  
比赛灯按低电平点亮。先查 `PA8` 极性。

**不要把 C8 固件刷到 C6 板上。** C8 按 64 KB Flash 链接，这块板只有 32 KB。

---

## 7. 转到 Keil

队友用 Keil 时，不要直接打开本仓库当 Keil 工程。本仓库是 PlatformIO 工程。按下面做一份 Keil 工程，源文件仍用本仓库里的 `src/`、`include/`、`lib/rfid_core/`。

建议 Keil 5（MDK-ARM）+ STM32F1xx HAL。省赛那份工程用过同一套 HAL，可以对照，但不要把 FreeRTOS 和省赛 `Task/` 拷进来。

### 7.1 用 CubeMX 生成骨架（推荐）

1. 打开 STM32CubeMX。新建工程，芯片选 **STM32F103C6Tx**（或 C6T6）。
2. 时钟：外部晶振 HSE 8 MHz，PLL ×9，SYSCLK 72 MHz。APB1 36 MHz。Flash Latency 2。
3. 引脚按第 2 节配：
   - `PA9` / `PA10`：USART1
   - `PA2` / `PA3`：USART2
   - `PA8`：GPIO 输出
   - `PA0` / `PA1`：TIM2_CH1 / TIM2_CH2
   - `PB1`：ADC1_IN9
4. USART1、USART2：9600、8 位、1 停止、无校验。真正的 RFID 115200 由我们的 `board_stm32.c` 在运行时改，不必在 Cube 里写成 115200。
5. TIM2：PWM，预分频 71，周期 999，通道 1 和通道 2，PWM2。
6. ADC1：规则通道 9。
7. 系统节拍选 **SysTick**。不要选 TIM1 做 HAL 时基。我们的 `board_stm32.c` 里已经有 `SysTick_Handler`。
8. 工程管理：
   - Toolchain / IDE 选 **MDK-ARM V5**
   - 不要勾选 FreeRTOS
   - 生成到单独目录，例如 `keil-board/`，不要覆盖本仓库的 `src/`
9. 生成代码。

### 7.2 在 Keil 里换成我们的源文件

打开 Cube 生成的 `.uvprojx`。

**删掉或移出编译的文件：**

- Cube 生成的 `Core/Src/main.c`（我们有自己的 `main.c`）
- `Core/Src/stm32f1xx_it.c` 里的 `SysTick_Handler`、`USART1_IRQHandler`  
  这两个符号在 `src/board_stm32.c` 里。两边都留会重复定义。  
  可以整份不编译 `stm32f1xx_it.c`，或把这两个函数删掉，只留故障处理。
- 所有 FreeRTOS 文件（如果误开了）
- Cube 里若生成了 `HAL_TIM_PeriodElapsedCallback` 给 TIM1 加节拍，删掉

**加入本仓库文件（添加到工程，不要复制进 `lib/` 乱改）：**

```
src/main.c
src/app.c
src/announce.c
src/soft_start.c
src/board_stm32.c
lib/rfid_core/src/rfid_protocol.c
lib/rfid_core/src/rfid_reader.c
```

`tts_service.c` 应用层没用到，Keil 里可以不加入，省 Flash。

头文件路径（Manage Project Items → C/C++ → Include Paths）至少包括：

```
../include
../lib/rfid_core/include
Cube 生成的 Core/Inc
Drivers/STM32F1xx_HAL_Driver/Inc
Drivers/CMSIS/Device/ST/STM32F1xx/Include
Drivers/CMSIS/Include
```

上面 `../` 按你 Keil 工程相对本仓库根目录的实际位置改。Keil 工程如果就在仓库根下的 `MDK-ARM/`，路径就是：

```
../include
../lib/rfid_core/include
../Core/Inc
../Drivers/STM32F1xx_HAL_Driver/Inc
../Drivers/CMSIS/Device/ST/STM32F1xx/Include
../Drivers/CMSIS/Include
```

**预处理器宏（C/C++ → Define）：**

```
USE_HAL_DRIVER,STM32F103x6,APP_ENABLE_MOTOR=1
```

不要写成 `STM32F103xB` 或 `STM32F103x8`。

**C 语言标准：** 选 C99 或 C11。不要用 C90。

**芯片和下载：**

- Device 选 STM32F103C6
- IROM1：起始 `0x08000000`，大小 `0x8000`（32 KB）
- IRAM1：起始 `0x20000000`，大小 `0x2800`（10 KB）
- 启动文件用 `startup_stm32f103x6.s`
- 调试器选 ST-Link，SWD

**HSE 频率：** 在 `stm32f1xx_hal_conf.h` 里确认 `HSE_VALUE` 是 `8000000U`。

### 7.3 不经过 CubeMX 的做法

如果你手里有省赛 Keil 工程：

1. 另存一份，芯片改成 STM32F103C6，Flash 改成 32 KB。
2. 去掉 FreeRTOS、`Task/`、`hardware/rfid_card`、`hardware/USART` 等省赛业务文件。
3. 保留 HAL、CMSIS、启动文件。
4. 按 7.2 加入本仓库源文件和头文件路径。
5. 宏改成 `USE_HAL_DRIVER,STM32F103x6,APP_ENABLE_MOTOR=1`。
6. 确认没有第二份 `SysTick_Handler` / `USART1_IRQHandler`。
7. 确认 HAL 时基是 SysTick，不是 TIM1。

省赛工程设备栏有时写成 C8，宏却是 `STM32F103x6`。我们这块板按 C6、32 KB 来。

### 7.4 Keil 编译通过后怎么烧

1. Rebuild 无错误。
2. 用 ST-Link 下载。
3. 复位或重新上电。
4. 按第 5.5 节做现场检查。

Keil 生成的 `.hex` 也可以用 ST-Link Utility / STM32CubeProgrammer 烧。

### 7.5 Keil 里常见报错

| 现象 | 原因 | 处理 |
|---|---|---|
| `SysTick_Handler` multiple definition | Cube 的 `stm32f1xx_it.c` 和 `board_stm32.c` 都有 | 只留一份 |
| `USART1_IRQHandler` multiple definition | 同上 | 只留 `board_stm32.c` 里的 |
| 找不到 `app.h` / `rfid_reader.h` | 头文件路径没加 | 按 7.2 加 Include Paths |
| `STM32F103x6` 未定义或外设对不上 | 宏写成了 xB / xE | 改成 `STM32F103x6` |
| 能编译，板子完全没反应 | 按 64 KB 链接，或 HSE 配错 | Flash 用 32 KB；`HSE_VALUE` 用 8 MHz |
| 刷卡失败 | 没编进 `board_stm32.c` 的改速逻辑 | 确认加入的是本仓库 `src/board_stm32.c` |

---

## 8. 协作约定

- 业务改动放在 `src/`、`include/`、`test/`。
- 不要改 `lib/rfid_core/`。
- 不要改 STM32 HAL、CMSIS、Cube 厂商库。
- 改参数优先改 `include/app_config.h`。
- 提交前尽量跑：`sh scripts/run_host_tests.sh`

仓库是 GitHub 私有库。需要权限时，找组织 `stm32-hit-team` 的管理员把你加上。
