/*
 * main.c：复位后的入口。先硬件，再业务，然后一直转。
 */

#include "app.h"
#include "board.h"

int main(void)
{
    board_init();
    app_init(board_millis());

    /* 事件泵，不会返回。SysTick 或 USART1 会把 board_idle 叫醒。 */
    for (;;) {
        app_process(board_millis());
        board_idle();
    }
}
