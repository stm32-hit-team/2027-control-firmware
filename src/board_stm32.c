/*
 * board_stm32.c：STM32F103 上的时钟、PA8 灯、USART1/2、PB1 旋钮和可选 TIM2。
 * 引脚按工创赛 26 新板网表，不按丝印图。
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "app_config.h"
#include "board.h"
#include "soft_start.h"
#include "stm32f1xx_hal.h"

enum {
    BOARD_UART_TX_TIMEOUT_MS     = 200,
    BOARD_USART1_IRQ_PRIORITY    = 5,
    BOARD_USART1_IRQ_SUBPRIORITY = 0,
    BOARD_MOTOR_TIM_PRESCALER    = 71,
    BOARD_LED_PIN                = GPIO_PIN_8,
    BOARD_UART_TX_PINS           = GPIO_PIN_9 | GPIO_PIN_2,
    BOARD_UART_RX_PINS           = GPIO_PIN_10 | GPIO_PIN_3,
    BOARD_MOTOR_PWM_PINS         = GPIO_PIN_0 | GPIO_PIN_1,
    BOARD_SOFT_START_PIN         = GPIO_PIN_1,
    BOARD_ADC_POLL_TIMEOUT_MS    = 10
};

/* 把非 OK 状态交回给调用方。只许用在没有获取资源的函数里。
 * 这是本模块唯一允许包含 return 的宏。 */
#define BOARD_TRY(expr)                         \
    do {                                        \
        board_status_t board_try_s_ = (expr);   \
        if (board_try_s_ != BOARD_OK)           \
            return board_try_s_;                \
    } while (0)

static UART_HandleTypeDef g_rfid_uart;
static UART_HandleTypeDef g_tts_uart;

static uint8_t g_rfid_rx_byte;
static volatile uint8_t g_rfid_rx_ring[BOARD_RFID_RX_RING_SIZE];
static volatile uint16_t g_rfid_rx_head;
static volatile uint16_t g_rfid_rx_tail;
static volatile uint32_t g_rfid_rx_overflows;

static uint8_t g_led_on;
static uint32_t g_led_off_deadline_ms;
static uint32_t g_soft_start_delay_ms = APP_SOFT_START_MIN_MS;

#if APP_ENABLE_MOTOR
static TIM_HandleTypeDef g_motor_timer;
static uint32_t g_motor_start_ms;
#endif

/* 外部 8 MHz 晶振，PLL 九倍到 72 MHz。 */
static void board_configure_clock(void);

/* 打开 HSE 和 PLL。失败则停住。 */
static void board_configure_oscillator(void);

/* HAL 失败时关中断后停住。继续跑只会得到乱行为。 */
static void board_fail_stop(void);

/* 适配 HAL 返回值：不是 HAL_OK 就停住。 */
static void board_require_hal_ok(HAL_StatusTypeDef status);

/* APB1 必须是 36 MHz。失败则停住。 */
static void board_configure_sysclk(void);

/* 网表 led灯1.1 接 PA8。先写高再配输出，避免上电闪一下。 */
static void board_configure_led(void);

/* 适配 HAL_GPIO_WritePin，只写 PA8。 */
static void board_led_write(GPIO_PinState level);

/* PA9/PA10 是 USART1，接 H1 读卡。PA2/PA3 是 USART2，接 U7 播报。
 * SW1 只切播报器电源，不接 MCU，这里不读开关。 */
static void board_configure_uarts(void);

/* 给 USART1/2 配好 GPIOA 上的 TX/RX。 */
static void board_configure_uart_gpio(void);

/* 按读卡波特率初始化 USART1。失败则停住。 */
static void board_configure_rfid_uart(void);

/* 填好 UART 句柄。uart、inst 不能为空。 */
static void board_fill_uart(UART_HandleTypeDef *uart, USART_TypeDef *inst,
                            uint32_t baud);

/* 按语音波特率初始化 USART2。失败则停住。 */
static void board_configure_tts_uart(void);

