#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* Same serial-module assumptions as the 2026 firmware. */
#define APP_RFID_BAUD_RATE 9600U
#define APP_TTS_BAUD_RATE 9600U
#define APP_TTS_SET_SPEED_ON_STARTUP 1

#define APP_RFID_POLL_INTERVAL_MS 80U
#define APP_RFID_RESPONSE_TIMEOUT_MS 100U
#define APP_RFID_REMOVAL_POLL_MS 100U
#define APP_RFID_DISPATCH_RETRY_MS 10U
#define APP_RFID_REMOVAL_CONFIRMATIONS 3U
#define APP_RFID_RETRY_LIMIT 2U

#define APP_LED_MARK_DURATION_MS 500U

/*
 * Motor output is deliberately off until the current circuit is finalized.
 * When enabled, PA0/PA1 retain last year's TIM2 CH1/CH2 truth table.
 */
#ifndef APP_ENABLE_MOTOR
#define APP_ENABLE_MOTOR 0
#endif
#define APP_MOTOR_START_DELAY_MS 3000U
#define APP_MOTOR_RAMP_MS 1000U
#define APP_MOTOR_RUN_TIMEOUT_MS 600000U
#define APP_MOTOR_TARGET_COMPARE 850U
#define APP_MOTOR_PWM_PERIOD 999U

#endif
