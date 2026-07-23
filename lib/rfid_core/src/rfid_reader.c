#include "rfid_reader.h"

#include <string.h>

static bool time_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

static bool emit_event(rfid_reader_t *reader, rfid_reader_event_type_t type)
{
    if (reader->event == NULL) {
        return true;
    }

    rfid_reader_event_t event = {0};
    event.type = type;
    memcpy(event.uid, reader->current_uid, sizeof(event.uid));
    if (type == RFID_READER_EVENT_TAG) {
        event.text_length = reader->text_length;
        memcpy(event.text, reader->text, event.text_length);
    }
    return reader->event(reader->context, &event);
}

static bool send_encoded(rfid_reader_t *reader, const uint8_t *encoded,
                         size_t length)
{
    return length > 0U && reader->write != NULL &&
           reader->write(reader->context, encoded, length);
}

static void schedule_discovery(rfid_reader_t *reader, uint32_t now_ms)
{
    reader->state = RFID_READER_IDLE;
    reader->next_action_ms = now_ms + reader->config.poll_interval_ms;
}

static void enter_wait_removal(rfid_reader_t *reader, uint32_t now_ms)
{
    reader->state = RFID_READER_WAIT_REMOVAL;
    reader->absence_count = 0U;
    reader->next_action_ms = now_ms + reader->config.removal_poll_interval_ms;
}

static bool send_uid_request(rfid_reader_t *reader, uint32_t now_ms,
                             rfid_uid_purpose_t purpose)
{
    uint8_t encoded[RFID_PROTOCOL_MAX_ENCODED_SIZE];
    const size_t length = rfid_protocol_encode_read_uid(encoded, sizeof(encoded));

    if (!send_encoded(reader, encoded, length)) {
        return false;
    }
    reader->uid_purpose = purpose;
    reader->state = RFID_READER_WAIT_UID;
    reader->deadline_ms = now_ms + reader->config.response_timeout_ms;
    return true;
}

static bool send_current_block(rfid_reader_t *reader, uint32_t now_ms)
{
    uint8_t encoded[RFID_PROTOCOL_MAX_ENCODED_SIZE];
    const uint8_t block = reader->config.data_blocks[reader->block_index];
    const size_t length = rfid_protocol_encode_read_block(block, encoded,
                                                          sizeof(encoded));

    if (!send_encoded(reader, encoded, length)) {
        return false;
    }
    reader->state = RFID_READER_WAIT_BLOCK;
    reader->deadline_ms = now_ms + reader->config.response_timeout_ms;
    return true;
}

static void begin_read(rfid_reader_t *reader, const uint8_t uid[RFID_UID_SIZE],
                       uint32_t now_ms)
{
    memcpy(reader->current_uid, uid, RFID_UID_SIZE);
    memset(reader->text, 0, sizeof(reader->text));
    reader->text_length = 0U;
    reader->block_index = 0U;
    reader->retry_count = 0U;
    reader->absence_count = 0U;
    if (!send_current_block(reader, now_ms)) {
        schedule_discovery(reader, now_ms);
    }
}

static bool text_is_valid_gb2312(const uint8_t *text, size_t length)
{
    size_t index = 0U;
    while (index < length) {
        const uint8_t first = text[index];
        if (first >= 0x20U && first <= 0x7EU) {
            index++;
            continue;
        }
        if (first < 0xA1U || first > 0xF7U || index + 1U >= length) {
            return false;
        }
        const uint8_t second = text[index + 1U];
        if (second < 0xA1U || second > 0xFEU) {
            return false;
        }
        index += 2U;
    }
    return length > 0U;
}

static void finish_text(rfid_reader_t *reader, uint32_t now_ms)
{
    while (reader->text_length > 0U) {
        const uint8_t last = reader->text[reader->text_length - 1U];
        if (last != 0x20U && last != '\r' && last != '\n' && last != '\t') {
            break;
        }
        reader->text_length--;
    }

    if (!text_is_valid_gb2312(reader->text, reader->text_length)) {
        emit_event(reader, RFID_READER_EVENT_INVALID_TEXT);
        enter_wait_removal(reader, now_ms);
        return;
    }

    if (emit_event(reader, RFID_READER_EVENT_TAG)) {
        memcpy(reader->last_uid, reader->current_uid, RFID_UID_SIZE);
        reader->last_uid_valid = 1U;
        enter_wait_removal(reader, now_ms);
    } else {
        reader->state = RFID_READER_WAIT_DISPATCH;
        reader->next_action_ms = now_ms + reader->config.dispatch_retry_ms;
    }
}

