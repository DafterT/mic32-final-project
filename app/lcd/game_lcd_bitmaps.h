#ifndef APP_LCD_GAME_LCD_BITMAPS_H
#define APP_LCD_GAME_LCD_BITMAPS_H

#include "game.h"

#include <stdint.h>

#define GAME_LCD_DUEL_PLAYER_SLOT 0u
#define GAME_LCD_DUEL_BOT_SLOT    4u

const char *game_lcd_move_label4(GameMove move);
void game_lcd_load_duel_bitmaps(GameMove player_move, GameMove bot_move);
void game_lcd_draw_custom_2x2(uint8_t base_slot, uint8_t row, uint8_t col);

#endif
