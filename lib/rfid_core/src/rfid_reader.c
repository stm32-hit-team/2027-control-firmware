/*
 * rfid_reader.c —— 读卡流程状态机的实现。
 *
 * 代码分两半：
 *   rfid_reader_tick 按时间驱动，负责发命令和判超时。
 *   rfid_reader_feed 按数据驱动，负责处理收到的回复。
 * 两者共同推进 state 字段，谁都不阻塞。
 *
 * 状态流转图见 rfid_reader.h 的开头注释。
 */
#include "rfid_reader.h"

#include <string.h>

/* 时间比较。先无符号相减再转有符号，计数器翻转时也不会误判。 */
static bool time_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

/*
 * 向上层汇报一个事件。
 *
 * 返回值直接透传上层的意思：
 *   true  上层收下了，可以往下走。
 *   false 上层暂时处理不了，需要稍后重试。
 * 没注册回调时一律当成 true，这样库单独用也不会卡住。
 *
 * 只有 TAG 事件才拷贝文字。其他事件的文字字段没有意义。
 */
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

/* 把编好的命令发出去。长度为 0 或没注册回调都算发送失败。 */
static bool send_encoded(rfid_reader_t *reader, const uint8_t *encoded,
                         size_t length)
{
    return length > 0U && reader->write != NULL &&
           reader->write(reader->context, encoded, length);
}

/* 回到空闲状态，隔一个轮询周期后再去找卡。 */
static void schedule_discovery(rfid_reader_t *reader, uint32_t now_ms)
{
    reader->state = RFID_READER_IDLE;
    reader->next_action_ms = now_ms + reader->config.poll_interval_ms;
}

/*
 * 进入“等卡拿走”状态。
 * 顺便清零 absence_count，重新开始数连续读不到的次数。
 */
static void enter_wait_removal(rfid_reader_t *reader, uint32_t now_ms)
{
    reader->state = RFID_READER_WAIT_REMOVAL;
    reader->absence_count = 0U;
    reader->next_action_ms = now_ms + reader->config.removal_poll_interval_ms;
}

/*
 * 发一条 UID 请求。
 *
 * purpose 记下这次请求的目的，收到回复时按目的分别处理。
 * 命令内容两种目的完全一样，区别只在回复的处理方式。
 * 发送失败就不改状态，让调用方决定怎么办。
 */
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

/* 发一条读块命令，块号取自 block_index 指向的那一个。 */
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

/*
 * 发现新卡，开始读它的数据。
 * 先把文字缓冲区、块序号、重试计数全部清干净，
 * 否则上一张卡的残留内容会混进来。
 */
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
        /* 命令发不出去，退回空闲，下个周期重新来。 */
        schedule_discovery(reader, now_ms);
    }
}

/*
 * 检查文字是不是合法的 GB2312 编码。
 *
 * 两种字节是允许的：
 *   0x20 到 0x7E          可打印的 ASCII，算一个字符
 *   0xA1-0xF7 配 0xA1-0xFE 一个 GB2312 汉字，占两个字节
 * 其他情况都判为非法。
 *
 * 加这道检查是为了拦住空白卡和写错的卡。
 * 乱码发给语音模块只会念出噪声，还不如直接报错让人知道卡有问题。
 */
static bool text_is_valid_gb2312(const uint8_t *text, size_t length)
{
    size_t index = 0U;
    while (index < length) {
        const uint8_t first = text[index];
        if (first >= 0x20U && first <= 0x7EU) {
            index++;
            continue;
        }
        /* 汉字首字节必须在范围内，而且后面还得有一个字节配对。 */
        if (first < 0xA1U || first > 0xF7U || index + 1U >= length) {
            return false;
        }
        const uint8_t second = text[index + 1U];
        if (second < 0xA1U || second > 0xFEU) {
            return false;
        }
        index += 2U;
    }
    /* 空文字也算不合法。没内容就没什么可播报的。 */
    return length > 0U;
}

