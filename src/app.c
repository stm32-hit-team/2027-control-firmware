#include "app.h"

#include "app_config.h"
#include "board.h"
#include "rfid_reader.h"
#include "tts_service.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static rfid_reader_t g_reader;
static tts_service_t g_tts;

static bool write_rfid(void *context, const uint8_t *data, size_t length)
{
    (void)context;
    return board_rfid_write(data, length);
}

static bool write_tts(void *context, const uint8_t *data, size_t length)
{
    (void)context;
    return board_tts_write(data, length);
}

static bool on_rfid_event(void *context, const rfid_reader_event_t *event)
{
    (void)context;
    if (event->type != RFID_READER_EVENT_TAG) {
        return true;
    }

    if (tts_service_enqueue(&g_tts, event->text, event->text_length)) {
        board_mark_tag(board_millis(), APP_LED_MARK_DURATION_MS);
        return true;
    }
    return false;
}

void app_init(uint32_t now_ms)
{
    rfid_reader_config_t reader_config;
    rfid_reader_default_config(&reader_config);
    reader_config.poll_interval_ms = APP_RFID_POLL_INTERVAL_MS;
    reader_config.response_timeout_ms = APP_RFID_RESPONSE_TIMEOUT_MS;
    reader_config.removal_poll_interval_ms = APP_RFID_REMOVAL_POLL_MS;
    reader_config.dispatch_retry_ms = APP_RFID_DISPATCH_RETRY_MS;
    reader_config.removal_confirmations = APP_RFID_REMOVAL_CONFIRMATIONS;
    reader_config.retry_limit = APP_RFID_RETRY_LIMIT;

    tts_service_init(&g_tts, write_tts, NULL,
                     APP_TTS_SET_SPEED_ON_STARTUP != 0, now_ms);
    rfid_reader_init(&g_reader, &reader_config, write_rfid, on_rfid_event, NULL);
}

void app_process(uint32_t now_ms)
{
    uint8_t byte;
    while (board_rfid_read_byte(&byte)) {
        rfid_reader_feed(&g_reader, byte, now_ms);
    }

    rfid_reader_tick(&g_reader, now_ms);
    tts_service_tick(&g_tts, now_ms);
    board_process(now_ms);
}
