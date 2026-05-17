#include "game_lcd_bitmaps.h"

#include "lcd_driver.h"

#include <stddef.h>
#include <stdint.h>

#define GAME_LCD_2X2_WIDTH  2u
#define GAME_LCD_2X2_HEIGHT 2u
#define GAME_LCD_COLS       16u
#define GAME_LCD_ROWS       2u

typedef struct {
    const uint8_t *top_left;
    const uint8_t *top_right;
    const uint8_t *bottom_left;
    const uint8_t *bottom_right;
} GameLcdBitmap2x2;

static const uint8_t SCISSORS_TOP_LEFT[LCD_CUSTOM_CHAR_ROWS] = {
    0x00,
    0x00,
    0x08,
    0x08,
    0x04,
    0x04,
    0x02,
    0x02,
};

static const uint8_t SCISSORS_TOP_RIGHT[LCD_CUSTOM_CHAR_ROWS] = {
    0x00,
    0x00,
    0x04,
    0x04,
    0x08,
    0x08,
    0x10,
    0x10,
};

static const uint8_t SCISSORS_BOTTOM_LEFT[LCD_CUSTOM_CHAR_ROWS] = {
    0x01,
    0x02,
    0x0C,
    0x12,
    0x12,
    0x0C,
    0x00,
    0x00,
};

static const uint8_t SCISSORS_BOTTOM_RIGHT[LCD_CUSTOM_CHAR_ROWS] = {
    0x00,
    0x10,
    0x0C,
    0x12,
    0x12,
    0x0C,
    0x00,
    0x00,
};

static const uint8_t PAPER_TOP_LEFT[LCD_CUSTOM_CHAR_ROWS] = {
    0x00,
    0x00,
    0x03,
    0x03,
    0x03,
    0x07,
    0x07,
    0x07,
};

static const uint8_t PAPER_TOP_RIGHT[LCD_CUSTOM_CHAR_ROWS] = {
    0x00,
    0x00,
    0x1F,
    0x1F,
    0x1F,
    0x1E,
    0x1E,
    0x1E,
};

static const uint8_t PAPER_BOTTOM_LEFT[LCD_CUSTOM_CHAR_ROWS] = {
    0x0F,
    0x0F,
    0x0F,
    0x1F,
    0x1F,
    0x1F,
    0x00,
    0x00,
};

static const uint8_t PAPER_BOTTOM_RIGHT[LCD_CUSTOM_CHAR_ROWS] = {
    0x1C,
    0x1C,
    0x1C,
    0x18,
    0x18,
    0x18,
    0x00,
    0x00,
};

static const uint8_t ROCK_TOP_LEFT[LCD_CUSTOM_CHAR_ROWS] = {
    0x00,
    0x00,
    0x03,
    0x07,
    0x0F,
    0x0F,
    0x1F,
    0x1F,
};

static const uint8_t ROCK_TOP_RIGHT[LCD_CUSTOM_CHAR_ROWS] = {
    0x00,
    0x00,
    0x10,
    0x18,
    0x18,
    0x1C,
    0x1C,
    0x1E,
};

static const uint8_t ROCK_BOTTOM_LEFT[LCD_CUSTOM_CHAR_ROWS] = {
    0x1F,
    0x0F,
    0x0F,
    0x07,
    0x07,
    0x03,
    0x00,
    0x00,
};

static const uint8_t ROCK_BOTTOM_RIGHT[LCD_CUSTOM_CHAR_ROWS] = {
    0x1F,
    0x1F,
    0x1E,
    0x1E,
    0x1E,
    0x10,
    0x00,
    0x00,
};

static const uint8_t BLANK_BITMAP[LCD_CUSTOM_CHAR_ROWS] = {
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
};

static const GameLcdBitmap2x2 SCISSORS_BITMAP = {
    .top_left = SCISSORS_TOP_LEFT,
    .top_right = SCISSORS_TOP_RIGHT,
    .bottom_left = SCISSORS_BOTTOM_LEFT,
    .bottom_right = SCISSORS_BOTTOM_RIGHT,
};

