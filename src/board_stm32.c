/*
 * board_stm32.c：STM32F103 上的时钟、PA8 灯、USART1/2 和可选 TIM2。
 * 引脚按工创赛 26 新板网表，不按丝印图。
 */

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>

#include "app_config.h"
#include "board.h"
#include "stm32f1xx_hal.h"

enum {
    BOARD_UART_TX_TIMEOUT_MS     = 200,
    BOARD_USART1_IRQ_PRIORITY    = 5,
    BOARD_USART1_IRQ_SUBPRIORITY = 0,
    BOARD_MOTOR_TIM_PRESCALER    = 71,
    BOARD_LED_PIN                = GPIO_PIN_8,
    BOARD_UART_TX_PINS           = GPIO_PIN_9 | GPIO_PIN_2,
    BOARD_UART_RX_PINS           = GPIO_PIN_10 | GPIO_PIN_3,
    BOARD_MOTOR_PWM_PINS         = GPIO_PIN_0 | GPIO_PIN_1
};

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

#if APP_ENABLE_MOTOR
static TIM_HandleTypeDef g_motor_timer;
static uint32_t g_motor_start_ms;
#endif

/* 判断 now_ms 是否已到 deadline_ms。先做无符号减，翻转后仍然正确。 */
static bool board_time_reached(uint32_t now_ms, uint32_t deadline_ms);

/* 时钟或串口配失败时停住。继续跑只会得到乱行为。 */
static void board_fail_stop(void);

/* 外部 8 MHz 晶振，PLL 九倍到 72 MHz。APB1 必须是 36 MHz。 */
static void board_configure_clock(void);

/* 网表 led灯.1 接 PA8。先写高再配输出，避免上电闪一下。 */
static void board_configure_led(void);

static void board_led_write(GPIO_PinState level);
static void board_led_on(void);
static void board_led_off(void);

/* PA9/PA10 是 USART1，接 H1 读卡。PA2/PA3 是 USART2，接 U7 播报。
 * SW1 只切播报器电源，不接 MCU，这里不读开关。 */
static void board_configure_uart_gpio(void);

static void board_fill_uart(UART_HandleTypeDef *uart, USART_TypeDef *inst,
                            uint32_t baud);

static void board_configure_rfid_uart(void);
static void board_configure_tts_uart(void);
static void board_arm_rfid_irq(void);
static void board_configure_uarts(void);

static board_status_t board_validate_tx(const uint8_t *data, size_t length);

static board_status_t board_uart_send(UART_HandleTypeDef *uart,
                                      const uint8_t *data,
                                      size_t length);

static uint16_t board_ring_next(uint16_t idx);
static void board_ring_push(uint8_t byte);
static board_status_t board_ring_take(uint8_t *out_byte);

#if APP_ENABLE_MOTOR
static void board_motor_apply(uint16_t compare);
static void board_configure_motor(void);
static void board_process_motor(uint32_t now_ms);
#endif

void board_init(void)
{
    HAL_Init();
    board_configure_clock();
    board_configure_led();
    board_configure_uarts();
#if APP_ENABLE_MOTOR
    board_configure_motor();
#endif
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
    g_led_off_deadline_ms = now_ms + duration_ms;
}

void board_process(uint32_t now_ms)
{
    if (g_led_on != 0U && board_time_reached(now_ms, g_led_off_deadline_ms))
        board_led_off();
#if APP_ENABLE_MOTOR
    board_process_motor(now_ms);
#endif
}

uint32_t board_rfid_rx_overflow_count(void)
{
    return g_rfid_rx_overflows;
}

static bool board_time_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

static void board_fail_stop(void)
{
    __disable_irq();
    for (;;) {
        /* 配时钟或串口失败。停在这里，避免带着坏时钟继续跑。 */
    }
}

static void board_configure_clock(void)
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
    RCC_ClkInitTypeDef clocks = {
        .ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                     RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2,
        .SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK,
        .AHBCLKDivider = RCC_SYSCLK_DIV1,
        .APB1CLKDivider = RCC_HCLK_DIV2,
        .APB2CLKDivider = RCC_HCLK_DIV1
    };

    if (HAL_RCC_OscConfig(&oscillator) != HAL_OK)
        board_fail_stop();
    if (HAL_RCC_ClockConfig(&clocks, FLASH_LATENCY_2) != HAL_OK)
        board_fail_stop();
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
}

static void board_led_write(GPIO_PinState level)
{
    HAL_GPIO_WritePin(GPIOA, BOARD_LED_PIN, level);
}

static void board_led_on(void)
{
    board_led_write(GPIO_PIN_RESET);
    g_led_on = 1U;
}

