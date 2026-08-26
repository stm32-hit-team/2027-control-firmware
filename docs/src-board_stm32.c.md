# src/board_stm32.c

这块板的时钟、串口、指示灯和可选电机。

源文件：`src/board_stm32.c`

## 这个文件管什么

它是板级层。直接碰 HAL 和寄存器，上面的应用层不碰。

引脚按工创赛 26 新板网表，不按丝印图。对照见 [[01-硬件对照]]。

## 常量

| 名字 | 值 | 说明 |
|---|---|---|
| `BOARD_UART_TX_TIMEOUT_MS` | 200 | 串口发送超时 |
| `BOARD_USART1_IRQ_PRIORITY` | 5 | USART1 中断优先级 |
| `BOARD_MOTOR_TIM_PRESCALER` | 71 | TIM2 分频 |
| `BOARD_LED_PIN` | `GPIO_PIN_8` | 指示灯脚 |
| `BOARD_UART_TX_PINS` | `PA9`（RFID）、`PA2`（播报） | 两路 TX |
| `BOARD_UART_RX_PINS` | `PA10`（RFID）、`PA3`（播报） | 两路 RX |
| `BOARD_MOTOR_PWM_PINS` | `PA0`、`PA1` | 电机 PWM |

环形缓冲长度在 [[include-board.h]] 里，是 128。

## 静态变量

| 名字 | 说明 |
|---|---|
| `g_rfid_uart` | USART1 句柄 |
| `g_tts_uart` | USART2 句柄 |
| `g_rfid_rx_byte` | 中断每次收的一字节 |
| `g_rfid_rx_ring` | 环形缓冲本体 |
| `g_rfid_rx_head` | 中断写位置 |
| `g_rfid_rx_tail` | 主循环读位置 |
| `g_rfid_rx_overflows` | 丢字节计数 |
| `g_led_on` | 灯当前状态 |
| `g_led_off_deadline_ms` | 灯该熄灭的时刻 |

中断写 `head`，主循环改 `tail`，各管一个下标，所以不加锁。

## 对外函数

### board_init

```c
void board_init(void)
```

配时钟、PA8 灯、USART1、USART2，可选 TIM2。

时钟或串口失败会关中断后停住。`main` 救不了，所以不返回状态。

### board_millis

```c
uint32_t board_millis(void)
```

返回开机后的毫秒数。到 2^32 会翻回 0。

### board_idle

```c
void board_idle(void)
```

睡到下一次中断。SysTick 或 USART1 会叫醒。

### board_rfid_write

```c
board_status_t board_rfid_write(const uint8_t *data, size_t length)
```

往 RFID 串口发数据。空指针或长度为 0 返回 `BOARD_ERR_ARG`，超时返回 `BOARD_ERR_IO`。

### board_rfid_read_byte

```c
board_status_t board_rfid_read_byte(uint8_t *out_byte)
```

从 RFID 缓冲取一字节。空指针返回 `BOARD_ERR_ARG`，缓冲空返回 `BOARD_ERR_EMPTY`。

### board_tts_write

```c
board_status_t board_tts_write(const uint8_t *data, size_t length)
```

往语音串口发数据。错误码和 `board_rfid_write` 一样。

### board_mark_tag

```c
void board_mark_tag(uint32_t now_ms, uint32_t duration_ms)
```

点亮 PA8，并记下 `now_ms + duration_ms` 时该熄灭。

### board_process

```c
void board_process(uint32_t now_ms)
```

到点关灯。开了电机就推一步电机时序。

### board_rfid_rx_overflow_count

```c
uint32_t board_rfid_rx_overflow_count(void)
```

返回丢字节的累计次数。一直为 0 说明主循环跟得上。

## 内部函数

### board_time_reached

```c
static bool board_time_reached(uint32_t now_ms, uint32_t deadline_ms)
```

判断时间是否到点。先做无符号减，再转有符号比较。毫秒计数翻回 0 时仍然正确。

### board_fail_stop

```c
static void board_fail_stop(void)
```

时钟或串口配失败时停住。继续跑只会得到乱行为。

### board_configure_clock

```c
static void board_configure_clock(void)
```

外部 8 MHz 晶振，PLL 九倍到 72 MHz。APB1 必须是 36 MHz。

### board_configure_led

```c
static void board_configure_led(void)
```

配 PA8 为推挽输出。先写高再配模式，避免上电闪一下。

### board_led_write / board_led_on / board_led_off

写 PA8 电平、点亮、熄灭。灯按低电平点亮。

### board_configure_uart_gpio

```c
static void board_configure_uart_gpio(void)
```

配两路串口的脚。TX 是复用推挽，RX 是浮空输入。

### board_fill_uart

```c
static void board_fill_uart(UART_HandleTypeDef *uart, USART_TypeDef *inst, uint32_t baud)
```

填一路串口的参数：9600、8 位、1 停止、无校验。

### board_configure_rfid_uart / board_configure_tts_uart

分别配 USART1 和 USART2。失败就停住。

### board_arm_rfid_irq

```c
static void board_arm_rfid_irq(void)
```

打开 USART1 接收中断，并启动第一次单字节接收。

### board_configure_uarts

```c
static void board_configure_uarts(void)
```

串口配置的总入口：先脚，再两路串口，最后开中断。

### board_validate_tx

```c
static board_status_t board_validate_tx(const uint8_t *data, size_t length)
```

发送前检查：空指针、长度为 0、长度超过 `UINT16_MAX` 都拒掉。

### board_uart_send

```c
static board_status_t board_uart_send(UART_HandleTypeDef *uart, const uint8_t *data, size_t length)
```

调 HAL 发送。超时返回 `BOARD_ERR_IO`。

### board_ring_next / board_ring_push / board_ring_take

环形缓冲的三个操作：下一个下标、中断里存、主循环取。

### board_motor_apply / board_configure_motor / board_process_motor

电机相关，只在 `APP_ENABLE_MOTOR` 打开时编译。

- `board_motor_apply`：写占空比。0 表示停车，两路同电位。
- `board_configure_motor`：配 TIM2 两路 PWM，约 1 kHz。
- `board_process_motor`：先停 3 秒，再 1 秒升速，最长 10 分钟自动停。

## 中断入口

这三个名字是启动文件和 HAL 按符号找的，不能改。

| 函数 | 做什么 |
|---|---|
| `USART1_IRQHandler` | USART1 中断，转给 HAL |
| `SysTick_Handler` | 每 1 ms 累加毫秒计数 |
| `HAL_UART_RxCpltCallback` | 收完一字节，存进环形缓冲，再启动下一次 |
| `HAL_UART_ErrorCallback` | 串口出错后重启接收 |

`HAL_UART_RxCpltCallback` 里重启接收这一步不能漏。漏了就再也收不到数据。

## 和其它文件的关系

- 对外接口：[[include-board.h]]
- 配置数字：[[include-app_config.h]]
- 应用层调用：[[src-app.c]]

## 相关页

- [[01-硬件对照]]
- [[02-主循环]]
- [[06-故障排查]]