/*
 * 所有数据块都读完了，收尾。
 *
 * 三步：
 *   1. 去掉末尾的空格、回车、换行、制表符。写卡工具经常补这些。
 *   2. 校验编码。不合法就报 INVALID_TEXT，然后等卡拿走。
 *   3. 上报 TAG 事件。上层收下了就记住这张卡的 UID，然后等卡拿走。
 *
 * 第 3 步如果上层没收下，就进 WAIT_DISPATCH 状态定时重试。
 * 关键是这时候不更新 last_uid。
 * 只有真正播报成功才算处理完，否则这张卡还有机会被重新处理。
 */
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
        /* 播报成功。记住这张卡，避免它一直放着被反复播报。 */
        memcpy(reader->last_uid, reader->current_uid, RFID_UID_SIZE);
        reader->last_uid_valid = 1U;
        enter_wait_removal(reader, now_ms);
    } else {
        /* 上层忙。稍后重试，此时故意不更新 last_uid。 */
        reader->state = RFID_READER_WAIT_DISPATCH;
        reader->next_action_ms = now_ms + reader->config.dispatch_retry_ms;
    }
}

/*
 * 读块失败后的处理：还有重试次数就重发，用完了就报超时。
 * 报超时之后进等卡拿走状态，而不是回空闲。
 * 这样避免对着一张有问题的卡反复重试，白占轮询时间。
 */
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

/*
 * 处理 UID 回复。
 *
 * 分四种情况：
 *   1. 状态不是成功，且这次是在确认卡还在不在
 *      给缺席计数加一。攒够次数才认定卡拿走了，然后回空闲找新卡。
 *      没攒够就继续等，这是防抖。
 *   2. 状态不是成功，且这次是在找新卡
 *      读卡区没卡，回空闲下个周期再问。
 *   3. 状态成功，但 UID 和上一张播报过的卡相同
 *      同一张卡还放着，什么都不做，继续等它被拿走。
 *   4. 状态成功，UID 是新的
 *      开始读这张卡的数据块。
 */
static void handle_uid_frame(rfid_reader_t *reader, const rfid_frame_t *frame,
                             uint32_t now_ms)
{
    /* 不在等 UID 回复的状态里收到这个帧，说明是过期数据，丢掉。 */
    if (reader->state != RFID_READER_WAIT_UID) {
        return;
    }

    if (frame->status != RFID_STATUS_OK) {
        if (reader->uid_purpose == RFID_UID_REMOVAL_CHECK) {
            /* 加计数时防一手溢出，虽然实际达不到 255。 */
            if (reader->absence_count < UINT8_MAX) {
                reader->absence_count++;
            }
            if (reader->absence_count >= reader->config.removal_confirmations) {
                /* 确认卡拿走了。清掉记录，下张同样的卡可以重新播报。 */
                reader->last_uid_valid = 0U;
                schedule_discovery(reader, now_ms);
            } else {
                /* 还没确认，隔一会儿再问一次。 */
                reader->state = RFID_READER_WAIT_REMOVAL;
                reader->next_action_ms =
                    now_ms + reader->config.removal_poll_interval_ms;
            }
        } else {
            schedule_discovery(reader, now_ms);
        }
        return;
    }

    /*
     * 长度必须严格等于 2 加 UID 长度。多一个字节少一个字节都不接受。
     * 严格校验能挡住串口噪声凑出的巧合帧。
     */
    if (frame->payload_length != 2U + RFID_UID_SIZE) {
        emit_event(reader, RFID_READER_EVENT_PROTOCOL_ERROR);
        schedule_discovery(reader, now_ms);
        return;
    }

    /* 前两个字节是卡类型信息，UID 从第三个字节开始。 */
    const uint8_t *uid = &frame->payload[2];

    /* 在确认卡还在不在，而且确实是同一张卡。继续等就好。 */
    if (reader->uid_purpose == RFID_UID_REMOVAL_CHECK &&
        reader->last_uid_valid != 0U &&
        memcmp(uid, reader->last_uid, RFID_UID_SIZE) == 0) {
        reader->absence_count = 0U;
        reader->state = RFID_READER_WAIT_REMOVAL;
        reader->next_action_ms = now_ms + reader->config.removal_poll_interval_ms;
        return;
    }

    /*
     * 在找新卡，但摸到的还是刚播报过的那张。
     * 转去等它被拿走，不要重复播报。
     */
    if (reader->uid_purpose == RFID_UID_DISCOVERY &&
        reader->last_uid_valid != 0U &&
        memcmp(uid, reader->last_uid, RFID_UID_SIZE) == 0) {
        enter_wait_removal(reader, now_ms);
        return;
    }

    /* 确实是新卡，开始读数据。 */
    begin_read(reader, uid, now_ms);
}

