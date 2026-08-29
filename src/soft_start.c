/* soft_start.c：把 ADC 原始值换成缓启动等待毫秒数。不碰硬件。 */

#include <assert.h>
#include <stdint.h>

#include "app_config.h"
#include "soft_start.h"

enum {
    SOFT_START_SPAN_MS = APP_SOFT_START_MAX_MS - APP_SOFT_START_MIN_MS
};

/* 把原始值限制在 12 位满量程以内。纯函数。 */
static uint16_t soft_start_clamp_raw(uint16_t adc_raw);

/* 把已限制的原始值换成等待毫秒数。纯函数。 */
static uint32_t soft_start_map_raw_to_ms(uint16_t clamped_raw);

/* 规范要求返回模块状态枚举。
 * 计划冻结为返回毫秒数，这里不能改签名。 */
uint32_t soft_start_delay_ms(uint16_t adc_raw)
{
    uint16_t clamped_raw = soft_start_clamp_raw(adc_raw);

    return soft_start_map_raw_to_ms(clamped_raw);
}

static uint16_t soft_start_clamp_raw(uint16_t adc_raw)
{
    if (adc_raw > (uint16_t)APP_ADC_FULL_SCALE)
        return (uint16_t)APP_ADC_FULL_SCALE;
    return adc_raw;
}

static uint32_t soft_start_map_raw_to_ms(uint16_t clamped_raw)
{
    assert(clamped_raw <= (uint16_t)APP_ADC_FULL_SCALE);

    uint32_t delay_ms = (uint32_t)APP_SOFT_START_MIN_MS +
                        (uint32_t)SOFT_START_SPAN_MS * (uint32_t)clamped_raw /
                            (uint32_t)APP_ADC_FULL_SCALE;

    assert(delay_ms >= (uint32_t)APP_SOFT_START_MIN_MS);
    assert(delay_ms <= (uint32_t)APP_SOFT_START_MAX_MS);
    return delay_ms;
}
