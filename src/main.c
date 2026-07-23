#include "app.h"
#include "board.h"

int main(void)
{
    board_init();
    app_init(board_millis());

    for (;;) {
        app_process(board_millis());
        board_idle();
    }
}
