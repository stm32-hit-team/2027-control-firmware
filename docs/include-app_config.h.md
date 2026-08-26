# include/app_config.h

固件里能调的数字都放这里。

源文件：`include/app_config.h`

## 这个文件管什么

只放常量和开关，不放逻辑。改完重新编译即可。

## 常量

### 串口

| 名字 | 默认 | 说明 |
|---|---|---|
| `APP_RFID_BAUD_RATE` | 9600 | RFID 波特率，走 USART1 |
| `APP_TTS_BAUD_RATE` | 9600 | 语音波特率，走 USART2 |

### 语音

| 名字 | 默认 | 说明 |
|---|---|---|
| `APP_TTS_SET_SPEED_ON_STARTUP` | 1 | 开机是否先发语速命令 |

### 读卡节奏

| 名字 | 默认 | 说明 |
|---|---|---|
| `APP_RFID_POLL_INTERVAL_MS` | 80 | 空闲时多久问一次卡 |
| `APP_RFID_RESPONSE_TIMEOUT_MS` | 100 | 等回复的超时 |
| `APP_RFID_REMOVAL_POLL_MS` | 100 | 确认卡还在的间隔 |
| `APP_RFID_DISPATCH_RETRY_MS` | 10 | 语音满时的重试间隔 |
| `APP_RFID_REMOVAL_CONFIRMATIONS` | 3 | 连续几次读不到算拿走 |
| `APP_RFID_RETRY_LIMIT` | 2 | 单块读失败的重试次数 |

### 指示灯

| 名字 | 默认 | 说明 |
|---|---|---|
| `APP_LED_MARK_DURATION_MS` | 500 | 读到卡后 PA8 亮多久 |

### 电机

| 名字 | 默认 | 说明 |
|---|---|---|
| `APP_ENABLE_MOTOR` | 0 | 默认关闭，编译电机环境才开 |
| `APP_MOTOR_START_DELAY_MS` | 3000 | 上电先停多久 |
| `APP_MOTOR_RAMP_MS` | 1000 | 升速用多久 |
| `APP_MOTOR_RUN_TIMEOUT_MS` | 600000 | 最长跑多久 |
| `APP_MOTOR_TARGET_COMPARE` | 850 | 目标占空比 |
| `APP_MOTOR_PWM_PERIOD` | 999 | 计数周期 |

## 相关页

- [[05-编译烧录]]
- [[01-硬件对照]]
