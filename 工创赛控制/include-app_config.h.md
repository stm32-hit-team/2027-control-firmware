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
| `APP_RFID_DISPATCH_RETRY_MS` | 10 | 发送失败时的重试间隔 |
| `APP_RFID_REMOVAL_CONFIRMATIONS` | 3 | 连续几次读不到算拿走 |
| `APP_RFID_RETRY_LIMIT` | 2 | 单块读失败的重试次数 |

### 指示灯

| 名字 | 默认 | 说明 |
|---|---|---|
| `APP_LED_MARK_DURATION_MS` | 500 | 读到卡后 PA8 亮多久 |

### 缓启动

| 名字 | 默认 | 说明 |
|---|---|---|
| `APP_SOFT_START_MIN_MS` | 10000 | 旋钮最小等待，10 秒 |
| `APP_SOFT_START_MAX_MS` | 600000 | 旋钮最大等待，10 分钟 |
| `APP_ADC_FULL_SCALE` | 4095 | 12 位 ADC 满量程 |
| `APP_ADC_SAMPLE_COUNT` | 20 | 上电采样次数 |

现场觉得等待太长或太短，只改最小、最大这两个数。

### 电机

| 名字 | 默认 | 说明 |
|---|---|---|
| `APP_ENABLE_MOTOR` | 1 | 默认打开。首次上板必须架空车轮 |
| `APP_MOTOR_RAMP_MS` | 1000 | 升速用多久 |
| `APP_MOTOR_RUN_TIMEOUT_MS` | 600000 | 最长跑多久 |
| `APP_MOTOR_TARGET_COMPARE` | 850 | 目标占空比 |
| `APP_MOTOR_PWM_PERIOD` | 999 | 计数周期 |

上电后等多久再转，由 `PB1` 旋钮决定，不再用固定延时。

## 相关页

- [[05-编译烧录]]
- [[01-硬件对照]]
