#include "rfid_protocol.h"
#include "rfid_reader.h"
#include "tts_service.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ARRAY_LEN(array) (sizeof(array) / sizeof((array)[0]))

static unsigned g_failures;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,           \
                    #condition);                                                \
            g_failures++;                                                       \
        }                                                                       \
    } while (0)

typedef struct {
    uint8_t writes[32][RFID_PROTOCOL_MAX_ENCODED_SIZE];
    size_t write_lengths[32];
    size_t write_count;
    rfid_reader_event_t events[16];
    size_t event_count;
    size_t rejected_tag_callbacks;
} reader_fixture_t;

typedef struct {
    uint8_t writes[8][TTS_TEXT_CAPACITY];
    size_t write_lengths[8];
    size_t write_count;
} tts_fixture_t;

static bool capture_rfid_write(void *context, const uint8_t *data, size_t length)
{
    reader_fixture_t *fixture = context;

    CHECK(fixture->write_count < ARRAY_LEN(fixture->writes));
    CHECK(length <= sizeof(fixture->writes[0]));
    if (fixture->write_count >= ARRAY_LEN(fixture->writes) ||
        length > sizeof(fixture->writes[0])) {
        return false;
    }

    memcpy(fixture->writes[fixture->write_count], data, length);
    fixture->write_lengths[fixture->write_count] = length;
    fixture->write_count++;
    return true;
}

static bool capture_reader_event(void *context, const rfid_reader_event_t *event)
{
    reader_fixture_t *fixture = context;

    CHECK(fixture->event_count < ARRAY_LEN(fixture->events));
    if (fixture->event_count < ARRAY_LEN(fixture->events)) {
        fixture->events[fixture->event_count++] = *event;
    }
    if (event->type == RFID_READER_EVENT_TAG &&
        fixture->rejected_tag_callbacks > 0U) {
        fixture->rejected_tag_callbacks--;
        return false;
    }
    return true;
}

static bool capture_tts_write(void *context, const uint8_t *data, size_t length)
{
    tts_fixture_t *fixture = context;

    CHECK(fixture->write_count < ARRAY_LEN(fixture->writes));
    CHECK(length <= sizeof(fixture->writes[0]));
    if (fixture->write_count >= ARRAY_LEN(fixture->writes) ||
        length > sizeof(fixture->writes[0])) {
        return false;
    }

    memcpy(fixture->writes[fixture->write_count], data, length);
    fixture->write_lengths[fixture->write_count] = length;
    fixture->write_count++;
    return true;
}

static size_t make_response(uint8_t command, uint8_t status,
                            const uint8_t *payload, size_t payload_length,
                            uint8_t *encoded, size_t capacity)
{
    uint8_t body[RFID_PROTOCOL_MAX_BODY_SIZE];

    CHECK(payload_length + 3U <= sizeof(body));
    body[0] = 0x00U;
    body[1] = command;
    body[2] = status;
    if (payload_length > 0U) {
        memcpy(&body[3], payload, payload_length);
    }
    return rfid_protocol_encode_body(body, payload_length + 3U, encoded,
                                     capacity);
}

static size_t make_uid_response(const uint8_t uid[RFID_UID_SIZE], uint8_t status,
                                uint8_t *encoded, size_t capacity)
{
    uint8_t payload[2U + RFID_UID_SIZE] = {0x04U, 0x00U};

    memcpy(&payload[2], uid, RFID_UID_SIZE);
    return make_response(RFID_RESPONSE_UID, status, payload, sizeof(payload),
                         encoded, capacity);
}

static size_t make_block_response(const uint8_t uid[RFID_UID_SIZE],
                                  const uint8_t block[RFID_BLOCK_SIZE],
                                  uint8_t *encoded, size_t capacity)
{
    uint8_t payload[2U + RFID_UID_SIZE + RFID_BLOCK_SIZE] = {0x04U, 0x00U};

    memcpy(&payload[2], uid, RFID_UID_SIZE);
    memcpy(&payload[2U + RFID_UID_SIZE], block, RFID_BLOCK_SIZE);
    return make_response(RFID_RESPONSE_BLOCK, RFID_STATUS_OK, payload,
                         sizeof(payload), encoded, capacity);
}

static void feed_reader(rfid_reader_t *reader, const uint8_t *frame,
                        size_t length, uint32_t now_ms)
{
    for (size_t i = 0; i < length; ++i) {
        rfid_reader_feed(reader, frame[i], now_ms);
    }
}