/* 让 USART1 开始收字节。失败则停住。 */
static void board_arm_rfid_irq(void);

/* 打开 USART1 的 NVIC。 */
static void board_enable_rfid_irq(void);

/* 适配 HAL_UART_Receive_IT，向 USART1 再要 1 字节。失败返回 BOARD_ERR_IO。 */
static board_status_t board_rfid_request_rx(void);

/* 上电读 PB1 旋钮，写入 g_soft_start_delay_ms。ADC 失败用最短等待。 */
static void board_read_soft_start_knob(void);

/* 写入缓启动等待时间。delay_ms 必须落在配置的最短和最长之间。 */
static void board_store_soft_start_delay_ms(uint32_t delay_ms);

/* 配好 PB1 旋钮用的 ADC1。adc 不能为空。
 * ADC 或校准失败返回 BOARD_ERR_IO，不要停死。 */
static board_status_t board_configure_soft_start_adc(ADC_HandleTypeDef *adc);

/* 打开 GPIOA、GPIOB、ADC1 时钟。GPIOA 给灯用。 */
static void board_enable_soft_start_clocks(void);

/* 把 ADC 时钟设为 PCLK2 / 6。失败返回 BOARD_ERR_IO。 */
static board_status_t board_configure_soft_start_adc_clock(void);

/* 把 PB1 配成模拟输入。 */
static void board_configure_soft_start_pin(void);

/* 填好 ADC1 软件触发。adc 不能为空。 */
static void board_fill_soft_start_adc(ADC_HandleTypeDef *adc);

/* 适配 HAL_ADC_Init。adc 不能为空。失败返回 BOARD_ERR_IO。 */
static board_status_t board_adc_init(ADC_HandleTypeDef *adc);

/* 适配 HAL_ADC_ConfigChannel，接到通道 9。adc 不能为空。 */
static board_status_t board_adc_apply_soft_start_channel(ADC_HandleTypeDef *adc);

/* 适配 HAL_ADCEx_Calibration_Start。adc 不能为空。 */
static board_status_t board_adc_calibrate(ADC_HandleTypeDef *adc);

/* 连续采 APP_ADC_SAMPLE_COUNT 次，写入平均值。adc、out_average 不能为空。
 * 任一次失败返回 BOARD_ERR_IO。 */
static board_status_t board_sample_soft_start_adc(ADC_HandleTypeDef *adc,
                                                  uint16_t *out_average);

/* 软件触发一次转换，写入 *out_raw。adc、out_raw 不能为空。
 * 启动或等待失败返回 BOARD_ERR_IO。本函数会占用 ADC，不能用 BOARD_TRY。 */
static board_status_t board_adc_convert_once(ADC_HandleTypeDef *adc,
                                             uint32_t *out_raw);

/* 适配 HAL_ADC_Start。adc 不能为空。失败返回 BOARD_ERR_IO。 */
static board_status_t board_adc_start(ADC_HandleTypeDef *adc);

/* 适配 HAL_ADC_PollForConversion。adc 不能为空。失败返回 BOARD_ERR_IO。 */
static board_status_t board_adc_poll(ADC_HandleTypeDef *adc);

/* 适配 HAL_ADC_GetValue。adc 不能为空。 */
static uint32_t board_adc_read_value(ADC_HandleTypeDef *adc);

/* 适配 HAL_ADC_Stop。adc 不能为空。返回值忽略，与原先一致。 */
static void board_adc_stop(ADC_HandleTypeDef *adc);

/* 适配 soft_start_delay_ms。 */
static uint32_t board_soft_start_from_raw(uint16_t adc_raw);

#if APP_ENABLE_MOTOR
/* 配 TIM2 两路 PWM，上电先 coast。失败则停住。 */
static void board_configure_motor(void);

/* 打开 GPIOA、TIM2 时钟，并把 PA0/PA1 配成复用。 */
static void board_configure_motor_gpio(void);

/* 填好 TIM2 向上计数和 PWM 周期。 */
static void board_fill_motor_timer(void);

