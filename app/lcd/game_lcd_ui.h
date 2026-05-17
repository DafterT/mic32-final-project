#ifndef APP_LCD_GAME_LCD_UI_H
#define APP_LCD_GAME_LCD_UI_H

#include "game.h"

#include <stdint.h>

void game_lcd_clear(void);
void game_lcd_write_line(uint8_t row, const char *text);
void game_lcd_center_line(uint8_t row, const char *text);
void game_lcd_show_splash(void);
void game_lcd_show_menu_empty(void);
void game_lcd_show_menu_user(const GameUser *user, uint8_t index, uint8_t count);
void game_lcd_show_welcome(const GameUser *user);
void game_lcd_show_wait_move(const GameUser *user);
void game_lcd_show_chant(const char *word);
void game_lcd_show_duel(GameMove player_move, GameMove bot_move);
void game_lcd_show_result(GameRoundResult result, const GameUser *user);

#endif