static void board_led_off(void)
{
    board_led_write(GPIO_PIN_SET);
    g_led_on = 0U;
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

static void board_configure_rfid_uart(void)
{
    board_fill_uart(&g_rfid_uart, USART1, APP_RFID_BAUD_RATE);
    if (HAL_UART_Init(&g_rfid_uart) != HAL_OK)
        board_fail_stop();
}

static void board_configure_tts_uart(void)
{
    board_fill_uart(&g_tts_uart, USART2, APP_TTS_BAUD_RATE);
    if (HAL_UART_Init(&g_tts_uart) != HAL_OK)
        board_fail_stop();
}

static void board_arm_rfid_irq(void)
{
    HAL_NVIC_SetPriority(USART1_IRQn, BOARD_USART1_IRQ_PRIORITY,
                         BOARD_USART1_IRQ_SUBPRIORITY);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
    if (HAL_UART_Receive_IT(&g_rfid_uart, &g_rfid_rx_byte, 1U) != HAL_OK)
        board_fail_stop();
}

static void board_configure_uarts(void)
{
    board_configure_uart_gpio();
    board_configure_rfid_uart();
    board_configure_tts_uart();
    board_arm_rfid_irq();
}

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

static uint16_t board_ring_next(uint16_t idx)
{
    return (uint16_t)((idx + 1U) % BOARD_RFID_RX_RING_SIZE);
}

static void board_ring_push(uint8_t byte)
{
    const uint16_t next_head = board_ring_next(g_rfid_rx_head);

    if (next_head == g_rfid_rx_tail) {
        g_rfid_rx_overflows++;
        return;
    }

    g_rfid_rx_ring[g_rfid_rx_head] = byte;
    g_rfid_rx_head = next_head;
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

#if APP_ENABLE_MOTOR
static void board_motor_apply(uint16_t compare)
{
    if (compare > APP_MOTOR_PWM_PERIOD)
        compare = APP_MOTOR_PWM_PERIOD;

    if (compare == 0U) {
        /* 两路都写成周期值，电机两端没有压差。 */
        __HAL_TIM_SET_COMPARE(&g_motor_timer, TIM_CHANNEL_1,
                              APP_MOTOR_PWM_PERIOD);
        __HAL_TIM_SET_COMPARE(&g_motor_timer, TIM_CHANNEL_2,
                              APP_MOTOR_PWM_PERIOD);
        return;
    }

    __HAL_TIM_SET_COMPARE(&g_motor_timer, TIM_CHANNEL_1, 0U);
    __HAL_TIM_SET_COMPARE(&g_motor_timer, TIM_CHANNEL_2, compare);
}

static void board_configure_motor(void)
{
    GPIO_InitTypeDef gpio = {
        .Pin = BOARD_MOTOR_PWM_PINS,
        .Mode = GPIO_MODE_AF_PP,
        .Pull = GPIO_NOPULL,
        .Speed = GPIO_SPEED_FREQ_LOW
    };
    TIM_OC_InitTypeDef channel = {
        .OCMode = TIM_OCMODE_PWM2,
        .Pulse = 0U,
        .OCPolarity = TIM_OCPOLARITY_HIGH,
        .OCFastMode = TIM_OCFAST_ENABLE
    };

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_TIM2_CLK_ENABLE();
    HAL_GPIO_Init(GPIOA, &gpio);

    g_motor_timer.Instance = TIM2;
    g_motor_timer.Init.Prescaler = BOARD_MOTOR_TIM_PRESCALER;
    g_motor_timer.Init.CounterMode = TIM_COUNTERMODE_UP;
    g_motor_timer.Init.Period = APP_MOTOR_PWM_PERIOD;
    g_motor_timer.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    g_motor_timer.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_PWM_Init(&g_motor_timer) != HAL_OK)
        board_fail_stop();

    if (HAL_TIM_PWM_ConfigChannel(&g_motor_timer, &channel, TIM_CHANNEL_1) !=
            HAL_OK ||
        HAL_TIM_PWM_ConfigChannel(&g_motor_timer, &channel, TIM_CHANNEL_2) !=
            HAL_OK ||
        HAL_TIM_PWM_Start(&g_motor_timer, TIM_CHANNEL_1) != HAL_OK ||
        HAL_TIM_PWM_Start(&g_motor_timer, TIM_CHANNEL_2) != HAL_OK)
        board_fail_stop();

    board_motor_apply(0U);
    g_motor_start_ms = HAL_GetTick();
}

static void board_process_motor(uint32_t now_ms)
{
    const uint32_t elapsed_ms = now_ms - g_motor_start_ms;

    if (elapsed_ms < APP_MOTOR_START_DELAY_MS) {
        board_motor_apply(0U);
        return;
    }

    const uint32_t moving_ms = elapsed_ms - APP_MOTOR_START_DELAY_MS;
    if (moving_ms >= APP_MOTOR_RUN_TIMEOUT_MS) {
        board_motor_apply(0U);
        return;
    }
    if (moving_ms < APP_MOTOR_RAMP_MS) {
        const uint32_t compare =
            (uint32_t)APP_MOTOR_TARGET_COMPARE * moving_ms / APP_MOTOR_RAMP_MS;
        board_motor_apply((uint16_t)compare);
        return;
    }

    board_motor_apply(APP_MOTOR_TARGET_COMPARE);
}
#endif

/*
 * 下面三个名字是启动文件和 HAL 按符号找的，不能改。
 * 改了中断进不来，串口会聋，毫秒会计不了。
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
    if (uart == NULL || uart->Instance != USART1)
        return;

    board_ring_push(g_rfid_rx_byte);
    (void)HAL_UART_Receive_IT(&g_rfid_uart, &g_rfid_rx_byte, 1U);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)
{
    if (uart != NULL && uart->Instance == USART1)
        (void)HAL_UART_Receive_IT(&g_rfid_uart, &g_rfid_rx_byte, 1U);
}
