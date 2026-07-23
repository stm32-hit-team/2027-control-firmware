#include "rfid_protocol.h"

#include <stdbool.h>
#include <string.h>

static void parser_reset(rfid_protocol_parser_t *parser)
{
    parser->raw_count = 0U;
    parser->expected_raw_count = 0U;
    parser->in_frame = 0U;
    parser->escape_pending = 0U;
}

static void parser_start(rfid_protocol_parser_t *parser)
{
    parser->raw_count = 0U;
    parser->expected_raw_count = 0U;
    parser->in_frame = 1U;
    parser->escape_pending = 0U;
}

static rfid_parse_result_t parser_accept_decoded(rfid_protocol_parser_t *parser,
                                                  uint8_t byte,
                                                  rfid_frame_t *frame)
{
    if (parser->raw_count == 0U) {
        const size_t declared_length = byte;
        if (declared_length < 3U ||
            declared_length > RFID_PROTOCOL_MAX_BODY_SIZE + 1U) {
            parser_reset(parser);
            return RFID_PARSE_ERROR;
        }
        parser->expected_raw_count = declared_length + 1U;
    }

    if (parser->raw_count >= sizeof(parser->raw)) {
        parser_reset(parser);
        return RFID_PARSE_ERROR;
    }
    parser->raw[parser->raw_count++] = byte;

    if (parser->raw_count < parser->expected_raw_count) {
        return RFID_PARSE_NONE;
    }
    if (parser->raw_count != parser->expected_raw_count) {
        parser_reset(parser);
        return RFID_PARSE_ERROR;
    }

    const size_t declared_length = parser->raw[0];
    uint8_t checksum = 0U;
    for (size_t i = 0U; i < declared_length; ++i) {
        checksum ^= parser->raw[i];
    }
    if (checksum != parser->raw[declared_length]) {
        parser_reset(parser);
        return RFID_PARSE_ERROR;
    }

    const size_t body_length = declared_length - 1U;
    if (body_length < 2U) {
        parser_reset(parser);
        return RFID_PARSE_ERROR;
    }

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

    parser_reset(parser);
    return RFID_PARSE_FRAME;
}

void rfid_protocol_parser_init(rfid_protocol_parser_t *parser)
{
    memset(parser, 0, sizeof(*parser));
}

rfid_parse_result_t rfid_protocol_parser_feed(rfid_protocol_parser_t *parser,
                                               uint8_t byte,
                                               rfid_frame_t *frame)
{
    if (parser == NULL || frame == NULL) {
        return RFID_PARSE_ERROR;
    }

    if (parser->in_frame == 0U) {
        if (byte == RFID_PROTOCOL_HEADER) {
            parser_start(parser);
        }
        return RFID_PARSE_NONE;
    }

    if (parser->escape_pending != 0U) {
        parser->escape_pending = 0U;
        if (byte == RFID_PROTOCOL_HEADER) {
            return parser_accept_decoded(parser, RFID_PROTOCOL_HEADER, frame);
        }

        parser_start(parser);
        return parser_accept_decoded(parser, byte, frame);
    }

    if (byte == RFID_PROTOCOL_HEADER) {
        parser->escape_pending = 1U;
        return RFID_PARSE_NONE;
    }

    return parser_accept_decoded(parser, byte, frame);
}

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

size_t rfid_protocol_encode_body(const uint8_t *body, size_t body_length,
                                 uint8_t *encoded, size_t capacity)
{
    if (body == NULL || encoded == NULL || body_length < 2U ||
        body_length > RFID_PROTOCOL_MAX_BODY_SIZE || capacity == 0U) {
        return 0U;
    }

    const size_t declared_length = body_length + 1U;
    if (declared_length > UINT8_MAX) {
        return 0U;
    }

    size_t position = 0U;
    encoded[position++] = RFID_PROTOCOL_HEADER;

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

size_t rfid_protocol_encode_read_uid(uint8_t *encoded, size_t capacity)
{
    static const uint8_t body[] = {0x00U, RFID_REQUEST_UID};
    return rfid_protocol_encode_body(body, sizeof(body), encoded, capacity);
}

size_t rfid_protocol_encode_read_block(uint8_t block, uint8_t *encoded,
                                       size_t capacity)
{
    const uint8_t body[] = {0x00U, RFID_REQUEST_BLOCK, block};
    return rfid_protocol_encode_body(body, sizeof(body), encoded, capacity);
}
