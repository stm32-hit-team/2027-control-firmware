/*
 * app.h：读卡结果送到语音。不碰寄存器。
 */
#ifndef APP_H
#define APP_H

#include <stdint.h>

/* 先准备语音，再准备读卡。now_ms 是开机后的毫秒数，给内部定时对表。 */
void app_init(uint32_t now_ms);

/* 倒空 RFID 接收缓冲，并推进一步读卡、语音和板级定时。
 * 不阻塞。now_ms 是当前毫秒数。 */
void app_process(uint32_t now_ms);

#endif /* APP_H */