static const GameLcdBitmap2x2 PAPER_BITMAP = {
    .top_left = PAPER_TOP_LEFT,
    .top_right = PAPER_TOP_RIGHT,
    .bottom_left = PAPER_BOTTOM_LEFT,
    .bottom_right = PAPER_BOTTOM_RIGHT,
};

static const GameLcdBitmap2x2 ROCK_BITMAP = {
    .top_left = ROCK_TOP_LEFT,
    .top_right = ROCK_TOP_RIGHT,
    .bottom_left = ROCK_BOTTOM_LEFT,
    .bottom_right = ROCK_BOTTOM_RIGHT,
};

static const GameLcdBitmap2x2 BLANK_BITMAP_2X2 = {
    .top_left = BLANK_BITMAP,
    .top_right = BLANK_BITMAP,
    .bottom_left = BLANK_BITMAP,
    .bottom_right = BLANK_BITMAP,
};

static const GameLcdBitmap2x2 *game_lcd_bitmaps_for_move(GameMove move);
static void game_lcd_load_bitmap(uint8_t base_slot, const GameLcdBitmap2x2 *bitmap);

const char *game_lcd_move_label4(GameMove move)
{
    switch (move) {
    case GAME_MOVE_ROCK:
        return "Rock";
    case GAME_MOVE_PAPER:
        return "Papr";
    case GAME_MOVE_SCISSORS:
        return "Scis";
    default:
        return "????";
    }
}

void game_lcd_load_duel_bitmaps(GameMove player_move, GameMove bot_move)
{
    game_lcd_load_bitmap(GAME_LCD_DUEL_PLAYER_SLOT, game_lcd_bitmaps_for_move(player_move));
    game_lcd_load_bitmap(GAME_LCD_DUEL_BOT_SLOT, game_lcd_bitmaps_for_move(bot_move));
}

void game_lcd_draw_custom_2x2(uint8_t base_slot, uint8_t row, uint8_t col)
{
    if (((uint32_t)base_slot + 3u) >= LCD_CUSTOM_CHAR_COUNT) {
        return;
    }

    if (((uint32_t)row + GAME_LCD_2X2_HEIGHT) > GAME_LCD_ROWS) {
        return;
    }

    if (((uint32_t)col + GAME_LCD_2X2_WIDTH) > GAME_LCD_COLS) {
        return;
    }

    lcd_send_custom_char(base_slot, row, col);
    lcd_send_custom_char((uint8_t)(base_slot + 1u), row, (uint8_t)(col + 1u));
    lcd_send_custom_char((uint8_t)(base_slot + 2u), (uint8_t)(row + 1u), col);
    lcd_send_custom_char((uint8_t)(base_slot + 3u), (uint8_t)(row + 1u), (uint8_t)(col + 1u));
}

static const GameLcdBitmap2x2 *game_lcd_bitmaps_for_move(GameMove move)
{
    switch (move) {
    case GAME_MOVE_ROCK:
        return &ROCK_BITMAP;
    case GAME_MOVE_PAPER:
        return &PAPER_BITMAP;
    case GAME_MOVE_SCISSORS:
        return &SCISSORS_BITMAP;
    default:
        return &BLANK_BITMAP_2X2;
    }
}

static void game_lcd_load_bitmap(uint8_t base_slot, const GameLcdBitmap2x2 *bitmap)
{
    if (bitmap == NULL) {
        bitmap = &BLANK_BITMAP_2X2;
    }

    lcd_create_char(base_slot, bitmap->top_left);
    lcd_create_char((uint8_t)(base_slot + 1u), bitmap->top_right);
    lcd_create_char((uint8_t)(base_slot + 2u), bitmap->bottom_left);
    lcd_create_char((uint8_t)(base_slot + 3u), bitmap->bottom_right);
}