/* 启动 TIM2 两路 PWM。失败则停住。 */
static void board_start_motor_pwm_channels(void);

/* 两路比较值写成 coast 或前进。compare 超过周期时按周期算。 */
static void board_motor_apply(uint16_t compare);

/* 把比较值限制在 PWM 周期以内。纯函数。 */
static uint16_t board_motor_clamped_compare(uint16_t compare);

/* 同时写下 TIM2 通道 1 和通道 2 的比较值。 */
static void board_motor_write_compares(uint16_t compare_ch1,
                                       uint16_t compare_ch2);
#endif

/* 空指针、长度为 0、或超过 16 位长度返回 BOARD_ERR_ARG。 */
static board_status_t board_validate_tx(const uint8_t *data, size_t length);

/* 适配 HAL_UART_Transmit。uart、data 不能为空。超时返回 BOARD_ERR_IO。 */
static board_status_t board_uart_send(UART_HandleTypeDef *uart,
                                      const uint8_t *data,
                                      size_t length);

/* 从环形缓冲取出一字节。缓冲空返回 BOARD_ERR_EMPTY。out_byte 不能为空。 */
static board_status_t board_ring_take(uint8_t *out_byte);

/* 环形下标加一，绕回 0。idx 必须小于缓冲长度。 */
static uint16_t board_ring_next(uint16_t idx);

/* 点亮 PA8。 */
static void board_led_on(void);

/* 记下 now_ms + duration_ms 时该熄灯。 */
static void board_set_led_off_deadline(uint32_t now_ms, uint32_t duration_ms);

/* 判断 PA8 是否处于点亮状态。 */
static bool board_led_is_lit(void);

/* 判断熄灯截止时刻是否已到。 */
static bool board_led_off_is_due(uint32_t now_ms);

/* 判断 now_ms 是否已到 deadline_ms。先做无符号减，翻转后仍然正确。 */
static bool board_time_reached(uint32_t now_ms, uint32_t deadline_ms);

/* 熄灭 PA8。 */
static void board_led_off(void);

#if APP_ENABLE_MOTOR
/* 按缓启动等待、爬升、巡航和超时改比较值。 */
static void board_process_motor(uint32_t now_ms);

/* 过了缓启动等待后，按爬升、巡航或超时给出比较值。 */
static uint16_t board_motor_compare_after_wait(uint32_t moving_ms);
#endif

/* 把一字节写入环形缓冲。满则丢掉并计数。 */
static void board_ring_push(uint8_t byte);

/* 判断这个句柄是不是 USART1 读卡口。 */
static bool board_is_rfid_uart(const UART_HandleTypeDef *uart);

/* 规范要求返回状态枚举。
 * 启动失败只能停住，不能把坏时钟交回给调用方。 */
void board_init(void)
{
    HAL_Init();
    board_configure_clock();
    board_configure_led();
    board_configure_uarts();
    board_read_soft_start_knob();
#if APP_ENABLE_MOTOR
    board_configure_motor();
#endif
}

/* 规范要求返回状态枚举。计划规定返回毫秒数，这里不能改签名。 */
uint32_t board_soft_start_delay_ms(void)
{
    assert(g_soft_start_delay_ms >= (uint32_t)APP_SOFT_START_MIN_MS);
    assert(g_soft_start_delay_ms <= (uint32_t)APP_SOFT_START_MAX_MS);
    return g_soft_start_delay_ms;
}

uint32_t board_millis(void)
{
    return HAL_GetTick();
}

void board_idle(void)
{
    __WFI();
}

board_status_t board_rfid_write(const uint8_t *data, size_t length)
{
    BOARD_TRY(board_validate_tx(data, length));
    return board_uart_send(&g_rfid_uart, data, length);
}

board_status_t board_rfid_read_byte(uint8_t *out_byte)
{
    if (out_byte == NULL)
        return BOARD_ERR_ARG;
    return board_ring_take(out_byte);
}

board_status_t board_tts_write(const uint8_t *data, size_t length)
{
    BOARD_TRY(board_validate_tx(data, length));
    return board_uart_send(&g_tts_uart, data, length);
}

