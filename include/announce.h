#ifndef ANNOUNCE_H
#define ANNOUNCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    /* 须与只读库 tts_service.c 里的 TTS_STARTUP_DELAY_MS 保持同一数值。 */
    ANNOUNCE_STARTUP_DELAY_MS = 500
};

/*
 * 计划要求可注入写回调，本机测试和板上串口共用同一套代码。
 * 规范要求函数指针只出现在 static const 分发表里。
 * 这里按计划保留注入点。
 */
typedef bool (*announce_write_fn)(void *context, const uint8_t *data,
                                  size_t length);

/*
 * 立刻播报的状态。没有队列，也没有 busy_until_ms。
 * speed_command_pending 为 1 时，下一次发送先发语速。
 */
typedef struct {
    announce_write_fn write;
    void *context;
    uint32_t startup_deadline_ms;
    uint8_t speed_command_pending;
} announce_t;

/*
 * 记下写回调和开机语速。service 不能为空。
 * 规范要求最多 4 个参数，并返回状态枚举。
 * 计划沿用 tts_service_init 的五参数签名，且不返回状态。
 */
void announce_init(announce_t *service, announce_write_fn write, void *context,
                   bool set_speed_on_startup, uint32_t now_ms);

/*
 * 立刻发送 text / length。语速若还没发，先发语速。
 * service、text 不能为空，length 不能为 0。正文发出去返回 true。
 * 规范要求返回状态枚举。计划规定成功失败都用 bool。
 */
bool announce_speak_now(announce_t *service, const uint8_t *text,
                        size_t length);

/*
 * 语速仍待发且已到开机延时时，发一次语速。不发送正文。
 * 规范要求返回状态枚举。计划沿用 tts_service_tick 的 void。
 */
void announce_tick(announce_t *service, uint32_t now_ms);

#endif
