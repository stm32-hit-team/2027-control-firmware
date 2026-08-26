# include/board.h

这块板的时钟、串口、指示灯和可选电机。

源文件：`include/board.h`

## 这个文件管什么

声明板级对外接口。实现见 [[src-board_stm32.c]]。

## 常量

| 名字 | 值 | 说明 |
|---|---|---|
| `BOARD_RFID_RX_RING_SIZE` | 128 | 环形缓冲长度，必须是 2 的幂 |

## 状态码

| 名字 | 值 | 说明 |
|---|---|---|
| `BOARD_OK` | 0 | 成功 |
| `BOARD_ERR_ARG` | -1 | 参数不对 |
| `BOARD_ERR_EMPTY` | -2 | 缓冲空 |
| `BOARD_ERR_IO` | -3 | 发送超时 |

## 函数

### board_init

```c
void board_init(void);
```

配时钟、PA8 灯、USART1、USART2，以及可选的 TIM2。失败会停住。

### board_millis

```c
uint32_t board_millis(void);
```

返回开机后的毫秒数。

### board_idle

```c
void board_idle(void);
```

睡到下一次中断。

### board_rfid_write

```c
board_status_t board_rfid_write(const uint8_t *data, size_t length);
```

往 RFID 串口发数据。

### board_rfid_read_byte

```c
board_status_t board_rfid_read_byte(uint8_t *out_byte);
```

从 RFID 缓冲取一字节。

### board_tts_write

```c
board_status_t board_tts_write(const uint8_t *data, size_t length);
```

往语音串口发数据。

### board_mark_tag

```c
void board_mark_tag(uint32_t now_ms, uint32_t duration_ms);
```

点亮 PA8，并记下熄灭时刻。

### board_process

```c
void board_process(uint32_t now_ms);
```

到点关灯，可选推电机。

### board_rfid_rx_overflow_count

```c
uint32_t board_rfid_rx_overflow_count(void);
```

返回丢字节的累计次数。

## 相关页

- [[src-board_stm32.c]]
- [[01-硬件对照]]
