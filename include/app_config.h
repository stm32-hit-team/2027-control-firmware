/*
 * app_config.h：固件里能调的数字都放这里。
 * 只放常量和开关，不放逻辑。改完重新编译即可。
 */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/*
 * 两路串口都是 9600。
 * 和网表一致：H1（RFID）走 USART1，U7（语音）走 USART2。
 */
enum {
    APP_RFID_BAUD_RATE = 9600,
    APP_TTS_BAUD_RATE  = 9600
};

/* 开机是否先发语音语速命令。1 发，0 不发。 */
enum {
    APP_TTS_SET_SPEED_ON_STARTUP = 1
};

enum {
    APP_RFID_POLL_INTERVAL_MS      = 80,
    APP_RFID_RESPONSE_TIMEOUT_MS   = 100,
    APP_RFID_REMOVAL_POLL_MS       = 100,
    APP_RFID_DISPATCH_RETRY_MS     = 10,
    APP_RFID_REMOVAL_CONFIRMATIONS = 3,
    APP_RFID_RETRY_LIMIT           = 2
};

/* 读到卡以后，PA8 指示灯亮多久。 */
enum {
    APP_LED_MARK_DURATION_MS = 500
};

/*
 * 电机 PWM 默认关闭。
 * 打开要编 stm32f103c8_motor_check。
 * 驱动芯片的唤醒脚不在 MCU 上，只出 PWM 不一定会转。
 */
#ifndef APP_ENABLE_MOTOR
#define APP_ENABLE_MOTOR 0
#endif

enum {
    APP_MOTOR_START_DELAY_MS = 3000,
    APP_MOTOR_RAMP_MS        = 1000,
    APP_MOTOR_RUN_TIMEOUT_MS = 600000,
    APP_MOTOR_TARGET_COMPARE = 850,
    APP_MOTOR_PWM_PERIOD     = 999
};

#endif /* APP_CONFIG_H */