void board_mark_tag(uint32_t now_ms, uint32_t duration_ms)
{
    board_led_on();
    board_set_led_off_deadline(now_ms, duration_ms);
}

void board_process(uint32_t now_ms)
{
    if (board_led_is_lit() && board_led_off_is_due(now_ms))
        board_led_off();
#if APP_ENABLE_MOTOR
    board_process_motor(now_ms);
#endif
}

uint32_t board_rfid_rx_overflow_count(void)
{
    return g_rfid_rx_overflows;
}

static void board_configure_clock(void)
{
    board_configure_oscillator();
    board_configure_sysclk();
}

static void board_configure_oscillator(void)
{
    RCC_OscInitTypeDef oscillator = {
        .OscillatorType = RCC_OSCILLATORTYPE_HSE,
        .HSEState = RCC_HSE_ON,
        .HSEPredivValue = RCC_HSE_PREDIV_DIV1,
        .HSIState = RCC_HSI_ON,
        .PLL = {
            .PLLState = RCC_PLL_ON,
            .PLLSource = RCC_PLLSOURCE_HSE,
            .PLLMUL = RCC_PLL_MUL9
        }
    };

    board_require_hal_ok(HAL_RCC_OscConfig(&oscillator));
}

static void board_fail_stop(void)
{
    __disable_irq();
    for (;;) {
        /* 配时钟或串口失败。停在这里，避免带着坏时钟继续跑。 */
    }
}

static void board_require_hal_ok(HAL_StatusTypeDef status)
{
    if (status != HAL_OK)
        board_fail_stop();
}

static void board_configure_sysclk(void)
{
    RCC_ClkInitTypeDef clocks = {
        .ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                     RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2,
        .SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK,
        .AHBCLKDivider = RCC_SYSCLK_DIV1,
        .APB1CLKDivider = RCC_HCLK_DIV2,
        .APB2CLKDivider = RCC_HCLK_DIV1
    };

    board_require_hal_ok(HAL_RCC_ClockConfig(&clocks, FLASH_LATENCY_2));
}

static void board_configure_led(void)
{
    GPIO_InitTypeDef gpio = {
        .Pin = BOARD_LED_PIN,
        .Mode = GPIO_MODE_OUTPUT_PP,
        .Pull = GPIO_PULLUP,
        .Speed = GPIO_SPEED_FREQ_LOW
    };

    __HAL_RCC_GPIOA_CLK_ENABLE();
    board_led_write(GPIO_PIN_SET);
    HAL_GPIO_Init(GPIOA, &gpio);
    g_led_on = 0U;
    assert(g_led_on == 0U);
}

static void board_led_write(GPIO_PinState level)
{
    HAL_GPIO_WritePin(GPIOA, BOARD_LED_PIN, level);
}

static void board_configure_uarts(void)
{
    board_configure_uart_gpio();
    board_configure_rfid_uart();
    board_configure_tts_uart();
    board_arm_rfid_irq();
}

static void board_configure_uart_gpio(void)
{
    GPIO_InitTypeDef tx = {
        .Pin = BOARD_UART_TX_PINS,
        .Mode = GPIO_MODE_AF_PP,
        .Pull = GPIO_NOPULL,
        .Speed = GPIO_SPEED_FREQ_HIGH
    };
    GPIO_InitTypeDef rx = {
        .Pin = BOARD_UART_RX_PINS,
        .Mode = GPIO_MODE_INPUT,
        .Pull = GPIO_NOPULL,
        .Speed = GPIO_SPEED_FREQ_HIGH
    };

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_USART2_CLK_ENABLE();
    HAL_GPIO_Init(GPIOA, &tx);
    HAL_GPIO_Init(GPIOA, &rx);
}

static void board_configure_rfid_uart(void)
{
    board_fill_uart(&g_rfid_uart, USART1, APP_RFID_BAUD_RATE);
    board_require_hal_ok(HAL_UART_Init(&g_rfid_uart));
}

