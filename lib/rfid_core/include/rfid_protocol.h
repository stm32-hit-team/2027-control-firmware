/*
 * rfid_protocol.h —— RFID 读卡器串口协议的编解码接口。
 *
 * 协议长这样：
 *   0x7F  长度  地址  命令  [状态]  [数据...]  校验
 *
 * 三个要点：
 *   1. 0x7F 是帧头。数据里出现 0x7F 时要写成两个 0x7F，接收方再还原成一个。
 *      这叫字节填充，作用是让帧头唯一，收到单个 0x7F 就知道新帧开始了。
 *   2. 长度字段算的是“去掉帧头之后到校验字节之前”的字节数，再加上校验本身。
 *   3. 校验是长度字段和整个帧体逐字节异或。
 *
 * 这个文件只管拼帧和拆帧，不管什么时候该发什么。那是 rfid_reader 的事。
 */
#ifndef RFID_PROTOCOL_H
#define RFID_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

/* 帧头字节。同时也是转义字符。 */
#define RFID_PROTOCOL_HEADER 0x7FU

/* 帧体最大长度。协议长度字段是一个字节，这是实际用到的上限。 */
#define RFID_PROTOCOL_MAX_BODY_SIZE 67U

/* 数据段最大长度。够放 UID 加上四个数据块。 */
#define RFID_PROTOCOL_MAX_PAYLOAD_SIZE 64U

/* 去掉帧头和转义之后的原始字节上限，帧体再加长度和校验两个字节。 */
#define RFID_PROTOCOL_MAX_RAW_SIZE (RFID_PROTOCOL_MAX_BODY_SIZE + 2U)

/*
 * 编码后的最大字节数。
 * 最坏情况是每个字节都是 0x7F，全都要翻倍，再加一个帧头。
 * 按最坏情况开缓冲区，就不会有溢出风险。
 */
#define RFID_PROTOCOL_MAX_ENCODED_SIZE (1U + 2U * RFID_PROTOCOL_MAX_RAW_SIZE)

/* 命令码。请求用 0x1x，对应的回复用 0x9x。 */
#define RFID_REQUEST_UID 0x10U   /* 问一下有没有卡，有就把 UID 报上来 */
#define RFID_REQUEST_BLOCK 0x11U /* 读某一个数据块 */
#define RFID_RESPONSE_UID 0x90U  /* UID 请求的回复 */
#define RFID_RESPONSE_BLOCK 0x91U/* 数据块请求的回复 */

/* 回复里的状态码。 */
#define RFID_STATUS_OK 0x00U      /* 操作成功 */
#define RFID_STATUS_NO_CARD 0xFFU /* 读卡区里没有卡 */

/* 解析器每吃一个字节后的三种结果。 */
typedef enum {
    RFID_PARSE_NONE = 0,  /* 帧还没收完，继续喂 */
    RFID_PARSE_FRAME,     /* 收到一个完整且校验正确的帧 */
    RFID_PARSE_ERROR,     /* 校验错、长度错，或者帧太长。该帧丢弃 */
} rfid_parse_result_t;

/* 解析成功后得到的一帧内容。 */
typedef struct {
    uint8_t address;      /* 设备地址。这块板子上一直是 0x00 */
    uint8_t command;      /* 命令码，比如 0x90 */
    uint8_t status;       /* 状态码，比如 0x00 表示成功 */
    uint8_t payload[RFID_PROTOCOL_MAX_PAYLOAD_SIZE]; /* 数据段 */
    size_t payload_length;                           /* 数据段实际长度 */
} rfid_frame_t;

/*
 * 解析器的内部状态。
 *
 * raw                 已经去掉转义的原始字节。
 * raw_count           已经收了多少个。
 * expected_raw_count  根据长度字段算出来一共该收多少个。
 * in_frame            是不是已经见过帧头，正在收帧中。
 * escape_pending      上一个字节是 0x7F，还不确定它是帧头还是转义。
 *
 * 这个结构体不含指针，也不动态分配内存，可以直接放在栈上或全局区。
 */
typedef struct {
    uint8_t raw[RFID_PROTOCOL_MAX_RAW_SIZE];
    size_t raw_count;
    size_t expected_raw_count;
    uint8_t in_frame;
    uint8_t escape_pending;
} rfid_protocol_parser_t;

/* 复位解析器。开始用之前调一次。 */
void rfid_protocol_parser_init(rfid_protocol_parser_t *parser);

/*
 * 往解析器喂一个字节，返回这一步的结果。
 * 返回 RFID_PARSE_FRAME 时，frame 里才有有效内容。
 */
rfid_parse_result_t rfid_protocol_parser_feed(rfid_protocol_parser_t *parser,
                                               uint8_t byte,
                                               rfid_frame_t *frame);

/*
 * 把帧体编码成可以直接发出去的字节流。
 * 自动补帧头、算长度、算校验、做转义。
 * 返回实际写进 encoded 的字节数。空间不够就返回 0。
 */
size_t rfid_protocol_encode_body(const uint8_t *body, size_t body_length,
                                 uint8_t *encoded, size_t capacity);

/* 拼一条“读 UID”命令。结果固定是 7F 03 00 10 13 这五个字节。 */
size_t rfid_protocol_encode_read_uid(uint8_t *encoded, size_t capacity);

/* 拼一条“读指定块”命令。block 是块号。 */
size_t rfid_protocol_encode_read_block(uint8_t block, uint8_t *encoded,
                                       size_t capacity);

#endif
