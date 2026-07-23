#include "tts_service.h"

#include <string.h>

#define TTS_STARTUP_DELAY_MS 500U
#define TTS_SPEED_SETTLE_MS 300U
#define TTS_BASE_SPEECH_MS 300U
#define TTS_MS_PER_GB2312_CHARACTER 180U

static bool time_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

static uint32_t estimate_speech_ms(const uint8_t *text, size_t byte_length)
{
    size_t character_count = 0U;
    size_t index = 0U;
    while (index < byte_length) {
        const bool is_gb2312_pair =
            text[index] >= 0xA1U && text[index] <= 0xF7U &&
            index + 1U < byte_length && text[index + 1U] >= 0xA1U &&
            text[index + 1U] <= 0xFEU;
        index += is_gb2312_pair ? 2U : 1U;
        character_count++;
    }
    return TTS_BASE_SPEECH_MS +
           (uint32_t)character_count * TTS_MS_PER_GB2312_CHARACTER;
}

void tts_service_init(tts_service_t *service, tts_write_fn write, void *context,
                      bool set_speed_on_startup, uint32_t now_ms)
{
    memset(service, 0, sizeof(*service));
    service->write = write;
    service->context = context;
    service->speed_command_pending = set_speed_on_startup ? 1U : 0U;
    service->startup_deadline_ms = now_ms + TTS_STARTUP_DELAY_MS;
}

bool tts_service_enqueue(tts_service_t *service, const uint8_t *text,
                         size_t length)
{
    if (service == NULL || text == NULL || length == 0U ||
        length > TTS_TEXT_CAPACITY || service->count >= TTS_QUEUE_DEPTH) {
        return false;
    }

    const size_t tail = (service->head + service->count) % TTS_QUEUE_DEPTH;
    memcpy(service->queue[tail].data, text, length);
    service->queue[tail].length = length;
    service->count++;
    return true;
}

void tts_service_tick(tts_service_t *service, uint32_t now_ms)
{
    static const uint8_t speed_command[] = {'<', 'S', '>', '3'};

    if (service == NULL || service->write == NULL ||
        !time_reached(now_ms, service->busy_until_ms)) {
        return;
    }

    if (service->speed_command_pending != 0U) {
        if (!time_reached(now_ms, service->startup_deadline_ms)) {
            return;
        }
        if (service->write(service->context, speed_command,
                           sizeof(speed_command))) {
            service->speed_command_pending = 0U;
            service->busy_until_ms = now_ms + TTS_SPEED_SETTLE_MS;
        }
        return;
    }

    if (service->count == 0U) {
        return;
    }

    const tts_message_t *message = &service->queue[service->head];
    if (!service->write(service->context, message->data, message->length)) {
        return;
    }
    service->busy_until_ms =
        now_ms + estimate_speech_ms(message->data, message->length);
    service->head = (service->head + 1U) % TTS_QUEUE_DEPTH;
    service->count--;
}
