/*
 * app.c：把一张卡的文字送到语音队列，并点一下指示灯。
 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app.h"
#include "app_config.h"
#include "board.h"
#include "rfid_reader.h"
#include "tts_service.h"

static rfid_reader_t g_reader;
static tts_service_t g_tts;

/* 适配 board_rfid_write。只有 BOARD_OK 才返回 true。
 * 读卡库的回调类型是 bool，签名不能改。 */
static bool app_write_rfid(void *context, const uint8_t *data, size_t length);

/* 适配 board_tts_write。只有 BOARD_OK 才返回 true。
 * 语音库的回调类型是 bool，签名不能改。 */
static bool app_write_tts(void *context, const uint8_t *data, size_t length);

/* 文字入队并点灯。队列满返回 false，读卡器会保住这张卡再送。
 * event 不能为空。 */
static bool app_accept_tag(const rfid_reader_event_t *event);

/* 读卡事件回调。库规定返回 bool。event 为空时返回 true，避免卡死重试。 */
static bool app_on_rfid_event(void *context, const rfid_reader_event_t *event);

/* 用 app_config.h 覆盖库的默认读卡节奏。config 不能为空。 */
static void app_load_reader_config(rfid_reader_config_t *config);

void app_init(uint32_t now_ms)
{
    rfid_reader_config_t reader_config;

    app_load_reader_config(&reader_config);

    /* 读卡器一启动就可能上报卡片，语音必须先能收。 */
    tts_service_init(&g_tts, app_write_tts, NULL,
                     APP_TTS_SET_SPEED_ON_STARTUP != 0, now_ms);
    rfid_reader_init(&g_reader, &reader_config, app_write_rfid,
                     app_on_rfid_event, NULL);
}

void app_process(uint32_t now_ms)
{
    /* 上限等于环形缓冲长度，避免中断一直进数时在这里转圈。 */
    for (size_t i = 0; i < (size_t)BOARD_RFID_RX_RING_SIZE; ++i) {
        uint8_t byte = 0;

        if (board_rfid_read_byte(&byte) != BOARD_OK)
            break;
        rfid_reader_feed(&g_reader, byte, now_ms);
    }

    rfid_reader_tick(&g_reader, now_ms);
    tts_service_tick(&g_tts, now_ms);
    board_process(now_ms);
}

static bool app_write_rfid(void *context, const uint8_t *data, size_t length)
{
    (void)context;
    return board_rfid_write(data, length) == BOARD_OK;
}

static bool app_write_tts(void *context, const uint8_t *data, size_t length)
{
    (void)context;
    return board_tts_write(data, length) == BOARD_OK;
}

static bool app_accept_tag(const rfid_reader_event_t *event)
{
    assert(event != NULL);

    if (!tts_service_enqueue(&g_tts, event->text, event->text_length))
        return false;

    board_mark_tag(board_millis(), APP_LED_MARK_DURATION_MS);
    return true;
}

static bool app_on_rfid_event(void *context, const rfid_reader_event_t *event)
{
    (void)context;

    if (event == NULL)
        return true;
    if (event->type != RFID_READER_EVENT_TAG)
        return true;
    return app_accept_tag(event);
}

static void app_load_reader_config(rfid_reader_config_t *config)
{
    assert(config != NULL);

    rfid_reader_default_config(config);
    config->poll_interval_ms = APP_RFID_POLL_INTERVAL_MS;
    config->response_timeout_ms = APP_RFID_RESPONSE_TIMEOUT_MS;
    config->removal_poll_interval_ms = APP_RFID_REMOVAL_POLL_MS;
    config->dispatch_retry_ms = APP_RFID_DISPATCH_RETRY_MS;
    config->removal_confirmations = APP_RFID_REMOVAL_CONFIRMATIONS;
    config->retry_limit = APP_RFID_RETRY_LIMIT;
}
