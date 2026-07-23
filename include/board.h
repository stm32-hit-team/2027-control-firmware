#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void board_init(void);
uint32_t board_millis(void);
void board_idle(void);

bool board_rfid_write(const uint8_t *data, size_t length);
bool board_rfid_read_byte(uint8_t *byte);
bool board_tts_write(const uint8_t *data, size_t length);

void board_mark_tag(uint32_t now_ms, uint32_t duration_ms);
void board_process(uint32_t now_ms);

uint32_t board_rfid_rx_overflow_count(void);

#endif