static void board_fill_uart(UART_HandleTypeDef *uart, USART_TypeDef *inst,
                            uint32_t baud)
{
    assert(uart != NULL);
    assert(inst != NULL);

    uart->Instance = inst;
    uart->Init.BaudRate = baud;
    uart->Init.WordLength = UART_WORDLENGTH_8B;
    uart->Init.StopBits = UART_STOPBITS_1;
    uart->Init.Parity = UART_PARITY_NONE;
    uart->Init.Mode = UART_MODE_TX_RX;
    uart->Init.HwFlowCtl = UART_HWCONTROL_NONE;
    uart->Init.OverSampling = UART_OVERSAMPLING_16;
}

static void board_configure_tts_uart(void)
{
    board_fill_uart(&g_tts_uart, USART2, APP_TTS_BAUD_RATE);
    board_require_hal_ok(HAL_UART_Init(&g_tts_uart));
}

static void board_arm_rfid_irq(void)
{
    board_enable_rfid_irq();
    if (board_rfid_request_rx() != BOARD_OK)
        board_fail_stop();
}

static void board_enable_rfid_irq(void)
{
    HAL_NVIC_SetPriority(USART1_IRQn, BOARD_USART1_IRQ_PRIORITY,
                         BOARD_USART1_IRQ_SUBPRIORITY);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
}

static board_status_t board_rfid_request_rx(void)
{
    if (HAL_UART_Receive_IT(&g_rfid_uart, &g_rfid_rx_byte, 1U) != HAL_OK)
        return BOARD_ERR_IO;
    return BOARD_OK;
}

static void board_read_soft_start_knob(void)
{
    ADC_HandleTypeDef adc = {0};

    board_store_soft_start_delay_ms((uint32_t)APP_SOFT_START_MIN_MS);
    if (board_configure_soft_start_adc(&adc) != BOARD_OK)
        return;

    uint16_t average_raw = 0;

    if (board_sample_soft_start_adc(&adc, &average_raw) != BOARD_OK)
        return;

    board_store_soft_start_delay_ms(board_soft_start_from_raw(average_raw));
}

static void board_store_soft_start_delay_ms(uint32_t delay_ms)
{
    assert(delay_ms >= (uint32_t)APP_SOFT_START_MIN_MS);
    assert(delay_ms <= (uint32_t)APP_SOFT_START_MAX_MS);
    g_soft_start_delay_ms = delay_ms;
    assert(g_soft_start_delay_ms == delay_ms);
}

static board_status_t board_configure_soft_start_adc(ADC_HandleTypeDef *adc)
{
    assert(adc != NULL);

    board_enable_soft_start_clocks();
    if (board_configure_soft_start_adc_clock() != BOARD_OK)
        return BOARD_ERR_IO;

    board_configure_soft_start_pin();
    board_fill_soft_start_adc(adc);
    if (board_adc_init(adc) != BOARD_OK)
        return BOARD_ERR_IO;
    if (board_adc_apply_soft_start_channel(adc) != BOARD_OK)
        return BOARD_ERR_IO;
    if (board_adc_calibrate(adc) != BOARD_OK)
        return BOARD_ERR_IO;
    return BOARD_OK;
}

static void board_enable_soft_start_clocks(void)
{
    /* PB1 在 GPIOB。计划写了 GPIOA，按网表开 GPIOB。GPIOA 仍打开，给灯用。 */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_ADC1_CLK_ENABLE();
}

static board_status_t board_configure_soft_start_adc_clock(void)
{
    RCC_PeriphCLKInitTypeDef adc_clock = {
        .PeriphClockSelection = RCC_PERIPHCLK_ADC,
        .AdcClockSelection = RCC_ADCPCLK2_DIV6
    };

    if (HAL_RCCEx_PeriphCLKConfig(&adc_clock) != HAL_OK)
        return BOARD_ERR_IO;
    return BOARD_OK;
}

