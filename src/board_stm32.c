#include "board.h"

#include "app_config.h"
#include "stm32f1xx_hal.h"

#include <limits.h>
#include <string.h>

#define RFID_RX_RING_SIZE 128U
#define UART_TX_TIMEOUT_MS 200U

static UART_HandleTypeDef g_rfid_uart;
static UART_HandleTypeDef g_tts_uart;

static uint8_t g_rfid_rx_byte;
static volatile uint8_t g_rfid_rx_ring[RFID_RX_RING_SIZE];
static volatile uint16_t g_rfid_rx_head;
static volatile uint16_t g_rfid_rx_tail;
static volatile uint32_t g_rfid_rx_overflows;

static uint8_t g_led_on;
static uint32_t g_led_off_deadline_ms;

#if APP_ENABLE_MOTOR
static TIM_HandleTypeDef g_motor_timer;
static uint32_t g_motor_start_ms;
#endif

static bool time_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

static void fail_stop(void)
{
    __disable_irq();
    for (;;) {
    }
}

static void configure_clock(void)
{
    RCC_OscInitTypeDef oscillator = {0};
    RCC_ClkInitTypeDef clocks = {0};

    oscillator.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    oscillator.HSEState = RCC_HSE_ON;
    oscillator.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    oscillator.HSIState = RCC_HSI_ON;
    oscillator.PLL.PLLState = RCC_PLL_ON;
    oscillator.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    oscillator.PLL.PLLMUL = RCC_PLL_MUL9;
    if (HAL_RCC_OscConfig(&oscillator) != HAL_OK) {
        fail_stop();
    }

    clocks.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                       RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clocks.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clocks.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clocks.APB1CLKDivider = RCC_HCLK_DIV2;
    clocks.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clocks, FLASH_LATENCY_2) != HAL_OK) {
        fail_stop();
    }
}

static void configure_led(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
    gpio.Pin = GPIO_PIN_13;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &gpio);
}

static void configure_uarts(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_USART2_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_9 | GPIO_PIN_2;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);

    gpio.Pin = GPIO_PIN_10 | GPIO_PIN_3;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);

    g_rfid_uart.Instance = USART1;
    g_rfid_uart.Init.BaudRate = APP_RFID_BAUD_RATE;
    g_rfid_uart.Init.WordLength = UART_WORDLENGTH_8B;
    g_rfid_uart.Init.StopBits = UART_STOPBITS_1;
    g_rfid_uart.Init.Parity = UART_PARITY_NONE;
    g_rfid_uart.Init.Mode = UART_MODE_TX_RX;
    g_rfid_uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    g_rfid_uart.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&g_rfid_uart) != HAL_OK) {
        fail_stop();
    }

    g_tts_uart.Instance = USART2;
    g_tts_uart.Init.BaudRate = APP_TTS_BAUD_RATE;
    g_tts_uart.Init.WordLength = UART_WORDLENGTH_8B;
    g_tts_uart.Init.StopBits = UART_STOPBITS_1;
    g_tts_uart.Init.Parity = UART_PARITY_NONE;
    g_tts_uart.Init.Mode = UART_MODE_TX_RX;
    g_tts_uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    g_tts_uart.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&g_tts_uart) != HAL_OK) {
        fail_stop();
    }

    HAL_NVIC_SetPriority(USART1_IRQn, 5U, 0U);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
    if (HAL_UART_Receive_IT(&g_rfid_uart, &g_rfid_rx_byte, 1U) != HAL_OK) {
        fail_stop();
    }
}

#if APP_ENABLE_MOTOR
static void motor_apply(uint16_t compare)
{
    if (compare > APP_MOTOR_PWM_PERIOD) {
        compare = APP_MOTOR_PWM_PERIOD;
    }
    if (compare == 0U) {
        __HAL_TIM_SET_COMPARE(&g_motor_timer, TIM_CHANNEL_1,
                              APP_MOTOR_PWM_PERIOD);
        __HAL_TIM_SET_COMPARE(&g_motor_timer, TIM_CHANNEL_2,
                              APP_MOTOR_PWM_PERIOD);
    } else {
        __HAL_TIM_SET_COMPARE(&g_motor_timer, TIM_CHANNEL_1, 0U);
        __HAL_TIM_SET_COMPARE(&g_motor_timer, TIM_CHANNEL_2, compare);
    }
}