static void test_known_request_vectors(void)
{
    uint8_t frame[RFID_PROTOCOL_MAX_ENCODED_SIZE];
    static const uint8_t expected_uid[] = {0x7FU, 0x03U, 0x00U, 0x10U, 0x13U};
    static const uint8_t expected_block4[] = {
        0x7FU, 0x04U, 0x00U, 0x11U, 0x04U, 0x11U,
    };

    size_t length = rfid_protocol_encode_read_uid(frame, sizeof(frame));
    CHECK(length == sizeof(expected_uid));
    CHECK(memcmp(frame, expected_uid, sizeof(expected_uid)) == 0);

    length = rfid_protocol_encode_read_block(4U, frame, sizeof(frame));
    CHECK(length == sizeof(expected_block4));
    CHECK(memcmp(frame, expected_block4, sizeof(expected_block4)) == 0);
}

static void test_parser_checksum_escape_and_recovery(void)
{
    const uint8_t uid[RFID_UID_SIZE] = {0x12U, 0x7FU, 0x34U, 0x56U};
    uint8_t encoded[RFID_PROTOCOL_MAX_ENCODED_SIZE];
    rfid_protocol_parser_t parser;
    rfid_frame_t frame;

    const size_t length = make_uid_response(uid, RFID_STATUS_OK, encoded,
                                            sizeof(encoded));
    CHECK(length > 0U);
    CHECK(memchr(&encoded[1], 0x7FU, length - 1U) != NULL);

    rfid_protocol_parser_init(&parser);
    rfid_parse_result_t result = RFID_PARSE_NONE;
    for (size_t i = 0; i < length; ++i) {
        result = rfid_protocol_parser_feed(&parser, encoded[i], &frame);
    }
    CHECK(result == RFID_PARSE_FRAME);
    CHECK(frame.command == RFID_RESPONSE_UID);
    CHECK(frame.status == RFID_STATUS_OK);
    CHECK(frame.payload_length == 2U + RFID_UID_SIZE);
    CHECK(memcmp(&frame.payload[2], uid, RFID_UID_SIZE) == 0);

    encoded[length - 1U] ^= 0x01U;
    rfid_protocol_parser_init(&parser);
    result = RFID_PARSE_NONE;
    for (size_t i = 0; i < length; ++i) {
        result = rfid_protocol_parser_feed(&parser, encoded[i], &frame);
    }
    CHECK(result == RFID_PARSE_ERROR);

    const size_t valid_length = make_uid_response(uid, RFID_STATUS_OK, encoded,
                                                  sizeof(encoded));
    result = RFID_PARSE_NONE;
    for (size_t i = 0; i < valid_length; ++i) {
        result = rfid_protocol_parser_feed(&parser, encoded[i], &frame);
    }
    CHECK(result == RFID_PARSE_FRAME);
}

static void test_reader_reads_text_once_until_card_removal(void)
{
    static const uint8_t expected_poll[] = {0x7FU, 0x03U, 0x00U, 0x10U, 0x13U};
    static const uint8_t uid[RFID_UID_SIZE] = {0x11U, 0x22U, 0x33U, 0x44U};
    static const uint8_t text[] = {0xD1U, 0xD3U, 0xB0U, 0xB2U}; /* GB2312: 延安 */
    uint8_t block[RFID_BLOCK_SIZE] = {0};
    uint8_t response[RFID_PROTOCOL_MAX_ENCODED_SIZE];
    reader_fixture_t fixture = {0};
    rfid_reader_t reader;
    rfid_reader_config_t config;

    memcpy(block, text, sizeof(text));
    rfid_reader_default_config(&config);
    config.poll_interval_ms = 50U;
    config.response_timeout_ms = 30U;
    config.removal_poll_interval_ms = 50U;
    config.removal_confirmations = 3U;
    rfid_reader_init(&reader, &config, capture_rfid_write,
                     capture_reader_event, &fixture);

    rfid_reader_tick(&reader, 0U);
    CHECK(fixture.write_count == 1U);
    CHECK(fixture.write_lengths[0] == sizeof(expected_poll));
    CHECK(memcmp(fixture.writes[0], expected_poll, sizeof(expected_poll)) == 0);

    size_t length = make_uid_response(uid, RFID_STATUS_OK, response,
                                      sizeof(response));
    feed_reader(&reader, response, length, 1U);
    CHECK(fixture.write_count == 2U);
    CHECK(fixture.writes[1][4] == 4U);

    length = make_block_response(uid, block, response, sizeof(response));
    feed_reader(&reader, response, length, 2U);
    CHECK(fixture.event_count == 1U);
    CHECK(fixture.events[0].type == RFID_READER_EVENT_TAG);
    CHECK(fixture.events[0].text_length == sizeof(text));
    CHECK(memcmp(fixture.events[0].text, text, sizeof(text)) == 0);

    rfid_reader_tick(&reader, 52U);
    CHECK(fixture.write_count == 3U);
    length = make_uid_response(uid, RFID_STATUS_OK, response, sizeof(response));
    feed_reader(&reader, response, length, 53U);
    rfid_reader_tick(&reader, 103U);
    length = make_uid_response(uid, RFID_STATUS_OK, response, sizeof(response));
    feed_reader(&reader, response, length, 104U);
    CHECK(fixture.event_count == 1U);

    for (uint32_t now = 154U; now <= 256U; now += 51U) {
        rfid_reader_tick(&reader, now);
        length = make_uid_response(uid, RFID_STATUS_NO_CARD, response,
                                   sizeof(response));
        feed_reader(&reader, response, length, now + 1U);
    }

    rfid_reader_tick(&reader, 307U);
    length = make_uid_response(uid, RFID_STATUS_OK, response, sizeof(response));
    feed_reader(&reader, response, length, 308U);
    length = make_block_response(uid, block, response, sizeof(response));
    feed_reader(&reader, response, length, 309U);
    CHECK(fixture.event_count == 2U);
}