static void board_configure_soft_start_pin(void)
{
    GPIO_InitTypeDef gpio = {
        .Pin = BOARD_SOFT_START_PIN,
        .Mode = GPIO_MODE_ANALOG,
        .Pull = GPIO_NOPULL
    };

    HAL_GPIO_Init(GPIOB, &gpio);
}

static void board_fill_soft_start_adc(ADC_HandleTypeDef *adc)
{
    assert(adc != NULL);

    adc->Instance = ADC1;
    adc->Init.DataAlign = ADC_DATAALIGN_RIGHT;
    adc->Init.ScanConvMode = ADC_SCAN_DISABLE;
    adc->Init.ContinuousConvMode = DISABLE;
    adc->Init.NbrOfConversion = 1U;
    adc->Init.DiscontinuousConvMode = DISABLE;
    adc->Init.NbrOfDiscConversion = 1U;
    adc->Init.ExternalTrigConv = ADC_SOFTWARE_START;

    assert(adc->Instance == ADC1);
    assert(adc->Init.NbrOfConversion == 1U);
}

static board_status_t board_adc_init(ADC_HandleTypeDef *adc)
{
    assert(adc != NULL);
    if (HAL_ADC_Init(adc) != HAL_OK)
        return BOARD_ERR_IO;
    return BOARD_OK;
}

static board_status_t board_adc_apply_soft_start_channel(ADC_HandleTypeDef *adc)
{
    ADC_ChannelConfTypeDef channel = {
        .Channel = ADC_CHANNEL_9,
        .Rank = ADC_REGULAR_RANK_1,
        .SamplingTime = ADC_SAMPLETIME_239CYCLES_5
    };

    assert(adc != NULL);
    if (HAL_ADC_ConfigChannel(adc, &channel) != HAL_OK)
        return BOARD_ERR_IO;
    return BOARD_OK;
}

static board_status_t board_adc_calibrate(ADC_HandleTypeDef *adc)
{
    assert(adc != NULL);
    if (HAL_ADCEx_Calibration_Start(adc) != HAL_OK)
        return BOARD_ERR_IO;
    return BOARD_OK;
}

static board_status_t board_sample_soft_start_adc(ADC_HandleTypeDef *adc,
                                                  uint16_t *out_average)
{
    uint32_t sum = 0;

    assert(adc != NULL);
    assert(out_average != NULL);

    for (uint32_t i = 0; i < (uint32_t)APP_ADC_SAMPLE_COUNT; ++i) {
        uint32_t sample = 0;

        BOARD_TRY(board_adc_convert_once(adc, &sample));
        sum += sample;
    }

    *out_average = (uint16_t)(sum / (uint32_t)APP_ADC_SAMPLE_COUNT);
    return BOARD_OK;
}

static board_status_t board_adc_convert_once(ADC_HandleTypeDef *adc,
                                             uint32_t *out_raw)
{
    assert(adc != NULL);
    assert(out_raw != NULL);

    if (board_adc_start(adc) != BOARD_OK)
        return BOARD_ERR_IO;

    board_status_t poll_status = board_adc_poll(adc);
    if (poll_status != BOARD_OK) {
        board_adc_stop(adc);
        return poll_status;
    }

    *out_raw = board_adc_read_value(adc);
    board_adc_stop(adc);
    return BOARD_OK;
}

static board_status_t board_adc_start(ADC_HandleTypeDef *adc)
{
    assert(adc != NULL);
    if (HAL_ADC_Start(adc) != HAL_OK)
        return BOARD_ERR_IO;
    return BOARD_OK;
}

static board_status_t board_adc_poll(ADC_HandleTypeDef *adc)
{
    assert(adc != NULL);
    if (HAL_ADC_PollForConversion(adc, BOARD_ADC_POLL_TIMEOUT_MS) != HAL_OK)
        return BOARD_ERR_IO;
    return BOARD_OK;
}

static uint32_t board_adc_read_value(ADC_HandleTypeDef *adc)
{
    assert(adc != NULL);
    return HAL_ADC_GetValue(adc);
}

