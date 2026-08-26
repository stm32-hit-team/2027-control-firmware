/*
 * tts_service.c —— 语音播报队列的实现。
 *
 * 要解决的问题很具体：语音模块念一句话要时间，念的过程中不能打断它。
 * 但模块本身不会告诉我们念完了没有。
 *
 * 解决办法是按字数估算时长。
 * 发出一条之后，算出它大概要念多久，这段时间里不发新的。
 * 估算值宁可偏大也不偏小。偏大只是多等一会儿，偏小会把话截断。
 */
#include "tts_service.h"

#include <string.h>

/* 开机后等 500 毫秒再发第一条命令，留时间给模块自己初始化。 */
#define TTS_STARTUP_DELAY_MS 500U

/* 发完语速命令后等 300 毫秒，让模块把设置吃进去。 */
#define TTS_SPEED_SETTLE_MS 300U

/* 每条播报的固定开销。包含模块处理和起头的停顿。 */
#define TTS_BASE_SPEECH_MS 300U

/* 每个字大概念多久。180 毫秒是按 3 档语速实测估的。 */
#define TTS_MS_PER_GB2312_CHARACTER 180U

/* 时间比较。先无符号相减再转有符号，这样计数器翻转时也不会误判。 */
static bool time_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

/*
 * 按字数估算播报时长。
 *
 * 难点在于要先数出有几个字，而不是几个字节。
 * GB2312 的汉字占两个字节，ASCII 占一个。
 * 判断规则是：首字节落在 0xA1 到 0xF7、次字节落在 0xA1 到 0xFE，
 * 这两个字节就是一个汉字，一起跳过。否则算一个 ASCII 字符。
 *
 * 数完字数，乘上每字耗时，再加上固定开销。
 */
static uint32_t estimate_speech_ms(const uint8_t *text, size_t byte_length)
{
    size_t character_count = 0U;
    size_t index = 0U;
    while (index < byte_length) {
        const bool is_gb2312_pair =
            text[index] >= 0xA1U && text[index] <= 0xF7U &&
            index + 1U < byte_length && text[index + 1U] >= 0xA1U &&
            text[index + 1U] <= 0xFEU;
        index += is_gb2312_pair ? 2U : 1U;
        character_count++;
    }
    return TTS_BASE_SPEECH_MS +
           (uint32_t)character_count * TTS_MS_PER_GB2312_CHARACTER;
}

/*
 * 初始化服务。
 * 先整体清零，保证所有计数和下标都是干净的初始值。
 */
void tts_service_init(tts_service_t *service, tts_write_fn write, void *context,
                      bool set_speed_on_startup, uint32_t now_ms)
{
    memset(service, 0, sizeof(*service));
    service->write = write;
    service->context = context;
    service->speed_command_pending = set_speed_on_startup ? 1U : 0U;
    service->startup_deadline_ms = now_ms + TTS_STARTUP_DELAY_MS;
}

/*
 * 把一段文字放进队列尾部。
 *
 * 队尾位置由队首加条数算出来，所以只需要维护 head 和 count 两个变量。
 * 队列满了直接返回 false，不覆盖旧数据。
 * 覆盖旧数据会让前面的卡漏播，返回 false 让调用方重试更安全。
 */
bool tts_service_enqueue(tts_service_t *service, const uint8_t *text,
                         size_t length)
{
    if (service == NULL || text == NULL || length == 0U ||
        length > TTS_TEXT_CAPACITY || service->count >= TTS_QUEUE_DEPTH) {
        return false;
    }

    const size_t tail = (service->head + service->count) % TTS_QUEUE_DEPTH;
    memcpy(service->queue[tail].data, text, length);
    service->queue[tail].length = length;
    service->count++;
    return true;
}

/*
 * 推进一步。主循环里反复调用。
 *
 * 判断顺序是固定的：
 *   1. 上一条还没念完，什么都不做。
 *   2. 语速命令还没发，先发它。语速必须在正文之前设好。
 *   3. 队列空，没事干。
 *   4. 取队首那条发出去，算好忙到什么时候，然后出队。
 *
 * 注意第 4 步：只有发送成功才出队。
 * 发送失败就原地留着，下一轮再试，消息不会丢。
 */
void tts_service_tick(tts_service_t *service, uint32_t now_ms)
{
    /* 语速命令。<S>3 是中速，比默认稍慢，现场听得更清楚。 */
    static const uint8_t speed_command[] = {'<', 'S', '>', '3'};

    if (service == NULL || service->write == NULL ||
        !time_reached(now_ms, service->busy_until_ms)) {
        return;
    }

    if (service->speed_command_pending != 0U) {
        /* 开机延时还没到，再等等。模块刚上电不一定能收命令。 */
        if (!time_reached(now_ms, service->startup_deadline_ms)) {
            return;
        }
        if (service->write(service->context, speed_command,
                           sizeof(speed_command))) {
            service->speed_command_pending = 0U;
            service->busy_until_ms = now_ms + TTS_SPEED_SETTLE_MS;
        }
        /* 不管成没成，这一轮都到此为止。成了下轮再发正文。 */
        return;
    }

    if (service->count == 0U) {
        return;
    }

    const tts_message_t *message = &service->queue[service->head];
    if (!service->write(service->context, message->data, message->length)) {
        /* 发失败了。保住这条消息，下一轮重试。 */
        return;
    }
    service->busy_until_ms =
        now_ms + estimate_speech_ms(message->data, message->length);
    service->head = (service->head + 1U) % TTS_QUEUE_DEPTH;
    service->count--;
}
