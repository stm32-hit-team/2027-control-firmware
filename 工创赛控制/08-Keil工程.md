# 转到 Keil

日常开发在 `main` 分支，用 PlatformIO。给只用 Keil 的队友准备了独立分支 [`keil`](https://github.com/stm32-hit-team/2027-control-firmware/tree/keil)。

那份工程能直接用 Keil 打开。里面没有 `platformio.ini`，也不用装 PlatformIO。

## 直接打开现成工程

```sh
git clone https://github.com/stm32-hit-team/2027-control-firmware.git
cd 2027-control-firmware
git checkout keil
```

用 Keil 5（MDK-ARM）打开：

```
MDK-ARM/2027-control.uvprojx
```

然后 Rebuild，再用 ST-Link 下载。现场检查和 PlatformIO 相同，见 [[05-编译烧录]]。

仓库是私有的。克隆失败时，让组织管理员把你的 GitHub 账号加进 `stm32-hit-team`。

## 这个分支里有什么

```
keil 分支
  MDK-ARM/2027-control.uvprojx   用 Keil 打开这个
  MDK-ARM/startup_stm32f103x6.s  启动文件
  src/  include/                 和应用层同一套源文件
  lib/rfid_core/                 读卡库，不要改
  Drivers/                       STM32 HAL 和 CMSIS，不要改
  Core/                          HAL 配置和 system_stm32f1xx.c
  工创赛控制/                    同一套说明文档
```

没有这些东西：

- `platformio.ini`
- PlatformIO 的 `.pio/`
- FreeRTOS
- 省赛的 `Task/`、`hardware/`
- Cube 生成的 `main.c`、`gpio.c`、`usart.c`、`stm32f1xx_it.c`

时钟、串口、灯、旋钮、电机都在我们自己的 `src/board_stm32.c` 里配。所以 Keil 工程不必再编一份 Cube 的业务文件。`SysTick_Handler` 和 `USART1_IRQHandler` 也只在 `board_stm32.c` 里。

## 工程已经配好的项

打开后一般不用改。对照用：

| 项 | 值 |
|---|---|
| 芯片 | STM32F103C6 |
| IROM1 | `0x08000000`，大小 `0x8000`（32 KB） |
| IRAM1 | `0x20000000`，大小 `0x2800`（10 KB） |
| 宏 | `USE_HAL_DRIVER`、`STM32F103x6`、`APP_ENABLE_MOTOR=1` |
| C 语言 | C99 |
| 启动文件 | `startup_stm32f103x6.s` |
| `HSE_VALUE` | 8 MHz |
| 调试器 | ST-Link，SWD |

头文件路径已经包含：

```
../include
../lib/rfid_core/include
../Core/Inc
../Drivers/STM32F1xx_HAL_Driver/Inc
../Drivers/STM32F1xx_HAL_Driver/Inc/Legacy
../Drivers/CMSIS/Device/ST/STM32F1xx/Include
../Drivers/CMSIS/Include
```

编进工程的应用文件：

```
src/main.c
src/app.c
src/announce.c
src/soft_start.c
src/board_stm32.c
lib/rfid_core/src/rfid_protocol.c
lib/rfid_core/src/rfid_reader.c
```

`tts_service.c` 没有加入。应用层用立刻播报，不走库里的语音队列。

## 和 main 分支怎么对应

| 内容 | `main` | `keil` |
|---|---|---|
| 业务源文件 | `src/`、`include/`、`lib/rfid_core/` | 同一套路径 |
| 构建 | `platformio.ini` | `MDK-ARM/2027-control.uvprojx` |
| HAL / CMSIS | PlatformIO 自动拉 | 工程里的 `Drivers/`、`Core/` |
| 说明文档 | `工创赛控制/` | 同一套 |

改参数、改读卡或电机逻辑，仍改 `include/app_config.h` 和 `src/`。不要改 `lib/rfid_core/`，也不要改 `Drivers/`。

## main 改完以后，怎么更新 keil 分支

在已经克隆的仓库里：

```sh
git checkout main
git pull
git checkout keil
git checkout main -- src include lib/rfid_core 工创赛控制 README.md
```

然后打开 Keil，Rebuild，确认没有重复定义、没有找不到头文件。通过后再提交并推送 `keil` 分支。

`Drivers/`、`Core/`、`MDK-ARM/startup_stm32f103x6.s` 不用从 `main` 覆盖。那些文件只存在于 `keil` 分支。

如果 `src/` 新增了 `.c` 文件，还要在 Keil 里把新文件加进工程。只改已有文件的内容，不用动工程树。

要从头生成一份 Keil 树，在能看到旁边「2027 省赛」工程的机器上跑：

```sh
python3 scripts/export_keil_project.py --inplace --keil-readme
```

脚本只拷 HAL、启动文件和工程文件，不改 `src/`、`include/`、`lib/rfid_core/`。

## 不要做的事

- 不要用 Keil 直接打开 `main` 分支。那个分支没有 `.uvprojx`。
- 不要打开仓库旁边那份 2027 省赛 Keil 工程来编这份固件。省赛工程有 FreeRTOS，业务也不是这一套。
- 不要把设备改成 STM32F103C8，也不要把 Flash 改成 64 KB。这块板是 C6，32 KB。
- 不要再往工程里加 Cube 生成的 `stm32f1xx_it.c`。`SysTick_Handler` 和 `USART1_IRQHandler` 会重复定义。
- 不要在 Keil 工程里改 `lib/rfid_core/` 或 `Drivers/`。

## 自己从零搭一份（备用）

只有现成分支不能用时才走这条路。推荐仍用 `keil` 分支。

1. 用 STM32CubeMX 新建工程，芯片选 STM32F103C6Tx。
2. 时钟：外部晶振 HSE 8 MHz，PLL ×9，SYSCLK 72 MHz，APB1 36 MHz，Flash Latency 2。
3. 引脚按 [[01-硬件对照]] 配。USART1 在 Cube 里写成 9600 即可。真正的 115200 由 `board_stm32.c` 在运行时改。
4. 系统节拍选 SysTick。不要选 TIM1 做 HAL 时基。不要勾选 FreeRTOS。
5. 生成到单独目录。不要覆盖本仓库的 `src/`。
6. 在 Keil 里去掉 Cube 的 `main.c`，以及 `stm32f1xx_it.c` 里的 `SysTick_Handler`、`USART1_IRQHandler`。
7. 加入上面列出的应用文件和两条读卡库源文件。
8. 头文件路径、宏、Flash / RAM 按本节「工程已经配好的项」填写。

## 常见报错

| 现象 | 原因 | 处理 |
|---|---|---|
| `SysTick_Handler` multiple definition | Cube 的 `stm32f1xx_it.c` 也编进来了 | 只留 `board_stm32.c` 里的 |
| `USART1_IRQHandler` multiple definition | 同上 | 只留 `board_stm32.c` 里的 |
| 找不到 `app.h` / `rfid_reader.h` | 头文件路径没加 | 按上面的 Include Paths 加 |
| `STM32F103x6` 未定义或外设对不上 | 宏写成了 xB / xE | 改成 `STM32F103x6` |
| 能编译，板子完全没反应 | 按 64 KB 链接，或 HSE 配错 | Flash 用 32 KB；`HSE_VALUE` 用 8 MHz |
| 打开工程提示缺 Pack | 本机没有 STM32F1 器件包 | 用 Pack Installer 安装 `Keil.STM32F1xx_DFP` |

## 相关页

- 编译和烧录：[[05-编译烧录]]
- 硬件对照：[[01-硬件对照]]
- 模块原理：[[07-模块原理]]