static void retry_or_fail_block(rfid_reader_t *reader, uint32_t now_ms)
{
    if (reader->retry_count < reader->config.retry_limit) {
        reader->retry_count++;
        if (send_current_block(reader, now_ms)) {
            return;
        }
    }
    emit_event(reader, RFID_READER_EVENT_READ_TIMEOUT);
    enter_wait_removal(reader, now_ms);
}

static void handle_uid_frame(rfid_reader_t *reader, const rfid_frame_t *frame,
                             uint32_t now_ms)
{
    if (reader->state != RFID_READER_WAIT_UID) {
        return;
    }

    if (frame->status != RFID_STATUS_OK) {
        if (reader->uid_purpose == RFID_UID_REMOVAL_CHECK) {
            if (reader->absence_count < UINT8_MAX) {
                reader->absence_count++;
            }
            if (reader->absence_count >= reader->config.removal_confirmations) {
                reader->last_uid_valid = 0U;
                schedule_discovery(reader, now_ms);
            } else {
                reader->state = RFID_READER_WAIT_REMOVAL;
                reader->next_action_ms =
                    now_ms + reader->config.removal_poll_interval_ms;
            }
        } else {
            schedule_discovery(reader, now_ms);
        }
        return;
    }

    if (frame->payload_length != 2U + RFID_UID_SIZE) {
        emit_event(reader, RFID_READER_EVENT_PROTOCOL_ERROR);
        schedule_discovery(reader, now_ms);
        return;
    }

    const uint8_t *uid = &frame->payload[2];
    if (reader->uid_purpose == RFID_UID_REMOVAL_CHECK &&
        reader->last_uid_valid != 0U &&
        memcmp(uid, reader->last_uid, RFID_UID_SIZE) == 0) {
        reader->absence_count = 0U;
        reader->state = RFID_READER_WAIT_REMOVAL;
        reader->next_action_ms = now_ms + reader->config.removal_poll_interval_ms;
        return;
    }

    if (reader->uid_purpose == RFID_UID_DISCOVERY &&
        reader->last_uid_valid != 0U &&
        memcmp(uid, reader->last_uid, RFID_UID_SIZE) == 0) {
        enter_wait_removal(reader, now_ms);
        return;
    }

    begin_read(reader, uid, now_ms);
}

static void handle_block_frame(rfid_reader_t *reader, const rfid_frame_t *frame,
                               uint32_t now_ms)
{
    if (reader->state != RFID_READER_WAIT_BLOCK) {
        return;
    }
    if (frame->status != RFID_STATUS_OK) {
        retry_or_fail_block(reader, now_ms);
        return;
    }
    if (frame->payload_length != 2U + RFID_UID_SIZE + RFID_BLOCK_SIZE ||
        memcmp(&frame->payload[2], reader->current_uid, RFID_UID_SIZE) != 0) {
        emit_event(reader, RFID_READER_EVENT_PROTOCOL_ERROR);
        retry_or_fail_block(reader, now_ms);
        return;
    }

    const uint8_t *block = &frame->payload[2U + RFID_UID_SIZE];
    bool terminated = false;
    for (size_t i = 0U; i < RFID_BLOCK_SIZE; ++i) {
        if (block[i] == 0x00U || block[i] == 0xFFU) {
            terminated = true;
            break;
        }
        if (reader->text_length >= sizeof(reader->text)) {
            emit_event(reader, RFID_READER_EVENT_INVALID_TEXT);
            enter_wait_removal(reader, now_ms);
            return;
        }
        reader->text[reader->text_length++] = block[i];
    }

    if (terminated || reader->block_index + 1U >= reader->config.data_block_count) {
        finish_text(reader, now_ms);
        return;
    }

    reader->block_index++;
    reader->retry_count = 0U;
    if (!send_current_block(reader, now_ms)) {
        retry_or_fail_block(reader, now_ms);
    }
}

