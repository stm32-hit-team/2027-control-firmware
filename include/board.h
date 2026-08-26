/*
 * board.h：这块板的时钟、串口、指示灯和可选电机。
 */
#ifndef BOARD_H
#define BOARD_H

#include <stddef.h>
#include <stdint.h>

/* 环形缓冲长度。必须是 2 的幂。倒空循环用它当上限。 */
enum {
    BOARD_RFID_RX_RING_SIZE = 128
};

#if (BOARD_RFID_RX_RING_SIZE & (BOARD_RFID_RX_RING_SIZE - 1)) != 0
#error BOARD_RFID_RX_RING_SIZE must be a power of two
#endif

typedef enum {
    BOARD_OK        = 0,
    BOARD_ERR_ARG   = -1,
    BOARD_ERR_EMPTY = -2,
    BOARD_ERR_IO    = -3
} board_status_t;

/* 配时钟、PA8 灯、USART1、USART2，以及可选的 TIM2。
 * 时钟或串口失败会关中断后停住。main 救不了，所以这里不返回状态。 */
void board_init(void);

/* 返回开机后的毫秒数。到 2^32 会翻回 0。 */
uint32_t board_millis(void);

/* 睡到下一次中断。SysTick 或 USART1 会叫醒。 */
void board_idle(void);

/* 往 RFID 串口发 length 字节。data 只借用，不保存。
 * 空指针或长度为 0 返回 BOARD_ERR_ARG，发送超时返回 BOARD_ERR_IO。 */
board_status_t board_rfid_write(const uint8_t *data, size_t length);

/* 从 RFID 缓冲取出一字节写入 *out_byte。
 * 空指针返回 BOARD_ERR_ARG，缓冲空返回 BOARD_ERR_EMPTY。 */
board_status_t board_rfid_read_byte(uint8_t *out_byte);

/* 往语音串口发 length 字节。data 只借用，不保存。
 * 空指针或长度为 0 返回 BOARD_ERR_ARG，发送超时返回 BOARD_ERR_IO。 */
board_status_t board_tts_write(const uint8_t *data, size_t length);

/* 点亮 PA8，并记下 now_ms + duration_ms 时该熄灭。 */
void board_mark_tag(uint32_t now_ms, uint32_t duration_ms);

/* 到点关灯。若编译了电机，同时推进一步电机时序。 */
void board_process(uint32_t now_ms);

/* 返回因缓冲满而丢掉的 RFID 字节数。一直为 0 说明主循环跟得上。 */
uint32_t board_rfid_rx_overflow_count(void);

#endif /* BOARD_H */
