/*
 * rfid_protocol.c —— 协议编解码的实现。
 *
 * 解析部分是一个逐字节的状态机。
 * 好处是不需要缓存整包再处理，收一个字节就能判断一次。
 * 而且帧长不对、校验错的时候能立刻发现并复位，不会卡死。
 */
#include "rfid_protocol.h"

#include <stdbool.h>
#include <string.h>

/* 彻底复位，回到“等帧头”的状态。 */
static void parser_reset(rfid_protocol_parser_t *parser)
{
    parser->raw_count = 0U;
    parser->expected_raw_count = 0U;
    parser->in_frame = 0U;
    parser->escape_pending = 0U;
}

/* 收到帧头，开始收一个新帧。清计数但保持 in_frame 为 1。 */
static void parser_start(rfid_protocol_parser_t *parser)
{
    parser->raw_count = 0U;
    parser->expected_raw_count = 0U;
    parser->in_frame = 1U;
    parser->escape_pending = 0U;
}

/*
 * 收下一个已经去掉转义的字节，判断帧是否收完。
 *
 * 处理顺序：
 *   1. 第一个字节是长度字段，先检查合不合理，再算出总共该收几个字节。
 *   2. 中间的字节直接存起来。
 *   3. 收够了就算校验和，对得上才拆出各个字段。
 *
 * 任何一步不对就复位并返回错误。这样一帧坏了不会影响下一帧。
 */
static rfid_parse_result_t parser_accept_decoded(rfid_protocol_parser_t *parser,
                                                  uint8_t byte,
                                                  rfid_frame_t *frame)
{
    if (parser->raw_count == 0U) {
        /*
         * 第一个字节是长度。
         * 小于 3 说明连地址和命令都放不下，一定是错的。
         * 大于上限说明超出了缓冲区，也不能收。
         * 先检查再分配，就不会有越界写的机会。
         */
        const size_t declared_length = byte;
        if (declared_length < 3U ||
            declared_length > RFID_PROTOCOL_MAX_BODY_SIZE + 1U) {
            parser_reset(parser);
            return RFID_PARSE_ERROR;
        }
        /* 加 1 是因为长度字段本身也占一个位置。 */
        parser->expected_raw_count = declared_length + 1U;
    }

    /* 再兜一层底。理论上上面已经拦住了，但多一道检查不吃亏。 */
    if (parser->raw_count >= sizeof(parser->raw)) {
        parser_reset(parser);
        return RFID_PARSE_ERROR;
    }
    parser->raw[parser->raw_count++] = byte;

    /* 还没收够，继续等下一个字节。 */
    if (parser->raw_count < parser->expected_raw_count) {
        return RFID_PARSE_NONE;
    }
    if (parser->raw_count != parser->expected_raw_count) {
        parser_reset(parser);
        return RFID_PARSE_ERROR;
    }

    /*
     * 收够了，验校验和。
     * 算法是长度字段和帧体逐字节异或，结果应该等于最后那个字节。
     * 异或校验能查出单个位翻转，对串口传输来说够用。
     */
    const size_t declared_length = parser->raw[0];
    uint8_t checksum = 0U;
    for (size_t i = 0U; i < declared_length; ++i) {
        checksum ^= parser->raw[i];
    }
    if (checksum != parser->raw[declared_length]) {
        parser_reset(parser);
        return RFID_PARSE_ERROR;
    }

    /* 帧体长度是声明长度减去校验字节那一位。 */
    const size_t body_length = declared_length - 1U;
    if (body_length < 2U) {
        parser_reset(parser);
        return RFID_PARSE_ERROR;
    }

    /*
     * 拆字段。raw[0] 是长度，所以内容从 raw[1] 开始。
     * 状态和数据段都可能不存在，所以用三目运算符做兼容。
     */
    frame->address = parser->raw[1];
    frame->command = parser->raw[2];
    frame->status = body_length >= 3U ? parser->raw[3] : 0U;
    frame->payload_length = body_length > 3U ? body_length - 3U : 0U;
    if (frame->payload_length > sizeof(frame->payload)) {
        parser_reset(parser);
        return RFID_PARSE_ERROR;
    }
    if (frame->payload_length > 0U) {
        memcpy(frame->payload, &parser->raw[4], frame->payload_length);
    }

    /* 一帧交付完就复位，准备收下一帧。 */
    parser_reset(parser);
    return RFID_PARSE_FRAME;
}

void rfid_protocol_parser_init(rfid_protocol_parser_t *parser)
{
    memset(parser, 0, sizeof(*parser));
}

/*
 * 解析器的入口。每收到一个字节调一次。
 *
 * 核心难点是 0x7F 有两个身份：帧头，以及转义标记。
 * 判断办法是看它后面跟的是什么：
 *   0x7F 后面又是 0x7F  这是转义，代表数据里的一个 0x7F
 *   0x7F 后面是别的字节 这是帧头，说明新帧从这里开始
 *
 * 所以收到 0x7F 时不能马上下结论，要先记下 escape_pending，等下一个字节。
 * 这个设计顺带带来一个好处：数据流中间被截断时，
 * 下一个帧头会自动让解析器重新对齐，不需要额外的复位逻辑。
 */
