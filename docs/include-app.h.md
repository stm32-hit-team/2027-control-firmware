# include/app.h

应用层对外的两个函数。

源文件：`include/app.h`

## 这个文件管什么

只声明两个函数。实现见 [[src-app.c]]。

## 函数

### app_init

```c
void app_init(uint32_t now_ms);
```

先准备语音，再准备读卡。`now_ms` 是开机后的毫秒数，给内部定时对表。

### app_process

```c
void app_process(uint32_t now_ms);
```

倒空 RFID 接收缓冲，并推进一步读卡、语音和板级定时。不阻塞。

## 相关页

- [[src-app.c]]
- [[02-主循环]]
