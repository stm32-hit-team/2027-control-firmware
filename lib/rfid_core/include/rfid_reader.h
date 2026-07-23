#ifndef RFID_READER_H
#define RFID_READER_H

#include "rfid_protocol.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RFID_UID_SIZE 4U
#define RFID_BLOCK_SIZE 16U
#define RFID_TEXT_CAPACITY 64U
#define RFID_READER_MAX_BLOCKS 4U

typedef enum {
    RFID_READER_EVENT_TAG = 0,
    RFID_READER_EVENT_PROTOCOL_ERROR,
    RFID_READER_EVENT_READ_TIMEOUT,
    RFID_READER_EVENT_INVALID_TEXT,
} rfid_reader_event_type_t;

typedef struct {
    rfid_reader_event_type_t type;
    uint8_t uid[RFID_UID_SIZE];
    uint8_t text[RFID_TEXT_CAPACITY];
    size_t text_length;
} rfid_reader_event_t;

typedef bool (*rfid_reader_write_fn)(void *context, const uint8_t *data,
                                     size_t length);
typedef bool (*rfid_reader_event_fn)(void *context,
                                     const rfid_reader_event_t *event);

typedef struct {
    uint32_t poll_interval_ms;
    uint32_t response_timeout_ms;
    uint32_t removal_poll_interval_ms;
    uint32_t dispatch_retry_ms;
    uint8_t retry_limit;
    uint8_t removal_confirmations;
    uint8_t data_blocks[RFID_READER_MAX_BLOCKS];
    size_t data_block_count;
} rfid_reader_config_t;

typedef enum {
    RFID_READER_IDLE = 0,
    RFID_READER_WAIT_UID,
    RFID_READER_WAIT_BLOCK,
    RFID_READER_WAIT_DISPATCH,
    RFID_READER_WAIT_REMOVAL,
} rfid_reader_state_t;

typedef enum {
    RFID_UID_DISCOVERY = 0,
    RFID_UID_REMOVAL_CHECK,
} rfid_uid_purpose_t;

typedef struct {
    rfid_reader_config_t config;
    rfid_protocol_parser_t parser;
    rfid_reader_write_fn write;
    rfid_reader_event_fn event;
    void *context;
    rfid_reader_state_t state;
    rfid_uid_purpose_t uid_purpose;
    uint32_t deadline_ms;
    uint32_t next_action_ms;
    uint8_t current_uid[RFID_UID_SIZE];
    uint8_t last_uid[RFID_UID_SIZE];
    uint8_t text[RFID_TEXT_CAPACITY];
    size_t text_length;
    size_t block_index;
    uint8_t retry_count;
    uint8_t absence_count;
    uint8_t last_uid_valid;
} rfid_reader_t;

void rfid_reader_default_config(rfid_reader_config_t *config);
void rfid_reader_init(rfid_reader_t *reader,
                      const rfid_reader_config_t *config,
                      rfid_reader_write_fn write,
                      rfid_reader_event_fn event,
                      void *context);
void rfid_reader_tick(rfid_reader_t *reader, uint32_t now_ms);
void rfid_reader_feed(rfid_reader_t *reader, uint8_t byte, uint32_t now_ms);

#endif