/*
 * 处理数据块回复。
 *
 * 校验有三层：状态码、长度、以及回复里的 UID 和当前卡是否一致。
 * 第三层是防止换卡的瞬间把两张卡的数据拼到一起。
 *
 * 校验通过后逐字节把数据搬进文字缓冲区。
 * 遇到 0x00 或 0xFF 就停，这两个值都表示后面是空白，文字到此结束。
 */
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

    /* 跳过卡类型和 UID，真正的块数据从这里开始。 */
    const uint8_t *block = &frame->payload[2U + RFID_UID_SIZE];
    bool terminated = false;
    for (size_t i = 0U; i < RFID_BLOCK_SIZE; ++i) {
        if (block[i] == 0x00U || block[i] == 0xFFU) {
            terminated = true;
            break;
        }
        /* 缓冲区放不下了。说明卡上数据超预期，判为非法。 */
        if (reader->text_length >= sizeof(reader->text)) {
            emit_event(reader, RFID_READER_EVENT_INVALID_TEXT);
            enter_wait_removal(reader, now_ms);
            return;
        }
        reader->text[reader->text_length++] = block[i];
    }

    /* 碰到结束标记，或者块已经读完，就去收尾。 */
    if (terminated || reader->block_index + 1U >= reader->config.data_block_count) {
        finish_text(reader, now_ms);
        return;
    }

    /* 还有块要读。序号往后挪，重试计数归零。 */
    reader->block_index++;
    reader->retry_count = 0U;
    if (!send_current_block(reader, now_ms)) {
        retry_or_fail_block(reader, now_ms);
    }
}

/*
 * 填一份默认参数。
 *
 * 默认读 4、5、6、8 号块。跳过 7 号是因为 M1 卡每个扇区的最后一块
 * 存的是密钥和权限位，不是用户数据。7 号正好是第二扇区的尾块。
 */
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

/*
 * 初始化状态机。
 * 对两个关键参数做了兜底修正：块数为 0 或超限就改成默认值，
 * 确认次数为 0 就改成 1。这样传进来的参数再离谱也不会跑飞。
 */
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

/*
 * 按时间推进一步。主循环里反复调用。
 *
 * 每个状态各管一件事：
 *   IDLE          到点了就发一条找卡命令
 *   WAIT_UID      超时了就按当次目的做相应处理
 *   WAIT_BLOCK    超时了就重试或者报错
 *   WAIT_DISPATCH 到点了再试一次上报事件
 *   WAIT_REMOVAL  到点了发一条命令确认卡还在不在
 *
 * 注意每个分支都不阻塞，条件不满足就直接跳过。
 */
void rfid_reader_tick(rfid_reader_t *reader, uint32_t now_ms)
{
    if (reader == NULL) {
        return;
    }

    switch (reader->state) {
    case RFID_READER_IDLE:
        if (time_reached(now_ms, reader->next_action_ms) &&
            !send_uid_request(reader, now_ms, RFID_UID_DISCOVERY)) {
            /* 发不出去，重新排一次，下个周期再试。 */
            schedule_discovery(reader, now_ms);
        }
        break;
    case RFID_READER_WAIT_UID:
        if (time_reached(now_ms, reader->deadline_ms)) {
            /*
             * 超时没收到回复。
             * 如果是在确认卡还在不在，那就当作一次缺席计入。
             * 处理逻辑和收到“无卡”回复完全一致。
             */
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
                /* 这次上层收下了，正式记住这张卡。 */
                memcpy(reader->last_uid, reader->current_uid, RFID_UID_SIZE);
                reader->last_uid_valid = 1U;
                enter_wait_removal(reader, now_ms);
            } else {
                /* 还是忙，再等一轮。 */
                reader->next_action_ms =
                    now_ms + reader->config.dispatch_retry_ms;
            }
        }
        break;
    case RFID_READER_WAIT_REMOVAL:
        if (time_reached(now_ms, reader->next_action_ms) &&
            !send_uid_request(reader, now_ms, RFID_UID_REMOVAL_CHECK)) {
            /* 命令没发出去，隔一会儿重来，不改状态。 */
            reader->next_action_ms =
                now_ms + reader->config.removal_poll_interval_ms;
        }
        break;
    default:
        /* 状态值意外损坏时的兜底，回到空闲重新开始。 */
        schedule_discovery(reader, now_ms);
        break;
    }
}

/*
 * 喂一个收到的字节。
 *
 * 先交给协议解析器。凑不成完整帧就直接返回。
 * 凑成了再按命令码分给对应的处理函数。
 *
 * 地址不是 0x00 的帧一律忽略。这块板子上只有一个读卡器，
 * 别的地址只可能是噪声或者串线。
 */
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