rfid_parse_result_t rfid_protocol_parser_feed(rfid_protocol_parser_t *parser,
                                               uint8_t byte,
                                               rfid_frame_t *frame)
{
    if (parser == NULL || frame == NULL) {
        return RFID_PARSE_ERROR;
    }

    /* 还没进帧，只认帧头，别的字节全部丢掉。 */
    if (parser->in_frame == 0U) {
        if (byte == RFID_PROTOCOL_HEADER) {
            parser_start(parser);
        }
        return RFID_PARSE_NONE;
    }

    /* 上一个字节是 0x7F，现在能判断它到底是什么身份了。 */
    if (parser->escape_pending != 0U) {
        parser->escape_pending = 0U;
        if (byte == RFID_PROTOCOL_HEADER) {
            /* 两个 0x7F 连着，是转义。还原成一个数据字节 0x7F。 */
            return parser_accept_decoded(parser, RFID_PROTOCOL_HEADER, frame);
        }

        /*
         * 后面跟的不是 0x7F，说明刚才那个是新帧的帧头。
         * 于是丢掉手上这个没收完的帧，从当前字节开始收新帧。
         */
        parser_start(parser);
        return parser_accept_decoded(parser, byte, frame);
    }

    /* 遇到 0x7F，先挂起，等下一个字节再定性。 */
    if (byte == RFID_PROTOCOL_HEADER) {
        parser->escape_pending = 1U;
        return RFID_PARSE_NONE;
    }

    /* 普通数据字节，直接收下。 */
    return parser_accept_decoded(parser, byte, frame);
}

/*
 * 写一个字节到输出缓冲区，需要转义就写两遍。
 *
 * 空间检查写成 required > capacity - *position 而不是
 * *position + required > capacity，是为了避开加法溢出。
 * 返回 false 表示空间不够，调用方应该整体放弃这次编码。
 */
static bool emit_stuffed(uint8_t byte, uint8_t *encoded, size_t capacity,
                         size_t *position)
{
    const size_t required = byte == RFID_PROTOCOL_HEADER ? 2U : 1U;
    if (*position > capacity || required > capacity - *position) {
        return false;
    }

    encoded[(*position)++] = byte;
    if (byte == RFID_PROTOCOL_HEADER) {
        encoded[(*position)++] = byte;
    }
    return true;
}

/*
 * 把帧体编码成完整的一帧。
 *
 * 顺序是：写帧头，写长度，写帧体，写校验。
 * 除了帧头，其余部分都要过转义。帧头必须保持单个 0x7F，否则接收方认不出。
 * 校验和在写帧体的同时顺便算出来，省一次遍历。
 */
size_t rfid_protocol_encode_body(const uint8_t *body, size_t body_length,
                                 uint8_t *encoded, size_t capacity)
{
    if (body == NULL || encoded == NULL || body_length < 2U ||
        body_length > RFID_PROTOCOL_MAX_BODY_SIZE || capacity == 0U) {
        return 0U;
    }

    /* 长度字段要把校验字节也算进去。 */
    const size_t declared_length = body_length + 1U;
    if (declared_length > UINT8_MAX) {
        return 0U;
    }

    size_t position = 0U;
    encoded[position++] = RFID_PROTOCOL_HEADER;

    /* 长度字段本身也参与校验计算。 */
    uint8_t checksum = (uint8_t)declared_length;
    if (!emit_stuffed((uint8_t)declared_length, encoded, capacity, &position)) {
        return 0U;
    }
    for (size_t i = 0U; i < body_length; ++i) {
        checksum ^= body[i];
        if (!emit_stuffed(body[i], encoded, capacity, &position)) {
            return 0U;
        }
    }
    if (!emit_stuffed(checksum, encoded, capacity, &position)) {
        return 0U;
    }
    return position;
}

/*
 * 拼“读 UID”命令。
 * 帧体是地址 0x00 加命令 0x10，编码结果固定为 7F 03 00 10 13。
 * 这个字节序列在单元测试里被写死做比对，改了会有测试失败提醒。
 */
size_t rfid_protocol_encode_read_uid(uint8_t *encoded, size_t capacity)
{
    static const uint8_t body[] = {0x00U, RFID_REQUEST_UID};
    return rfid_protocol_encode_body(body, sizeof(body), encoded, capacity);
}

/* 拼“读指定块”命令。帧体是地址、命令、块号三个字节。 */
size_t rfid_protocol_encode_read_block(uint8_t block, uint8_t *encoded,
                                       size_t capacity)
{
    const uint8_t body[] = {0x00U, RFID_REQUEST_BLOCK, block};
    return rfid_protocol_encode_body(body, sizeof(body), encoded, capacity);
}
