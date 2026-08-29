# src/main.c

程序入口。先硬件，再业务，然后一直转。

源文件：`src/main.c`

## 这个文件管什么

只做上电后的第一步。具体业务在 [[src-app.c]]，硬件在 [[src-board_stm32.c]]。

## 函数

### main

```c
int main(void)
```

做什么：

1. 调 `board_init()` 配时钟、串口、灯。
2. 调 `app_init(board_millis())` 建读卡和语音。
3. 进死循环：每圈调 `app_process`，然后 `board_idle` 睡觉。

这个循环不会返回。SysTick 或 USART1 中断会把它叫醒。

## 和其它文件的关系

- 硬件初始化：[[src-board_stm32.c]] 的 `board_init`
- 业务初始化：[[src-app.c]] 的 `app_init`
- 每圈推进：[[src-app.c]] 的 `app_process`

## 相关页

- [[02-主循环]]
- [[00-总览]]
