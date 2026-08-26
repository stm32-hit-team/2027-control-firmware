/*
 * rfid_reader.h —— 读卡流程状态机的接口。
 *
 * 这个模块负责“什么时候该发什么命令”，是整个读卡逻辑的核心。
 * 编解码交给 rfid_protocol，发数据交给回调，它只管流程。
 *
 * 正常的一次读卡是这样走的：
 *   IDLE          每隔 80 毫秒问一次有没有卡
 *   WAIT_UID      等回复。没卡就回 IDLE，有卡就记下 UID 往下走
 *   WAIT_BLOCK    依次读 4、5、6、8 号块，把文字拼起来
 *   WAIT_DISPATCH 文字交给语音队列。队列满就停在这儿重试
 *   WAIT_REMOVAL  卡还在就一直等，直到确认卡被拿走才回 IDLE
 *
 * 最后那个 WAIT_REMOVAL 状态是关键。
 * 没有它的话，卡一直放着会被反复读到，语音就会一直重复播报。
 *
 * 整个状态机不阻塞，也不用任何硬件相关的东西。
 * 所以它能直接在电脑上编译运行，用假数据跑测试。
 */
#ifndef RFID_READER_H
#define RFID_READER_H

#include "rfid_protocol.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RFID_UID_SIZE 4U        /* M1 卡的 UID 是 4 字节 */
#define RFID_BLOCK_SIZE 16U     /* M1 卡每块 16 字节 */
#define RFID_TEXT_CAPACITY 64U  /* 拼起来的文字上限，正好 4 块 */
#define RFID_READER_MAX_BLOCKS 4U /* 最多读几个块 */

/* 状态机向外汇报的四种事件。 */
typedef enum {
    RFID_READER_EVENT_TAG = 0,       /* 读到一张新卡，文字已经拼好 */
    RFID_READER_EVENT_PROTOCOL_ERROR,/* 帧格式或长度不对 */
    RFID_READER_EVENT_READ_TIMEOUT,  /* 重试完了还是读不出来 */
    RFID_READER_EVENT_INVALID_TEXT,  /* 读出来了但不是合法的 GB2312 文字 */
} rfid_reader_event_type_t;

/*
 * 事件的内容。
 * uid 一直有效。text 和 text_length 只在 TAG 事件里有意义。
 */
typedef struct {
    rfid_reader_event_type_t type;
    uint8_t uid[RFID_UID_SIZE];
    uint8_t text[RFID_TEXT_CAPACITY];
    size_t text_length;
} rfid_reader_event_t;

/* 发送回调。状态机要发命令时调用它。 */
typedef bool (*rfid_reader_write_fn)(void *context, const uint8_t *data,
                                     size_t length);

/*
 * 事件回调。状态机有事汇报时调用它。
 *
 * 返回值是一个反压机制：
 *   true  上层收下了，状态机继续往前走。
 *   false 上层暂时处理不了，状态机保住这张卡，过一会儿再送一次。
 * 有了这个机制，语音队列满的时候卡不会被漏掉。
 */
typedef bool (*rfid_reader_event_fn)(void *context,
                                     const rfid_reader_event_t *event);

/* 可调参数。含义和 app_config.h 里同名的宏一致。 */
typedef struct {
    uint32_t poll_interval_ms;         /* 空闲时的轮询间隔 */
    uint32_t response_timeout_ms;      /* 等回复的超时时间 */
    uint32_t removal_poll_interval_ms; /* 确认卡还在不在的间隔 */
    uint32_t dispatch_retry_ms;        /* 上层忙时的重试间隔 */
    uint8_t retry_limit;               /* 单块读失败的重试次数 */
    uint8_t removal_confirmations;     /* 连续几次读不到才算卡拿走了 */
    uint8_t data_blocks[RFID_READER_MAX_BLOCKS]; /* 要读哪几个块 */
    size_t data_block_count;                     /* 实际读几个 */
} rfid_reader_config_t;

/* 状态机的五个状态。含义见本文件开头的流程说明。 */
typedef enum {
    RFID_READER_IDLE = 0,
    RFID_READER_WAIT_UID,
    RFID_READER_WAIT_BLOCK,
    RFID_READER_WAIT_DISPATCH,
    RFID_READER_WAIT_REMOVAL,
} rfid_reader_state_t;

/*
 * 发 UID 请求的两种目的。
 *
 * 命令本身完全一样，但收到回复后的处理不同：
 *   DISCOVERY     在找新卡。发现卡就开始读数据。
 *   REMOVAL_CHECK 在确认旧卡还在不在。卡还在就什么都不做，继续等。
 * 用一个字段区分，就不用为两种场合写两套解析代码。
 */
typedef enum {
    RFID_UID_DISCOVERY = 0,
    RFID_UID_REMOVAL_CHECK,
} rfid_uid_purpose_t;

/*
 * 状态机的全部状态。
 *
 * current_uid    正在处理的这张卡的 UID。
 * last_uid       上一张成功播报过的卡的 UID。
 * last_uid_valid last_uid 里的内容有没有效。
 *
 * 用 UID 而不是文字内容来判断是不是同一张卡。
 * 因为两张不同的卡可能写了同样的文字，那种情况应该各播报一次。
 *
 * absence_count  连续读不到卡的次数。攒够了才认为卡真的拿走了。
 *                这是防抖，避免手抖一下就重复播报。
 */
typedef struct {
    rfid_reader_config_t config;
    rfid_protocol_parser_t parser;
    rfid_reader_write_fn write;
    rfid_reader_event_fn event;
    void *context;
    rfid_reader_state_t state;
    rfid_uid_purpose_t uid_purpose;
    uint32_t deadline_ms;    /* 等回复的超时时刻 */
    uint32_t next_action_ms; /* 下一次该动作的时刻 */
    uint8_t current_uid[RFID_UID_SIZE];
    uint8_t last_uid[RFID_UID_SIZE];
    uint8_t text[RFID_TEXT_CAPACITY];
    size_t text_length;
    size_t block_index;   /* 当前读到第几个块 */
    uint8_t retry_count;  /* 当前块已经重试几次 */
    uint8_t absence_count;
    uint8_t last_uid_valid;
} rfid_reader_t;

/* 填一份默认参数。默认读 4、5、6、8 号块。 */
void rfid_reader_default_config(rfid_reader_config_t *config);

/* 初始化状态机。config 会被复制一份存起来，调用后可以释放。 */
void rfid_reader_init(rfid_reader_t *reader,
                      const rfid_reader_config_t *config,
                      rfid_reader_write_fn write,
                      rfid_reader_event_fn event,
                      void *context);

/* 按时间推进一步。负责发命令和判超时。主循环里反复调用。 */
void rfid_reader_tick(rfid_reader_t *reader, uint32_t now_ms);

/* 喂一个收到的字节。凑成完整帧就顺手处理掉。 */
void rfid_reader_feed(rfid_reader_t *reader, uint8_t byte, uint32_t now_ms);

#endif