static void test_different_uid_with_same_text_is_not_suppressed(void)
{
    static const uint8_t uid_a[RFID_UID_SIZE] = {1U, 2U, 3U, 4U};
    static const uint8_t uid_b[RFID_UID_SIZE] = {5U, 6U, 7U, 8U};
    static const uint8_t text[] = {0xB1U, 0xB1U, 0xBEU, 0xA9U}; /* 北京 */
    uint8_t block[RFID_BLOCK_SIZE] = {0};
    uint8_t response[RFID_PROTOCOL_MAX_ENCODED_SIZE];
    reader_fixture_t fixture = {0};
    rfid_reader_t reader;
    rfid_reader_config_t config;

    memcpy(block, text, sizeof(text));
    rfid_reader_default_config(&config);
    rfid_reader_init(&reader, &config, capture_rfid_write,
                     capture_reader_event, &fixture);

    rfid_reader_tick(&reader, 0U);
    size_t length = make_uid_response(uid_a, RFID_STATUS_OK, response,
                                      sizeof(response));
    feed_reader(&reader, response, length, 1U);
    length = make_block_response(uid_a, block, response, sizeof(response));
    feed_reader(&reader, response, length, 2U);

    rfid_reader_tick(&reader, 102U);
    length = make_uid_response(uid_b, RFID_STATUS_OK, response, sizeof(response));
    feed_reader(&reader, response, length, 103U);
    length = make_block_response(uid_b, block, response, sizeof(response));
    feed_reader(&reader, response, length, 104U);

    CHECK(fixture.event_count == 2U);
    CHECK(memcmp(fixture.events[0].uid, uid_a, RFID_UID_SIZE) == 0);
    CHECK(memcmp(fixture.events[1].uid, uid_b, RFID_UID_SIZE) == 0);
    CHECK(memcmp(fixture.events[0].text, fixture.events[1].text,
                 sizeof(text)) == 0);
}

static void test_reader_combines_blocks_and_skips_sector_trailer(void)
{
    static const uint8_t uid[RFID_UID_SIZE] = {0x21U, 0x22U, 0x23U, 0x24U};
    static const uint8_t first_block[RFID_BLOCK_SIZE] = {
        '1', '2', '3', '4', '5', '6', '7', '8',
        '9', 'A', 'B', 'C', 'D', 'E', 'F', 'G',
    };
    uint8_t continuation[RFID_BLOCK_SIZE] = {0xD1U, 0xD3U};
    uint8_t response[RFID_PROTOCOL_MAX_ENCODED_SIZE];
    reader_fixture_t fixture = {0};
    rfid_reader_t reader;
    rfid_reader_config_t config;

    rfid_reader_default_config(&config);
    rfid_reader_init(&reader, &config, capture_rfid_write,
                     capture_reader_event, &fixture);
    rfid_reader_tick(&reader, 0U);

    size_t length = make_uid_response(uid, RFID_STATUS_OK, response,
                                      sizeof(response));
    feed_reader(&reader, response, length, 1U);
    length = make_block_response(uid, first_block, response, sizeof(response));
    feed_reader(&reader, response, length, 2U);
    CHECK(fixture.write_count == 3U);
    CHECK(fixture.writes[2][4] == 5U);

    length = make_block_response(uid, continuation, response, sizeof(response));
    feed_reader(&reader, response, length, 3U);
    CHECK(fixture.event_count == 1U);
    CHECK(fixture.events[0].type == RFID_READER_EVENT_TAG);
    CHECK(fixture.events[0].text_length == RFID_BLOCK_SIZE + 2U);
    CHECK(memcmp(fixture.events[0].text, first_block, RFID_BLOCK_SIZE) == 0);
    CHECK(memcmp(&fixture.events[0].text[RFID_BLOCK_SIZE], continuation, 2U) ==
          0);

    rfid_reader_default_config(&config);
    CHECK(config.data_blocks[0] == 4U);
    CHECK(config.data_blocks[1] == 5U);
    CHECK(config.data_blocks[2] == 6U);
    CHECK(config.data_blocks[3] == 8U);
}

