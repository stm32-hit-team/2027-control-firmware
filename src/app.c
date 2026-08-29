/* app.c：读到一张卡就立刻发这段文字。指示灯跟着闪一下。 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "announce.h"
#include "app.h"
#include "app_config.h"
#include "board.h"
#include "rfid_reader.h"

static rfid_reader_t g_reader;
static announce_t g_announce;

/* 用 app_config.h 覆盖库的默认读卡节奏。config 不能为空。 */
static void app_load_reader_config(rfid_reader_config_t *config);

/* 适配 rfid_reader_default_config。config 不能为空。 */
static void app_fill_default_reader_config(rfid_reader_config_t *config);

/* 把固件里的读卡间隔写进 config。config 不能为空。 */
static void app_apply_reader_timing(rfid_reader_config_t *config);

/* 倒空 RFID 环形缓冲。
 * 上限等于缓冲长度，避免中断一直进数时在这里转圈。 */
static void app_drain_rfid_rx(uint32_t now_ms);

/*
 * 适配 board_rfid_write。只有 BOARD_OK 才返回 true。
 * 读卡库的回调类型是 bool，签名不能改。
 */
static bool app_write_rfid(void *context, const uint8_t *data, size_t length);

/*
 * 适配 board_tts_write。只有 BOARD_OK 才返回 true。
 * announce 的回调类型是 bool，签名不能改。
 */
static bool app_write_tts(void *context, const uint8_t *data, size_t length);

/*
 * 读卡事件回调。库规定返回 bool。event 为空时返回 true，避免卡死重试。
 * 读卡库的回调类型是 bool，签名不能改。
 */
static bool app_on_rfid_event(void *context, const rfid_reader_event_t *event);

/* 判断是否为到点刷卡事件。event 不能为空。 */
static bool app_event_is_tag(const rfid_reader_event_t *event);

/* 处理一张到点的卡。发送失败返回 false，读卡器会再送。
 * event 不能为空。 */
static bool app_accept_tag(const rfid_reader_event_t *event);

/* 适配 announce_speak_now。event 不能为空。 */
static bool app_speak_tag_text(const rfid_reader_event_t *event);

/* 适配 board_mark_tag，点一下 PA8。 */
static void app_mark_seen_tag(void);

/* 规范要求返回状态枚举。
 * 主循环是事件泵，救不了初始化失败，所以不返回状态。 */
void app_init(uint32_t now_ms)
{
    rfid_reader_config_t reader_config = {0};

    app_load_reader_config(&reader_config);

    /* 读卡器一启动就可能上报卡片，播报必须先能发。 */
    announce_init(&g_announce, app_write_tts, NULL,
                  APP_TTS_SET_SPEED_ON_STARTUP != 0, now_ms);
    rfid_reader_init(&g_reader, &reader_config, app_write_rfid,
                     app_on_rfid_event, NULL);
}

/* 规范要求返回状态枚举。事件泵每一圈都要继续转，所以不返回状态。 */
void app_process(uint32_t now_ms)
{
    app_drain_rfid_rx(now_ms);
    rfid_reader_tick(&g_reader, now_ms);
    announce_tick(&g_announce, now_ms);
    board_process(now_ms);
}

static void app_load_reader_config(rfid_reader_config_t *config)
{
    assert(config != NULL);

    app_fill_default_reader_config(config);
    app_apply_reader_timing(config);
}

static void app_fill_default_reader_config(rfid_reader_config_t *config)
{
    assert(config != NULL);
    rfid_reader_default_config(config);
}

static void app_apply_reader_timing(rfid_reader_config_t *config)
{
    assert(config != NULL);

    config->poll_interval_ms = APP_RFID_POLL_INTERVAL_MS;
    config->response_timeout_ms = APP_RFID_RESPONSE_TIMEOUT_MS;
    config->removal_poll_interval_ms = APP_RFID_REMOVAL_POLL_MS;
    config->dispatch_retry_ms = APP_RFID_DISPATCH_RETRY_MS;
    config->removal_confirmations = APP_RFID_REMOVAL_CONFIRMATIONS;
    config->retry_limit = APP_RFID_RETRY_LIMIT;

    assert(config->poll_interval_ms == APP_RFID_POLL_INTERVAL_MS);
    assert(config->retry_limit == APP_RFID_RETRY_LIMIT);
}

static void app_drain_rfid_rx(uint32_t now_ms)
{
    for (size_t i = 0; i < (size_t)BOARD_RFID_RX_RING_SIZE; ++i) {
        uint8_t byte = 0;

        if (board_rfid_read_byte(&byte) != BOARD_OK)
            break;
        rfid_reader_feed(&g_reader, byte, now_ms);
    }
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

static bool app_on_rfid_event(void *context, const rfid_reader_event_t *event)
{
    (void)context;

    if (event == NULL)
        return true;
    if (!app_event_is_tag(event))
        return true;
    return app_accept_tag(event);
}

static bool app_event_is_tag(const rfid_reader_event_t *event)
{
    assert(event != NULL);
    return event->type == RFID_READER_EVENT_TAG;
}

static bool app_accept_tag(const rfid_reader_event_t *event)
{
    assert(event != NULL);

    if (!app_speak_tag_text(event))
        return false;
    app_mark_seen_tag();
    return true;
}

static bool app_speak_tag_text(const rfid_reader_event_t *event)
{
    assert(event != NULL);
    return announce_speak_now(&g_announce, event->text, event->text_length);
}

static void app_mark_seen_tag(void)
{
    board_mark_tag(board_millis(), APP_LED_MARK_DURATION_MS);
}
