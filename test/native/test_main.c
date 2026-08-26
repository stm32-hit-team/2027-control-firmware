/*
 * test_main.c —— 在电脑上跑的单元测试。
 *
 * 为什么能在电脑上跑：
 *   rfid_core 这个库完全不碰硬件，发数据靠回调，时间靠参数传进来。
 *   所以用一个假的发送函数和手动指定的时间戳，就能把整套逻辑跑通。
 *
 * 这样做的好处很实在：
 *   不用插板子就能验证协议和状态机，改一次代码几秒钟就知道有没有改坏。
 *   还能开地址消毒和未定义行为检查，把越界和溢出直接抓出来。
 *
 * 跑法：sh scripts/run_host_tests.sh
 */
#include "rfid_protocol.h"
#include "rfid_reader.h"
#include "tts_service.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* 求数组元素个数。 */
#define ARRAY_LEN(array) (sizeof(array) / sizeof((array)[0]))

/* 累计失败次数。测试全跑完再一次性报告。 */
static unsigned g_failures;

/*
 * 断言宏。
 *
 * 条件不成立时打印文件名、行号和条件本身，然后计数加一。
 * 注意这里不中断执行。一次跑完能看到所有问题，比只看到第一个更省时间。
 */
#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,           \
                    #condition);                                                \
            g_failures++;                                                       \
        }                                                                       \
    } while (0)

/*
 * 读卡器测试用的桩数据。
 *
 * writes / write_lengths  记录状态机发出去的每一条命令，供后面比对。
 * events                  记录状态机汇报的每一个事件。
 * rejected_tag_callbacks  故意拒绝几次 TAG 事件，用来测反压机制。
 */
typedef struct {
    uint8_t writes[32][RFID_PROTOCOL_MAX_ENCODED_SIZE];
    size_t write_lengths[32];
    size_t write_count;
    rfid_reader_event_t events[16];
    size_t event_count;
    size_t rejected_tag_callbacks;
} reader_fixture_t;

/* 语音服务测试用的桩数据。只需要记录发出去的内容。 */
typedef struct {
    uint8_t writes[8][TTS_TEXT_CAPACITY];
    size_t write_lengths[8];
    size_t write_count;
} tts_fixture_t;

/* 假的 RFID 发送函数。不真发，只把内容抄下来。 */
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

/*
 * 假的事件回调。
 *
 * 除了记录事件，还会按 rejected_tag_callbacks 的值故意拒绝几次 TAG。
 * 这是为了模拟“语音队列满了”的场景，验证状态机会不会保住这张卡重试。
 */
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

/* 假的语音发送函数。同样只记录不真发。 */
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

/*
 * 造一个假的回复帧。
 *
 * 这里直接调用产品代码里的编码函数来造帧。
 * 好处是不用手写校验和，也不用手动处理转义。
 */
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

/* 造一个 UID 回复。前两个字节 04 00 是卡类型，和真实模块一致。 */
static size_t make_uid_response(const uint8_t uid[RFID_UID_SIZE], uint8_t status,
                                uint8_t *encoded, size_t capacity)
{
    uint8_t payload[2U + RFID_UID_SIZE] = {0x04U, 0x00U};

    memcpy(&payload[2], uid, RFID_UID_SIZE);
    return make_response(RFID_RESPONSE_UID, status, payload, sizeof(payload),
                         encoded, capacity);
}

/* 造一个数据块回复。内容是卡类型、UID、16 字节块数据。 */
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

/* 把一帧数据逐字节喂给状态机，模拟串口一个个收字节。 */
static void feed_reader(rfid_reader_t *reader, const uint8_t *frame,
                        size_t length, uint32_t now_ms)
{
    for (size_t i = 0; i < length; ++i) {
        rfid_reader_feed(reader, frame[i], now_ms);
    }
}

/*
 * 测试一：编码结果和已知的正确字节序列完全一致。
 *
 * 这两个字节序列是从去年能正常工作的固件里抄出来的。
 * 把它们写死在测试里，等于给协议编码上了一道锁。
 * 以后谁不小心改了校验算法或者转义规则，这个测试立刻失败。
 */
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

