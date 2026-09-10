#ifndef SOFT_START_H
#define SOFT_START_H

#include <stdint.h>

/*
 * 把 12 位 ADC 原始值换成停车毫秒数。超过满量程按满量程算。
 * 结果落在配置的最短和最长停车时间之间。
 * 规范要求返回模块状态枚举。
 * 计划冻结为返回毫秒数，这里不能改签名。
 */
uint32_t soft_start_stop_ms(uint16_t adc_raw);

#endif