static void test_reader_rejects_invalid_gb2312(void)
{
    static const uint8_t uid[RFID_UID_SIZE] = {9U, 8U, 7U, 6U};
    uint8_t invalid_block[RFID_BLOCK_SIZE] = {0xD1U, 0x20U, 0x00U};
    uint8_t response[RFID_PROTOCOL_MAX_ENCODED_SIZE];
    reader_fixture_t fixture = {0};
    rfid_reader_t reader;
    rfid_reader_config_t config;

    rfid_reader_default_config(&config);
    rfid_reader_init(&reader, &config, capture_rfid_write,
                     capture_reader_event, &fixture);
    rfid_reader_tick(&reader, 0U);
    size_t length = make_uid_response(uid, RFID_STATUS_OK, response,
                                      sizeof(response));
    feed_reader(&reader, response, length, 1U);
    length = make_block_response(uid, invalid_block, response, sizeof(response));
    feed_reader(&reader, response, length, 2U);

    CHECK(fixture.event_count == 1U);
    CHECK(fixture.events[0].type == RFID_READER_EVENT_INVALID_TEXT);
}

static void test_reader_rejects_oversized_known_responses(void)
{
    static const uint8_t uid[RFID_UID_SIZE] = {7U, 6U, 5U, 4U};
    uint8_t uid_payload[2U + RFID_UID_SIZE + 1U] = {0x04U, 0x00U};
    uint8_t block_payload[2U + RFID_UID_SIZE + RFID_BLOCK_SIZE + 1U] = {
        0x04U, 0x00U,
    };
    uint8_t response[RFID_PROTOCOL_MAX_ENCODED_SIZE];
    reader_fixture_t fixture = {0};
    rfid_reader_t reader;
    rfid_reader_config_t config;

    memcpy(&uid_payload[2], uid, RFID_UID_SIZE);
    rfid_reader_default_config(&config);
    rfid_reader_init(&reader, &config, capture_rfid_write,
                     capture_reader_event, &fixture);
    rfid_reader_tick(&reader, 0U);
    size_t length = make_response(RFID_RESPONSE_UID, RFID_STATUS_OK, uid_payload,
                                  sizeof(uid_payload), response,
                                  sizeof(response));
    feed_reader(&reader, response, length, 1U);
    CHECK(fixture.event_count == 1U);
    CHECK(fixture.events[0].type == RFID_READER_EVENT_PROTOCOL_ERROR);
    CHECK(reader.state == RFID_READER_IDLE);

    fixture.event_count = 0U;
    rfid_reader_tick(&reader, config.poll_interval_ms + 1U);
    length = make_uid_response(uid, RFID_STATUS_OK, response, sizeof(response));
    feed_reader(&reader, response, length, config.poll_interval_ms + 2U);
    memcpy(&block_payload[2], uid, RFID_UID_SIZE);
    length = make_response(RFID_RESPONSE_BLOCK, RFID_STATUS_OK, block_payload,
                           sizeof(block_payload), response, sizeof(response));
    feed_reader(&reader, response, length, config.poll_interval_ms + 3U);
    CHECK(fixture.event_count == 1U);
    CHECK(fixture.events[0].type == RFID_READER_EVENT_PROTOCOL_ERROR);
    CHECK(reader.state == RFID_READER_WAIT_BLOCK);
}