static void configure_motor(void)
{
    GPIO_InitTypeDef gpio = {0};
    TIM_OC_InitTypeDef channel = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_TIM2_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &gpio);

    g_motor_timer.Instance = TIM2;
    g_motor_timer.Init.Prescaler = 71U;
    g_motor_timer.Init.CounterMode = TIM_COUNTERMODE_UP;
    g_motor_timer.Init.Period = APP_MOTOR_PWM_PERIOD;
    g_motor_timer.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    g_motor_timer.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_PWM_Init(&g_motor_timer) != HAL_OK) {
        fail_stop();
    }

    channel.OCMode = TIM_OCMODE_PWM2;
    channel.Pulse = 0U;
    channel.OCPolarity = TIM_OCPOLARITY_HIGH;
    channel.OCFastMode = TIM_OCFAST_ENABLE;
    if (HAL_TIM_PWM_ConfigChannel(&g_motor_timer, &channel, TIM_CHANNEL_1) !=
            HAL_OK ||
        HAL_TIM_PWM_ConfigChannel(&g_motor_timer, &channel, TIM_CHANNEL_2) !=
            HAL_OK ||
        HAL_TIM_PWM_Start(&g_motor_timer, TIM_CHANNEL_1) != HAL_OK ||
        HAL_TIM_PWM_Start(&g_motor_timer, TIM_CHANNEL_2) != HAL_OK) {
        fail_stop();
    }
    motor_apply(0U);
    g_motor_start_ms = HAL_GetTick();
}

static void process_motor(uint32_t now_ms)
{
    const uint32_t elapsed_ms = now_ms - g_motor_start_ms;
    if (elapsed_ms < APP_MOTOR_START_DELAY_MS) {
        motor_apply(0U);
        return;
    }

    const uint32_t moving_ms = elapsed_ms - APP_MOTOR_START_DELAY_MS;
    if (moving_ms >= APP_MOTOR_RUN_TIMEOUT_MS) {
        motor_apply(0U);
    } else if (moving_ms < APP_MOTOR_RAMP_MS) {
        const uint32_t compare =
            (uint32_t)APP_MOTOR_TARGET_COMPARE * moving_ms / APP_MOTOR_RAMP_MS;
        motor_apply((uint16_t)compare);
    } else {
        motor_apply(APP_MOTOR_TARGET_COMPARE);
    }
}
#endif

void board_init(void)
{
    HAL_Init();
    configure_clock();
    configure_led();
    configure_uarts();
#if APP_ENABLE_MOTOR
    configure_motor();
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

bool board_rfid_write(const uint8_t *data, size_t length)
{
    if (data == NULL || length == 0U || length > UINT16_MAX) {
        return false;
    }
    return HAL_UART_Transmit(&g_rfid_uart, (uint8_t *)data, (uint16_t)length,
                             UART_TX_TIMEOUT_MS) == HAL_OK;
}

bool board_rfid_read_byte(uint8_t *byte)
{
    if (byte == NULL || g_rfid_rx_tail == g_rfid_rx_head) {
        return false;
    }

    *byte = g_rfid_rx_ring[g_rfid_rx_tail];
    g_rfid_rx_tail = (uint16_t)((g_rfid_rx_tail + 1U) % RFID_RX_RING_SIZE);
    return true;
}

bool board_tts_write(const uint8_t *data, size_t length)
{
    if (data == NULL || length == 0U || length > UINT16_MAX) {
        return false;
    }
    return HAL_UART_Transmit(&g_tts_uart, (uint8_t *)data, (uint16_t)length,
                             UART_TX_TIMEOUT_MS) == HAL_OK;
}

void board_mark_tag(uint32_t now_ms, uint32_t duration_ms)
{
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
    g_led_on = 1U;
    g_led_off_deadline_ms = now_ms + duration_ms;
}

void board_process(uint32_t now_ms)
{
    if (g_led_on != 0U && time_reached(now_ms, g_led_off_deadline_ms)) {
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
        g_led_on = 0U;
    }
#if APP_ENABLE_MOTOR
    process_motor(now_ms);
#endif
}

uint32_t board_rfid_rx_overflow_count(void)
{
    return g_rfid_rx_overflows;
}

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
    if (uart == NULL || uart->Instance != USART1) {
        return;
    }

    const uint16_t next_head =
        (uint16_t)((g_rfid_rx_head + 1U) % RFID_RX_RING_SIZE);
    if (next_head == g_rfid_rx_tail) {
        g_rfid_rx_overflows++;
    } else {
        g_rfid_rx_ring[g_rfid_rx_head] = g_rfid_rx_byte;
        g_rfid_rx_head = next_head;
    }
    (void)HAL_UART_Receive_IT(&g_rfid_uart, &g_rfid_rx_byte, 1U);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)
{
    if (uart != NULL && uart->Instance == USART1) {
        (void)HAL_UART_Receive_IT(&g_rfid_uart, &g_rfid_rx_byte, 1U);
    }
}
