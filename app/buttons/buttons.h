#ifndef APP_BUTTONS_BUTTONS_H
#define APP_BUTTONS_BUTTONS_H

#include "game.h"

#include <stdbool.h>
#include <stdint.h>

void buttons_init(void);
void buttons_handle_irq(void);
void buttons_reset(void);
bool buttons_poll_move(uint32_t now_ms, GameMove *move);

#endif
