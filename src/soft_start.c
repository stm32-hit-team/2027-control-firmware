/* soft_start.c：把 ADC 原始值换成停车毫秒数。不碰硬件。 */

#include <assert.h>
#include <stdint.h>

#include "app_config.h"
#include "soft_start.h"

enum {
    SOFT_START_STOP_SPAN_MS =
        APP_MOTOR_STOP_MAX_MS - APP_MOTOR_STOP_MIN_MS
};

/* 把原始值限制在 12 位满量程以内。纯函数。 */
static uint16_t soft_start_clamp_raw(uint16_t adc_raw);

/* 按省赛分压公式估电位器电阻，单位欧。纯函数。 */
static uint32_t soft_start_knob_res_ohms(uint16_t clamped_raw);

/* 把电阻换成停车毫秒数。纯函数。 */
static uint32_t soft_start_map_res_to_stop_ms(uint32_t res_ohms);

/* 规范要求返回模块状态枚举。
 * 计划冻结为返回毫秒数，这里不能改签名。 */
uint32_t soft_start_stop_ms(uint16_t adc_raw)
{
    uint16_t clamped_raw = soft_start_clamp_raw(adc_raw);
    uint32_t res_ohms = soft_start_knob_res_ohms(clamped_raw);
    uint32_t stop_ms = soft_start_map_res_to_stop_ms(res_ohms);

    assert(stop_ms >= (uint32_t)APP_MOTOR_STOP_MIN_MS);
    assert(stop_ms <= (uint32_t)APP_MOTOR_STOP_MAX_MS);
    return stop_ms;
}

static uint16_t soft_start_clamp_raw(uint16_t adc_raw)
{
    if (adc_raw > (uint16_t)APP_ADC_FULL_SCALE)
        return (uint16_t)APP_ADC_FULL_SCALE;
    return adc_raw;
}

static uint32_t soft_start_knob_res_ohms(uint16_t clamped_raw)
{
    assert(clamped_raw <= (uint16_t)APP_ADC_FULL_SCALE);

    if (clamped_raw >= (uint16_t)APP_ADC_FULL_SCALE)
        return 0;

    uint32_t drop_ohms =
        (uint32_t)clamped_raw * (uint32_t)APP_MOTOR_KNOB_SERIES_OHMS /
        ((uint32_t)APP_ADC_FULL_SCALE - (uint32_t)clamped_raw);

    if (drop_ohms >= (uint32_t)APP_MOTOR_KNOB_RES_OHMS)
        return (uint32_t)APP_MOTOR_KNOB_RES_OHMS + 1U;
    return (uint32_t)APP_MOTOR_KNOB_RES_OHMS - drop_ohms;
}

static uint32_t soft_start_map_res_to_stop_ms(uint32_t res_ohms)
{
    if (res_ohms > (uint32_t)APP_MOTOR_KNOB_RES_OHMS)
        return (uint32_t)APP_MOTOR_STOP_MAX_MS;

    uint32_t stop_ms = (uint32_t)APP_MOTOR_STOP_MIN_MS +
                       res_ohms * (uint32_t)SOFT_START_STOP_SPAN_MS /
                           (uint32_t)APP_MOTOR_KNOB_RES_OHMS;

    assert(stop_ms >= (uint32_t)APP_MOTOR_STOP_MIN_MS);
    assert(stop_ms <= (uint32_t)APP_MOTOR_STOP_MAX_MS);
    return stop_ms;
}