static void test_reader_retries_tag_dispatch_when_consumer_is_full(void)
{
    static const uint8_t uid[RFID_UID_SIZE] = {3U, 1U, 4U, 1U};
    uint8_t block[RFID_BLOCK_SIZE] = {'O', 'K', 0U};
    uint8_t response[RFID_PROTOCOL_MAX_ENCODED_SIZE];
    reader_fixture_t fixture = {.rejected_tag_callbacks = 1U};
    rfid_reader_t reader;
    rfid_reader_config_t config;

    rfid_reader_default_config(&config);
    config.dispatch_retry_ms = 10U;
    rfid_reader_init(&reader, &config, capture_rfid_write,
                     capture_reader_event, &fixture);
    rfid_reader_tick(&reader, 0U);
    size_t length = make_uid_response(uid, RFID_STATUS_OK, response,
                                      sizeof(response));
    feed_reader(&reader, response, length, 1U);
    length = make_block_response(uid, block, response, sizeof(response));
    feed_reader(&reader, response, length, 2U);

    CHECK(fixture.event_count == 1U);
    CHECK(reader.state == RFID_READER_WAIT_DISPATCH);
    CHECK(reader.last_uid_valid == 0U);

    rfid_reader_tick(&reader, 12U);
    CHECK(fixture.event_count == 2U);
    CHECK(reader.state == RFID_READER_WAIT_REMOVAL);
    CHECK(reader.last_uid_valid == 1U);
}

static void test_tts_uses_explicit_lengths_and_startup_speed_command(void)
{
    static const uint8_t text[] = {0xD1U, 0xD3U, 0x00U, 0xB0U, 0xB2U};
    static const uint8_t speed_command[] = {'<', 'S', '>', '3'};
    tts_fixture_t fixture = {0};
    tts_service_t service;

    tts_service_init(&service, capture_tts_write, &fixture, true, 0U);
    CHECK(tts_service_enqueue(&service, text, sizeof(text)));
    tts_service_tick(&service, 499U);
    CHECK(fixture.write_count == 0U);
    tts_service_tick(&service, 500U);
    CHECK(fixture.write_count == 1U);
    CHECK(fixture.write_lengths[0] == sizeof(speed_command));
    CHECK(memcmp(fixture.writes[0], speed_command, sizeof(speed_command)) == 0);

    tts_service_tick(&service, 800U);
    CHECK(fixture.write_count == 2U);
    CHECK(fixture.write_lengths[1] == sizeof(text));
    CHECK(memcmp(fixture.writes[1], text, sizeof(text)) == 0);
}

static void test_tts_queue_capacity_and_ascii_duration(void)
{
    static const uint8_t short_text[] = {'A'};
    static const uint8_t ascii_text[] = "ABCDEFGHIJ";
    static const uint8_t mixed_text[] = {'A', 0xD1U, 0xD3U, 'B'};
    uint8_t longest_ascii[TTS_TEXT_CAPACITY];
    tts_fixture_t fixture = {0};
    tts_service_t service;

    tts_service_init(&service, capture_tts_write, &fixture, false, 0U);
    for (size_t i = 0U; i < TTS_QUEUE_DEPTH; ++i) {
        CHECK(tts_service_enqueue(&service, short_text, sizeof(short_text)));
    }
    CHECK(!tts_service_enqueue(&service, short_text, sizeof(short_text)));

    tts_service_init(&service, capture_tts_write, &fixture, false, 0U);
    CHECK(tts_service_enqueue(&service, ascii_text, sizeof(ascii_text) - 1U));
    tts_service_tick(&service, 0U);
    CHECK(service.busy_until_ms == 2100U);

    memset(longest_ascii, 'A', sizeof(longest_ascii));
    tts_service_init(&service, capture_tts_write, &fixture, false, 0U);
    CHECK(tts_service_enqueue(&service, longest_ascii, sizeof(longest_ascii)));
    tts_service_tick(&service, 0U);
    CHECK(service.busy_until_ms == 11820U);

    tts_service_init(&service, capture_tts_write, &fixture, false, 0U);
    CHECK(tts_service_enqueue(&service, mixed_text, sizeof(mixed_text)));
    tts_service_tick(&service, 0U);
    CHECK(service.busy_until_ms == 840U);
}

int main(void)
{
    test_known_request_vectors();
    test_parser_checksum_escape_and_recovery();
    test_reader_reads_text_once_until_card_removal();
    test_different_uid_with_same_text_is_not_suppressed();
    test_reader_combines_blocks_and_skips_sector_trailer();
    test_reader_rejects_invalid_gb2312();
    test_reader_rejects_oversized_known_responses();
    test_reader_retries_tag_dispatch_when_consumer_is_full();
    test_tts_uses_explicit_lengths_and_startup_speed_command();
    test_tts_queue_capacity_and_ascii_duration();

    if (g_failures != 0U) {
        fprintf(stderr, "%u test assertion(s) failed\n", g_failures);
        return 1;
    }
    puts("All native firmware tests passed.");
    return 0;
}
