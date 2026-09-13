# 2027 工创赛 RFID 电动车控制固件（Keil 工程）

仓库：<https://github.com/stm32-hit-team/2027-control-firmware>

**这是 `keil` 分支。** 用 Keil 5（MDK-ARM）打开下面这个文件即可：

```
MDK-ARM/2027-control.uvprojx
```

这里没有 `platformio.ini`，也不依赖 PlatformIO。日常开发仍在 [`main`](https://github.com/stm32-hit-team/2027-control-firmware/tree/main) 分支。

更细的说明在 [`工创赛控制/`](工创赛控制/index.md)。转到 Keil 的专页是 [工创赛控制/08-Keil工程.md](工创赛控制/08-Keil工程.md)。

---

## 1. 打开、编译、烧录

1. 安装 Keil 5，并用 Pack Installer 装好 `Keil.STM32F1xx_DFP`。
2. 克隆本仓库后切到这个分支：

```sh
git clone https://github.com/stm32-hit-team/2027-control-firmware.git
cd 2027-control-firmware
git checkout keil
```

3. 双击 `MDK-ARM/2027-control.uvprojx`。
4. 菜单 Rebuild。
5. 用 ST-Link 接 `SWD`（`PA13`、`PA14`、`NRST`、地、3.3 V），再 Download。

芯片是 **STM32F103C6**。Flash 32 KB，RAM 10 KB。不要改成 C8。

烧完芯片会复位。约 3 秒后电机可能转，先架空车轮。

---

## 2. 说明文档

| 页 | 讲什么 |
|---|---|
| [文档首页](工创赛控制/index.md) | 从哪看起 |
| [总览](工创赛控制/00-总览.md) | 产品做什么、分几层 |
| [硬件对照](工创赛控制/01-硬件对照.md) | 插座、开关、单片机脚 |
| [主循环](工创赛控制/02-主循环.md) | 上电后程序怎么转 |
| [读卡到播报](工创赛控制/03-读卡到播报.md) | 一张卡从读到念 |
| [模块原理](工创赛控制/07-模块原理.md) | RFID、语音、缓启动、电机、灯 |
| [转到 Keil](工创赛控制/08-Keil工程.md) | 本分支怎么用，以及和 `main` 怎么对应 |
| [故障排查](工创赛控制/06-故障排查.md) | 灯、卡、语音、电机常见问题 |

---

## 3. 代码里有哪些部分

应用层在 `src/`、`include/`。读卡协议和状态机在 `lib/rfid_core/`，不要改。HAL 和 CMSIS 在 `Drivers/`，不要改。

```
main.c          上电入口
app.c           读到卡就立刻发文字，并点灯
announce.c      立刻播报。没有队列
soft_start.c    把旋钮 ADC 换成停车毫秒数
board_stm32.c   时钟、脚、串口、旋钮、电机、灯
rfid_protocol   串口帧的拼和拆（库）
rfid_reader     寻卡、读块、去重、确认拿走（库）
```

要点：

- RFID 走 USART1。上电先 9600 改速，再 115200 工作。
- 语音走 USART2，9600。读到就念，下一张不同 UID 的卡会打断上一句。
- 缓启动：上电先等 3 秒，再约 3 秒爬到巡航。停车时间由 `PB1` 旋钮决定。
- 旋钮只管「跑多久后停」，不管「等多久才起步」。

原理见 [模块原理](工创赛控制/07-模块原理.md)。

---

## 4. 工程已经配好的项

| 项 | 值 |
|---|---|
| 芯片 | STM32F103C6 |
| IROM1 | `0x08000000`，`0x8000` |
| IRAM1 | `0x20000000`，`0x2800` |
| 宏 | `USE_HAL_DRIVER`、`STM32F103x6`、`APP_ENABLE_MOTOR=1` |
| C 语言 | C99 |
| 启动文件 | `startup_stm32f103x6.s` |
| 调试器 | ST-Link，SWD |

`tts_service.c` 没有加入工程。应用层用立刻播报。

改参数改 `include/app_config.h`，然后 Rebuild。

---

## 5. 和 main 分支怎么同步

`main` 改了 `src/`、`include/`、`lib/rfid_core/` 之后：

```sh
git checkout keil
git checkout main -- src include lib/rfid_core 工创赛控制
```

然后 Rebuild。`Drivers/`、`Core/`、启动文件不用从 `main` 覆盖。

如果 `src/` 新增了 `.c` 文件，还要在 Keil 里把新文件加进工程。

---

## 6. 常见问题

**打开工程提示缺 Pack**  
用 Pack Installer 安装 `Keil.STM32F1xx_DFP`。

**`SysTick_Handler` 重复定义**  
不要再加入 Cube 生成的 `stm32f1xx_it.c`。

**能编译，板子没反应**  
确认设备是 C6，Flash 是 32 KB，`HSE_VALUE` 是 8 MHz。

**灯、卡、语音、电机**  
见 [故障排查](工创赛控制/06-故障排查.md)。

不要打开仓库旁边那份 2027 省赛 Keil 工程来编这份固件。
