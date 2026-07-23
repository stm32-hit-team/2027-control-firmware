#ifndef RFID_PROTOCOL_H
#define RFID_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define RFID_PROTOCOL_HEADER 0x7FU
#define RFID_PROTOCOL_MAX_BODY_SIZE 67U
#define RFID_PROTOCOL_MAX_PAYLOAD_SIZE 64U
#define RFID_PROTOCOL_MAX_RAW_SIZE (RFID_PROTOCOL_MAX_BODY_SIZE + 2U)
#define RFID_PROTOCOL_MAX_ENCODED_SIZE (1U + 2U * RFID_PROTOCOL_MAX_RAW_SIZE)

#define RFID_REQUEST_UID 0x10U
#define RFID_REQUEST_BLOCK 0x11U
#define RFID_RESPONSE_UID 0x90U
#define RFID_RESPONSE_BLOCK 0x91U
#define RFID_STATUS_OK 0x00U
#define RFID_STATUS_NO_CARD 0xFFU

typedef enum {
    RFID_PARSE_NONE = 0,
    RFID_PARSE_FRAME,
    RFID_PARSE_ERROR,
} rfid_parse_result_t;

typedef struct {
    uint8_t address;
    uint8_t command;
    uint8_t status;
    uint8_t payload[RFID_PROTOCOL_MAX_PAYLOAD_SIZE];
    size_t payload_length;
} rfid_frame_t;

typedef struct {
    uint8_t raw[RFID_PROTOCOL_MAX_RAW_SIZE];
    size_t raw_count;
    size_t expected_raw_count;
    uint8_t in_frame;
    uint8_t escape_pending;
} rfid_protocol_parser_t;

void rfid_protocol_parser_init(rfid_protocol_parser_t *parser);
rfid_parse_result_t rfid_protocol_parser_feed(rfid_protocol_parser_t *parser,
                                               uint8_t byte,
                                               rfid_frame_t *frame);

size_t rfid_protocol_encode_body(const uint8_t *body, size_t body_length,
                                 uint8_t *encoded, size_t capacity);
size_t rfid_protocol_encode_read_uid(uint8_t *encoded, size_t capacity);
size_t rfid_protocol_encode_read_block(uint8_t block, uint8_t *encoded,
                                       size_t capacity);

#endif