static void board_adc_stop(ADC_HandleTypeDef *adc)
{
    assert(adc != NULL);
    (void)HAL_ADC_Stop(adc);
}

static uint32_t board_soft_start_from_raw(uint16_t adc_raw)
{
    return soft_start_delay_ms(adc_raw);
}

#if APP_ENABLE_MOTOR
static void board_configure_motor(void)
{
    board_configure_motor_gpio();
    board_fill_motor_timer();
    board_require_hal_ok(HAL_TIM_PWM_Init(&g_motor_timer));
    board_start_motor_pwm_channels();
    board_motor_apply(0U);
    g_motor_start_ms = board_millis();
}

static void board_configure_motor_gpio(void)
{
    GPIO_InitTypeDef gpio = {
        .Pin = BOARD_MOTOR_PWM_PINS,
        .Mode = GPIO_MODE_AF_PP,
        .Pull = GPIO_NOPULL,
        .Speed = GPIO_SPEED_FREQ_LOW
    };

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_TIM2_CLK_ENABLE();
    HAL_GPIO_Init(GPIOA, &gpio);
}

static void board_fill_motor_timer(void)
{
    g_motor_timer.Instance = TIM2;
    g_motor_timer.Init.Prescaler = BOARD_MOTOR_TIM_PRESCALER;
    g_motor_timer.Init.CounterMode = TIM_COUNTERMODE_UP;
    g_motor_timer.Init.Period = APP_MOTOR_PWM_PERIOD;
    g_motor_timer.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    g_motor_timer.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
}

static void board_start_motor_pwm_channels(void)
{
    TIM_OC_InitTypeDef channel = {
        .OCMode = TIM_OCMODE_PWM2,
        .Pulse = 0U,
        .OCPolarity = TIM_OCPOLARITY_HIGH,
        .OCFastMode = TIM_OCFAST_ENABLE
    };

    board_require_hal_ok(HAL_TIM_PWM_ConfigChannel(&g_motor_timer, &channel,
                                                   TIM_CHANNEL_1));
    board_require_hal_ok(HAL_TIM_PWM_ConfigChannel(&g_motor_timer, &channel,
                                                   TIM_CHANNEL_2));
    board_require_hal_ok(HAL_TIM_PWM_Start(&g_motor_timer, TIM_CHANNEL_1));
    board_require_hal_ok(HAL_TIM_PWM_Start(&g_motor_timer, TIM_CHANNEL_2));
}

static void board_motor_apply(uint16_t compare)
{
    uint16_t clamped = board_motor_clamped_compare(compare);

    if (clamped == 0U) {
        /* 两路都写成周期值，电机两端没有压差。 */
        board_motor_write_compares((uint16_t)APP_MOTOR_PWM_PERIOD,
                                   (uint16_t)APP_MOTOR_PWM_PERIOD);
        return;
    }

    board_motor_write_compares(0U, clamped);
}

static uint16_t board_motor_clamped_compare(uint16_t compare)
{
    if (compare > (uint16_t)APP_MOTOR_PWM_PERIOD)
        return (uint16_t)APP_MOTOR_PWM_PERIOD;
    return compare;
}

static void board_motor_write_compares(uint16_t compare_ch1,
                                       uint16_t compare_ch2)
{
    __HAL_TIM_SET_COMPARE(&g_motor_timer, TIM_CHANNEL_1, compare_ch1);
    __HAL_TIM_SET_COMPARE(&g_motor_timer, TIM_CHANNEL_2, compare_ch2);
}
#endif

static board_status_t board_validate_tx(const uint8_t *data, size_t length)
{
    if (data == NULL)
        return BOARD_ERR_ARG;
    if (length == 0U)
        return BOARD_ERR_ARG;
    if (length > UINT16_MAX)
        return BOARD_ERR_ARG;
    return BOARD_OK;
}

