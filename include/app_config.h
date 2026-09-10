#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* 固件里能调的数字都放这里。只放常量和开关，不放逻辑。 */

/*
 * H1（RFID）走 USART1：先用 9600 发改速命令，再按 115200 工作。
 * U7（语音）走 USART2，一直是 9600。
 */
enum {
    APP_RFID_SETUP_BAUD_RATE = 9600,
    APP_RFID_BAUD_RATE       = 115200,
    APP_TTS_BAUD_RATE        = 9600
};

/* 开机是否先发语音语速命令。1 发，0 不发。 */
enum {
    APP_TTS_SET_SPEED_ON_STARTUP = 1
};

enum {
    APP_RFID_POLL_INTERVAL_MS      = 40,
    APP_RFID_RESPONSE_TIMEOUT_MS   = 80,
    APP_RFID_REMOVAL_POLL_MS       = 80,
    APP_RFID_DISPATCH_RETRY_MS     = 10,
    APP_RFID_REMOVAL_CONFIRMATIONS = 3,
    APP_RFID_RETRY_LIMIT           = 2
};

/* 读到卡以后，PA8 指示灯亮多久。 */
enum {
    APP_LED_MARK_DURATION_MS = 500
};

enum {
    APP_ADC_FULL_SCALE   = 4095,
    APP_ADC_SAMPLE_COUNT = 20
};

/*
 * 电机时序对齐省赛：固定 3 秒后起步，3 秒爬升。
 * PB1 旋钮只决定跑多久再停。换算和省赛同一套电阻公式。
 */
enum {
    APP_MOTOR_START_LATE_MS     = 3000,
    APP_MOTOR_RAMP_MS           = 3000,
    APP_MOTOR_TARGET_COMPARE    = 600,
    APP_MOTOR_PWM_PERIOD        = 999,
    APP_MOTOR_STOP_MIN_MS       = 10000,
    APP_MOTOR_STOP_MAX_MS       = 600000,
    APP_MOTOR_KNOB_RES_OHMS     = 5000,
    APP_MOTOR_KNOB_SERIES_OHMS  = 1000
};

/*
 * 电机默认打开。首次上板必须架空车轮。
 */
#ifndef APP_ENABLE_MOTOR
#define APP_ENABLE_MOTOR 1
#endif

#endif /* APP_CONFIG_H */
