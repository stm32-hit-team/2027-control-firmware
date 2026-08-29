#ifndef SOFT_START_H
#define SOFT_START_H

#include <stdint.h>

/*
 * 把 12 位 ADC 原始值换成缓启动等待毫秒数。
 * 超过满量程按满量程算。始终得到 [最短, 最长] 之间的等待时间。
 * 规范要求返回模块状态枚举。
 * 计划冻结为返回毫秒数，这里不能改签名。
 */
uint32_t soft_start_delay_ms(uint16_t adc_raw);

#endif
