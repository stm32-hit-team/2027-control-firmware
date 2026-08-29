#ifndef APP_H
#define APP_H

#include <stdint.h>

/*
 * 先准备语音，再准备读卡。now_ms 是开机后的毫秒数，给内部定时对表。
 * 规范要求返回状态枚举。
 * 主循环是事件泵，救不了初始化失败，所以不返回状态。
 */
void app_init(uint32_t now_ms);

/*
 * 倒空 RFID 接收缓冲，再推进一步读卡、语音和板级定时。
 * 不阻塞。now_ms 是当前毫秒数。
 * 规范要求返回状态枚举。事件泵每一圈都要继续转，所以不返回状态。
 */
void app_process(uint32_t now_ms);

#endif /* APP_H */
