/* announce.c：到点立刻发文字。没有队列，也不等念完。 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "announce.h"

static const uint8_t ANNOUNCE_SPEED_COMMAND[] = {
    '<', 'S', '>', '3'
};

/* 判断 speak_now 的参数是否能发。纯函数。 */
static bool announce_is_speak_args_valid(const announce_t *service,
                                         const uint8_t *text, size_t length);

/* 语速还待发就立刻发。已发过返回 true。
 * 写出失败返回 false，待发标记保留。 */
static bool announce_send_speed_if_pending(announce_t *service);

/* 适配注入的写回调。service、data 不能为空，length 不能为 0。 */
static bool announce_write_bytes(announce_t *service, const uint8_t *data,
                                 size_t length);

/* 判断语速命令是否还没发出。service 不能为空。 */
static bool announce_has_speed_pending(const announce_t *service);

/* 清掉待发语速标记。service 不能为空，且标记必须仍为待发。 */
static void announce_clear_speed_pending(announce_t *service);

/* 判断现在是否该发开机语速。service 为空或写回调为空时为假。 */
static bool announce_is_startup_speed_due(const announce_t *service,
                                          uint32_t now_ms);

/* 判断 now_ms 是否已到 deadline_ms。先做无符号减，翻转后仍然正确。 */
static bool announce_time_reached(uint32_t now_ms, uint32_t deadline_ms);

/* 规范要求最多 4 个参数，并返回状态枚举。
 * 计划沿用 tts_service_init 的五参数签名，且不返回状态。 */
void announce_init(announce_t *service, announce_write_fn write, void *context,
                   bool set_speed_on_startup, uint32_t now_ms)
{
    if (service == NULL)
        return;

    service->write = write;
    service->context = context;
    service->startup_deadline_ms = now_ms + (uint32_t)ANNOUNCE_STARTUP_DELAY_MS;
    service->speed_command_pending = set_speed_on_startup ? 1U : 0U;

    assert(service->write == write);
    assert((service->speed_command_pending == 0U) ||
           (service->speed_command_pending == 1U));
}

/* 规范要求返回状态枚举。计划规定成功失败都用 bool。 */
bool announce_speak_now(announce_t *service, const uint8_t *text, size_t length)
{
    if (!announce_is_speak_args_valid(service, text, length))
        return false;

    (void)announce_send_speed_if_pending(service);
    return announce_write_bytes(service, text, length);
}

/* 规范要求返回状态枚举。计划沿用 tts_service_tick 的 void。 */
void announce_tick(announce_t *service, uint32_t now_ms)
{
    if (!announce_is_startup_speed_due(service, now_ms))
        return;

    (void)announce_send_speed_if_pending(service);
}

static bool announce_is_speak_args_valid(const announce_t *service,
                                         const uint8_t *text, size_t length)
{
    if (service == NULL)
        return false;
    if (service->write == NULL)
        return false;
    if (text == NULL)
        return false;
    if (length == 0U)
        return false;
    return true;
}

static bool announce_send_speed_if_pending(announce_t *service)
{
    assert(service != NULL);

    if (!announce_has_speed_pending(service))
        return true;
    if (!announce_write_bytes(service, ANNOUNCE_SPEED_COMMAND,
                              sizeof(ANNOUNCE_SPEED_COMMAND)))
        return false;

    announce_clear_speed_pending(service);
    return true;
}

static bool announce_write_bytes(announce_t *service, const uint8_t *data,
                                 size_t length)
{
    assert(service != NULL);
    assert(data != NULL);
    assert(length > 0U);
    assert(service->write != NULL);

    announce_write_fn write = service->write;

    return write(service->context, data, length);
}

static bool announce_has_speed_pending(const announce_t *service)
{
    assert(service != NULL);
    return service->speed_command_pending != 0U;
}

static void announce_clear_speed_pending(announce_t *service)
{
    assert(service != NULL);
    assert(service->speed_command_pending != 0U);

    service->speed_command_pending = 0U;
    assert(service->speed_command_pending == 0U);
}

static bool announce_is_startup_speed_due(const announce_t *service,
                                          uint32_t now_ms)
{
    if (service == NULL)
        return false;
    if (service->write == NULL)
        return false;
    if (!announce_has_speed_pending(service))
        return false;
    return announce_time_reached(now_ms, service->startup_deadline_ms);
}

static bool announce_time_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}