static board_status_t board_uart_send(UART_HandleTypeDef *uart,
                                      const uint8_t *data,
                                      size_t length)
{
    assert(uart != NULL);
    assert(data != NULL);

    if (HAL_UART_Transmit(uart, (uint8_t *)data, (uint16_t)length,
                          BOARD_UART_TX_TIMEOUT_MS) != HAL_OK)
        return BOARD_ERR_IO;
    return BOARD_OK;
}

static board_status_t board_ring_take(uint8_t *out_byte)
{
    assert(out_byte != NULL);

    if (g_rfid_rx_tail == g_rfid_rx_head)
        return BOARD_ERR_EMPTY;

    *out_byte = g_rfid_rx_ring[g_rfid_rx_tail];
    g_rfid_rx_tail = board_ring_next(g_rfid_rx_tail);
    return BOARD_OK;
}

static uint16_t board_ring_next(uint16_t idx)
{
    assert(idx < (uint16_t)BOARD_RFID_RX_RING_SIZE);
    return (uint16_t)((idx + 1U) % BOARD_RFID_RX_RING_SIZE);
}

static void board_led_on(void)
{
    board_led_write(GPIO_PIN_RESET);
    g_led_on = 1U;
    assert(g_led_on == 1U);
}

static void board_set_led_off_deadline(uint32_t now_ms, uint32_t duration_ms)
{
    g_led_off_deadline_ms = now_ms + duration_ms;
}

static bool board_led_is_lit(void)
{
    return g_led_on != 0U;
}

static bool board_led_off_is_due(uint32_t now_ms)
{
    return board_time_reached(now_ms, g_led_off_deadline_ms);
}

static bool board_time_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

static void board_led_off(void)
{
    board_led_write(GPIO_PIN_SET);
    g_led_on = 0U;
    assert(g_led_on == 0U);
}

#if APP_ENABLE_MOTOR
static void board_process_motor(uint32_t now_ms)
{
    const uint32_t elapsed_ms = now_ms - g_motor_start_ms;
    const uint32_t wait_ms = board_soft_start_delay_ms();

    if (elapsed_ms < wait_ms) {
        board_motor_apply(0U);
        return;
    }

    board_motor_apply(board_motor_compare_after_wait(elapsed_ms - wait_ms));
}

static uint16_t board_motor_compare_after_wait(uint32_t moving_ms)
{
    if (moving_ms >= APP_MOTOR_RUN_TIMEOUT_MS)
        return 0;
    if (moving_ms >= APP_MOTOR_RAMP_MS)
        return (uint16_t)APP_MOTOR_TARGET_COMPARE;

    assert(APP_MOTOR_RAMP_MS != 0);

    uint32_t compare =
        (uint32_t)APP_MOTOR_TARGET_COMPARE * moving_ms / APP_MOTOR_RAMP_MS;

    return (uint16_t)compare;
}
#endif

static void board_ring_push(uint8_t byte)
{
    const uint16_t next_head = board_ring_next(g_rfid_rx_head);

    assert(g_rfid_rx_head < (uint16_t)BOARD_RFID_RX_RING_SIZE);

    if (next_head == g_rfid_rx_tail) {
        g_rfid_rx_overflows++;
        return;
    }

    g_rfid_rx_ring[g_rfid_rx_head] = byte;
    g_rfid_rx_head = next_head;
}

static bool board_is_rfid_uart(const UART_HandleTypeDef *uart)
{
    return uart != NULL && uart->Instance == USART1;
}

/*
 * 下面三个名字是启动文件和 HAL 按符号找的，不能改。
 * 改了中断进不来，串口会聋，毫秒会计不了。
 * 规范要求模块前缀。这些符号由启动文件和 HAL 固定，不能加 board_ 前缀。
 */
void USART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(&g_rfid_uart);
}

void SysTick_Handler(void)
{
    HAL_IncTick();
    HAL_SYSTICK_IRQHandler();
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart)
{
    if (!board_is_rfid_uart(uart))
        return;

    board_ring_push(g_rfid_rx_byte);
    (void)board_rfid_request_rx();
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)
{
    if (board_is_rfid_uart(uart))
        (void)board_rfid_request_rx();
}
