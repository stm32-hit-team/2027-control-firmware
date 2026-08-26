/*
 * tts_service.h —— 语音播报服务的接口。
 *
 * 语音模块的特点是播报要花时间，比如念四个字大概一秒。
 * 如果一条没念完就发下一条，前一条会被打断。
 *
 * 所以这里做了一个队列加一个忙等标记：
 *   文字先进队列，服务按顺序发。
 *   每发一条就按字数估算它要念多久，在这段时间里不发新的。
 *
 * 整个过程不阻塞。主循环反复调 tick，能发就发，不能发就直接返回。
 */
#ifndef TTS_SERVICE_H
#define TTS_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 单条文字最长 64 字节。和 RFID 一次能读出的文字长度对齐。 */
#define TTS_TEXT_CAPACITY 64U

/* 队列能存 8 条。够应付连续刷卡的情况。 */
#define TTS_QUEUE_DEPTH 8U

/*
 * 发送回调的类型。
 * 服务本身不知道怎么发数据，需要发的时候回调这个函数。
 * 单片机上它指向串口发送，电脑测试时指向一个记录函数。
 */
typedef bool (*tts_write_fn)(void *context, const uint8_t *data, size_t length);

/*
 * 队列里的一条消息。
 *
 * 注意这里显式存了长度，没有依赖字符串结尾的 0。
 * 原因是 GB2312 编码的汉字字节里可能出现 0x00，用 0 当结尾会截断。
 */
typedef struct {
    uint8_t data[TTS_TEXT_CAPACITY];
    size_t length;
} tts_message_t;

/*
 * 服务的全部状态。
 *
 * write / context        发送回调和它的参数。
 * queue / head / count   环形队列本体、队首下标、当前条数。
 * busy_until_ms          在这个时刻之前不发新消息，等上一条念完。
 * startup_deadline_ms    开机后要等到这个时刻才发第一条命令。
 * speed_command_pending  语速命令还没发出去，标记为 1。
 */
typedef struct {
    tts_write_fn write;
    void *context;
    tts_message_t queue[TTS_QUEUE_DEPTH];
    size_t head;
    size_t count;
    uint32_t busy_until_ms;
    uint32_t startup_deadline_ms;
    uint8_t speed_command_pending;
} tts_service_t;

/*
 * 初始化服务。
 * set_speed_on_startup 为 true 时，开机会先发一条语速设置命令。
 * now_ms 用来算开机延时的起点。
 */
void tts_service_init(tts_service_t *service, tts_write_fn write, void *context,
                      bool set_speed_on_startup, uint32_t now_ms);

/*
 * 把一段文字放进队列。
 * 队列满了返回 false，调用方需要自己决定是丢掉还是稍后重试。
 * 本工程的做法是稍后重试，见 app.c 里的 on_rfid_event。
 */
bool tts_service_enqueue(tts_service_t *service, const uint8_t *text,
                         size_t length);

/* 推进一步。主循环里反复调用。 */
void tts_service_tick(tts_service_t *service, uint32_t now_ms);

#endif