void rfid_reader_default_config(rfid_reader_config_t *config)
{
    memset(config, 0, sizeof(*config));
    config->poll_interval_ms = 80U;
    config->response_timeout_ms = 100U;
    config->removal_poll_interval_ms = 100U;
    config->dispatch_retry_ms = 10U;
    config->retry_limit = 2U;
    config->removal_confirmations = 3U;
    config->data_blocks[0] = 4U;
    config->data_blocks[1] = 5U;
    config->data_blocks[2] = 6U;
    config->data_blocks[3] = 8U;
    config->data_block_count = RFID_READER_MAX_BLOCKS;
}

void rfid_reader_init(rfid_reader_t *reader,
                      const rfid_reader_config_t *config,
                      rfid_reader_write_fn write,
                      rfid_reader_event_fn event,
                      void *context)
{
    memset(reader, 0, sizeof(*reader));
    reader->config = *config;
    if (reader->config.data_block_count == 0U ||
        reader->config.data_block_count > RFID_READER_MAX_BLOCKS) {
        reader->config.data_block_count = RFID_READER_MAX_BLOCKS;
    }
    if (reader->config.removal_confirmations == 0U) {
        reader->config.removal_confirmations = 1U;
    }
    reader->write = write;
    reader->event = event;
    reader->context = context;
    reader->state = RFID_READER_IDLE;
    rfid_protocol_parser_init(&reader->parser);
}

void rfid_reader_tick(rfid_reader_t *reader, uint32_t now_ms)
{
    if (reader == NULL) {
        return;
    }

    switch (reader->state) {
    case RFID_READER_IDLE:
        if (time_reached(now_ms, reader->next_action_ms) &&
            !send_uid_request(reader, now_ms, RFID_UID_DISCOVERY)) {
            schedule_discovery(reader, now_ms);
        }
        break;
    case RFID_READER_WAIT_UID:
        if (time_reached(now_ms, reader->deadline_ms)) {
            if (reader->uid_purpose == RFID_UID_REMOVAL_CHECK) {
                if (reader->absence_count < UINT8_MAX) {
                    reader->absence_count++;
                }
                if (reader->absence_count >= reader->config.removal_confirmations) {
                    reader->last_uid_valid = 0U;
                    schedule_discovery(reader, now_ms);
                } else {
                    reader->state = RFID_READER_WAIT_REMOVAL;
                    reader->next_action_ms =
                        now_ms + reader->config.removal_poll_interval_ms;
                }
            } else {
                schedule_discovery(reader, now_ms);
            }
        }
        break;
    case RFID_READER_WAIT_BLOCK:
        if (time_reached(now_ms, reader->deadline_ms)) {
            retry_or_fail_block(reader, now_ms);
        }
        break;
    case RFID_READER_WAIT_DISPATCH:
        if (time_reached(now_ms, reader->next_action_ms)) {
            if (emit_event(reader, RFID_READER_EVENT_TAG)) {
                memcpy(reader->last_uid, reader->current_uid, RFID_UID_SIZE);
                reader->last_uid_valid = 1U;
                enter_wait_removal(reader, now_ms);
            } else {
                reader->next_action_ms =
                    now_ms + reader->config.dispatch_retry_ms;
            }
        }
        break;
    case RFID_READER_WAIT_REMOVAL:
        if (time_reached(now_ms, reader->next_action_ms) &&
            !send_uid_request(reader, now_ms, RFID_UID_REMOVAL_CHECK)) {
            reader->next_action_ms =
                now_ms + reader->config.removal_poll_interval_ms;
        }
        break;
    default:
        schedule_discovery(reader, now_ms);
        break;
    }
}

void rfid_reader_feed(rfid_reader_t *reader, uint8_t byte, uint32_t now_ms)
{
    if (reader == NULL) {
        return;
    }

    rfid_frame_t frame;
    const rfid_parse_result_t result =
        rfid_protocol_parser_feed(&reader->parser, byte, &frame);
    if (result == RFID_PARSE_ERROR) {
        emit_event(reader, RFID_READER_EVENT_PROTOCOL_ERROR);
        return;
    }
    if (result != RFID_PARSE_FRAME || frame.address != 0x00U) {
        return;
    }

    if (frame.command == RFID_RESPONSE_UID) {
        handle_uid_frame(reader, &frame, now_ms);
    } else if (frame.command == RFID_RESPONSE_BLOCK) {
        handle_block_frame(reader, &frame, now_ms);
    }
}
