#ifndef TTS_SERVICE_H
#define TTS_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TTS_TEXT_CAPACITY 64U
#define TTS_QUEUE_DEPTH 8U

typedef bool (*tts_write_fn)(void *context, const uint8_t *data, size_t length);

typedef struct {
    uint8_t data[TTS_TEXT_CAPACITY];
    size_t length;
} tts_message_t;

typedef struct {
    tts_write_fn write;
    void *context;
    tts_message_t queue[TTS_QUEUE_DEPTH];
    size_t head;
    size_t count;
    uint32_t busy_until_ms;
    uint32_t startup_deadline_ms;
    uint8_t speed_command_pending;
} tts_service_t;

void tts_service_init(tts_service_t *service, tts_write_fn write, void *context,
                      bool set_speed_on_startup, uint32_t now_ms);
bool tts_service_enqueue(tts_service_t *service, const uint8_t *text,
                         size_t length);
void tts_service_tick(tts_service_t *service, uint32_t now_ms);

#endif
