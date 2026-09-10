# include/app_config.h

固件里能调的数字都放这里。

源文件：`include/app_config.h`

## 这个文件管什么

只放常量和开关，不放逻辑。改完重新编译即可。

## 常量

### 串口

| 名字 | 默认 | 说明 |
|---|---|---|
| `APP_RFID_SETUP_BAUD_RATE` | 9600 | 给读卡器发改速命令时用 |
| `APP_RFID_BAUD_RATE` | 115200 | RFID 工作波特率，走 USART1 |
| `APP_TTS_BAUD_RATE` | 9600 | 语音波特率，走 USART2 |

### 语音

| 名字 | 默认 | 说明 |
|---|---|---|
| `APP_TTS_SET_SPEED_ON_STARTUP` | 1 | 开机是否先发语速命令 |

### 读卡节奏

| 名字 | 默认 | 说明 |
|---|---|---|
| `APP_RFID_POLL_INTERVAL_MS` | 40 | 空闲时多久问一次卡 |
| `APP_RFID_RESPONSE_TIMEOUT_MS` | 80 | 等回复的超时 |
| `APP_RFID_REMOVAL_POLL_MS` | 80 | 确认卡还在的间隔 |
| `APP_RFID_DISPATCH_RETRY_MS` | 10 | 发送失败时的重试间隔 |
| `APP_RFID_REMOVAL_CONFIRMATIONS` | 3 | 连续几次读不到算拿走 |
| `APP_RFID_RETRY_LIMIT` | 2 | 单块读失败的重试次数 |

同一张卡还在场上只念一次。这是按 UID 去重，不是按文字。

### 指示灯

| 名字 | 默认 | 说明 |
|---|---|---|
| `APP_LED_MARK_DURATION_MS` | 500 | 读到卡后 PA8 亮多久 |

### 旋钮和 ADC

| 名字 | 默认 | 说明 |
|---|---|---|
| `APP_ADC_FULL_SCALE` | 4095 | 12 位 ADC 满量程 |
| `APP_ADC_SAMPLE_COUNT` | 20 | 上电采样次数 |
| `APP_MOTOR_KNOB_RES_OHMS` | 5000 | 电位器满量程电阻，对齐省赛 |
| `APP_MOTOR_KNOB_SERIES_OHMS` | 1000 | 分压公式里的串联电阻 |

### 电机

| 名字 | 默认 | 说明 |
|---|---|---|
| `APP_ENABLE_MOTOR` | 1 | 默认打开。首次上板必须架空车轮 |
| `APP_MOTOR_START_LATE_MS` | 3000 | 上电后多久起步 |
| `APP_MOTOR_RAMP_MS` | 3000 | 升速用多久 |
| `APP_MOTOR_TARGET_COMPARE` | 600 | 目标占空比 |
| `APP_MOTOR_PWM_PERIOD` | 999 | 计数周期 |
| `APP_MOTOR_STOP_MIN_MS` | 10000 | 旋钮最短停车，10 秒 |
| `APP_MOTOR_STOP_MAX_MS` | 600000 | 旋钮最长停车，10 分钟 |

`PB1` 旋钮只决定跑多久后停。起步等待是固定的 3 秒。

## 相关页

- [[05-编译烧录]]
- [[01-硬件对照]]