/*
 * 测试二：校验和、转义、以及出错之后能不能恢复。
 *
 * 分三段验证：
 *   1. UID 里故意放一个 0x7F，看编码有没有转义、解码能不能还原。
 *   2. 把校验字节改坏一位，解析器必须报错。
 *   3. 紧接着喂一个正常帧，解析器必须能重新对齐并解析成功。
 * 第 3 段最重要。现场串口一定会有干扰，解析器坏一帧就再也不工作是不行的。
 */
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

/*
 * 测试三：同一张卡只播报一次，拿走再放回才播报第二次。
 *
 * 这是整个固件最核心的行为，所以测试也写得最细。时间线是：
 *   0 毫秒    发出第一条找卡命令
 *   1 毫秒    回一个 UID，状态机开始读块
 *   2 毫秒    回块数据，上报第一次 TAG 事件
 *   52 到 104 毫秒  卡一直在，反复确认，事件数必须还是 1
 *   154 到 256 毫秒 连回三次“无卡”，凑够确认次数
 *   307 毫秒起 重新发现同一张卡，这次应该上报第二次 TAG
 *
 * 最后那个断言是关键：事件数变成 2，说明拿走再放回确实会重新播报。
 */
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

/*
 * 测试四：两张不同的卡写着一样的文字，必须各播报一次。
 *
 * 这个测试是为了钉住一个设计决定：判重看的是 UID，不是文字内容。
 * 如果哪天有人改成按文字判重，第二张卡就会被吞掉，这个测试会失败。
 */
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

/*
 * 测试五：多个块的内容能正确拼接，而且跳过了扇区尾块。
 *
 * 第一个块塞满 16 个字节不留结束标记，逼状态机去读第二个块。
 * 然后检查两个块的内容是不是首尾相接拼在一起。
 *
 * 最后四个断言检查默认块号是 4、5、6、8。
 * 7 号是 M1 卡第二扇区的尾块，存的是密钥，必须跳过。
 */
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

/*
 * 测试六：非法编码的文字会被拦下来。
 *
 * 块里放的是 0xD1 后面跟 0x20。0xD1 是汉字首字节，
 * 但 0x20 不在合法的次字节范围内，所以这是一段坏编码。
 * 期望结果是报 INVALID_TEXT，而不是把乱码发给语音模块。
 */
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

/*
 * 测试七：长度多一个字节的回复会被拒绝。
 *
 * 两种回复各测一次，都是故意在数据段末尾多加一个字节。
 * 期望结果是报协议错误，并且状态不乱跑。
 *
 * 为什么要卡这么严：长度校验松了，串口噪声凑出的巧合帧就可能被当真数据，
 * 那时候读出来的内容是错的，还很难查。
 */
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

/*
 * 测试八：上层忙的时候，卡不会被漏掉。
 *
 * 把 rejected_tag_callbacks 设成 1，第一次 TAG 事件会被故意拒绝。
 * 期望的行为是：
 *   状态机进 WAIT_DISPATCH，并且 last_uid_valid 保持为 0。
 *   过了重试间隔再送一次，这次上层收下，last_uid_valid 才变成 1。
 *
 * 中间那个 last_uid_valid == 0 的断言最要紧。
 * 它保证了没播报成功的卡不会被误记成“已处理”。
 */
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

/*
 * 测试九：发送用显式长度，以及开机的语速命令。
 *
 * 文字里故意放了一个 0x00 在中间。
 * 如果代码用字符串长度去算，那个 0x00 会把文字截成两半。
 * 这里断言发出去的长度等于完整的 5 字节，就把这个坑钉住了。
 *
 * 另外验证开机延时：499 毫秒时什么都不发，500 毫秒才发语速命令。
 */
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

/*
 * 测试十：队列容量和时长估算。
 *
 * 先塞满 8 条，第 9 条必须被拒绝，验证队列不会溢出。
 *
 * 然后验证三种文字的时长估算，算法是 300 加上字数乘 180：
 *   10 个 ASCII 字符   300 + 10*180 = 2100
 *   64 个 ASCII 字符   300 + 64*180 = 11820
 *   1 汉字加 2 ASCII   300 + 3*180  = 840，说明汉字被算成一个字而不是两个
 * 最后那条最能说明问题。字数数错，播报就会被截断。
 */
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

/*
 * 入口。把十个测试依次跑完，最后统一报结果。
 *
 * 返回 0 表示全过，返回 1 表示有失败。
 * 脚本里开了 set -e，返回非 0 会让整个脚本失败，方便接到自动化流程里。
 */
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
