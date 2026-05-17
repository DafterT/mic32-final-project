#include "game_lcd_ui.h"

#include "game_lcd_bitmaps.h"
#include "lcd_driver.h"
#include "uid_hash.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define GAME_LCD_COLS 16u
#define GAME_LCD_ROWS 2u
#define GAME_LCD_FORMAT_BUFFER_SIZE 48u
#define GAME_LCD_PERCENT_MAX 100u
#define GAME_LCD_MENU_MATCHES_MAX 999u
#define GAME_LCD_DUEL_PLAYER_COL 5u
#define GAME_LCD_DUEL_BOT_COL 9u

static char lcd_shadow[GAME_LCD_ROWS][GAME_LCD_COLS];
static bool lcd_shadow_row_valid[GAME_LCD_ROWS];

static void game_lcd_fill_spaces(char line[GAME_LCD_COLS]);
static uint8_t game_lcd_text_length_up_to(const char *text, uint8_t max_length);
static void game_lcd_render_line(uint8_t row, const char target[GAME_LCD_COLS], bool force);
static void game_lcd_format_menu_title(char *buffer, uint16_t hash, uint32_t rounds, uint8_t index, uint8_t count);
static void game_lcd_format_welcome(char *buffer, uint16_t hash);
static void game_lcd_format_stats(char *buffer, const GameStats *stats);
static void game_lcd_format_percent_stats(char *buffer, const GameStats *stats);
static uint32_t game_lcd_percent(uint32_t value, uint32_t total);
static const char *game_lcd_result_text(GameRoundResult result);
static void game_lcd_format_start(char *buffer, size_t *position);
static void game_lcd_append_char(char *buffer, size_t *position, char value);
static void game_lcd_append_string(char *buffer, size_t *position, const char *text);
static void game_lcd_append_uint32(char *buffer, size_t *position, uint32_t value);
static void game_lcd_append_uint32_min_width(char *buffer, size_t *position, uint32_t value, uint8_t min_width);
static void game_lcd_append_hex4(char *buffer, size_t *position, uint16_t value);

void game_lcd_clear(void)
{
    char line[GAME_LCD_COLS];
    uint8_t row;

    game_lcd_fill_spaces(line);
    for (row = 0u; row < GAME_LCD_ROWS; ++row) {
        game_lcd_render_line(row, line, true);
    }
}

void game_lcd_write_line(uint8_t row, const char *text)
{
    char line[GAME_LCD_COLS];
    uint8_t length;

    if (row >= GAME_LCD_ROWS) {
        return;
    }

    game_lcd_fill_spaces(line);
    length = game_lcd_text_length_up_to(text, GAME_LCD_COLS);
    if (length != 0u) {
        memcpy(line, text, length);
    }

    game_lcd_render_line(row, line, false);
}

void game_lcd_center_line(uint8_t row, const char *text)
{
    char line[GAME_LCD_COLS];
    uint8_t length;
    uint8_t left;

    if (row >= GAME_LCD_ROWS) {
        return;
    }

    game_lcd_fill_spaces(line);
    length = game_lcd_text_length_up_to(text, GAME_LCD_COLS);
    left = (uint8_t)((GAME_LCD_COLS - length) / 2u);
    if (length != 0u) {
        memcpy(&line[left], text, length);
    }

    game_lcd_render_line(row, line, false);
}

void game_lcd_show_splash(void)
{
    game_lcd_center_line(0u, "Rock Paper");
    game_lcd_center_line(1u, "Scissors game");
}

void game_lcd_show_menu_empty(void)
{
    game_lcd_center_line(0u, "No players");
    game_lcd_center_line(1u, "Apply RFID card");
}

void game_lcd_show_menu_user(const GameUser *user, uint8_t index, uint8_t count)
{
    char line[GAME_LCD_FORMAT_BUFFER_SIZE];
    uint32_t rounds = 0u;
    uint16_t hash = 0u;

    if (user != NULL) {
        rounds = user->stats.rounds;
        hash = uid_hash16(&user->id);
    }

    game_lcd_format_menu_title(line, hash, rounds, index, count);
    game_lcd_write_line(0u, line);

    game_lcd_format_percent_stats(line, (user != NULL) ? &user->stats : NULL);
    game_lcd_write_line(1u, line);
}

void game_lcd_show_welcome(const GameUser *user)
{
    char line[GAME_LCD_FORMAT_BUFFER_SIZE];
    uint16_t hash = 0u;

    if (user != NULL) {
        hash = uid_hash16(&user->id);
    }

    game_lcd_format_welcome(line, hash);
    game_lcd_write_line(0u, line);

    game_lcd_format_stats(line, (user != NULL) ? &user->stats : NULL);
    game_lcd_write_line(1u, line);
}

void game_lcd_show_wait_move(const GameUser *user)
{
    char line[GAME_LCD_FORMAT_BUFFER_SIZE];

    game_lcd_center_line(0u, "Choose move");
    game_lcd_format_stats(line, (user != NULL) ? &user->stats : NULL);
    game_lcd_write_line(1u, line);
}

void game_lcd_show_chant(const char *word)
{
    game_lcd_center_line(0u, word);
    game_lcd_write_line(1u, "");
}

void game_lcd_show_duel(GameMove player_move, GameMove bot_move)
{
    char top[GAME_LCD_COLS];
    char bottom[GAME_LCD_COLS];
    const char *player_label = game_lcd_move_label4(player_move);
    const char *bot_label = game_lcd_move_label4(bot_move);

    game_lcd_load_duel_bitmaps(player_move, bot_move);
    game_lcd_fill_spaces(top);
    game_lcd_fill_spaces(bottom);

    memcpy(&top[0u], "You", 3u);
    top[GAME_LCD_DUEL_PLAYER_COL] = (char)GAME_LCD_DUEL_PLAYER_SLOT;
    top[GAME_LCD_DUEL_PLAYER_COL + 1u] = (char)(GAME_LCD_DUEL_PLAYER_SLOT + 1u);
    top[GAME_LCD_DUEL_BOT_COL] = (char)GAME_LCD_DUEL_BOT_SLOT;
    top[GAME_LCD_DUEL_BOT_COL + 1u] = (char)(GAME_LCD_DUEL_BOT_SLOT + 1u);
    memcpy(&top[13u], "Bot", 3u);

    memcpy(&bottom[0u], player_label, 4u);
    bottom[GAME_LCD_DUEL_PLAYER_COL] = (char)(GAME_LCD_DUEL_PLAYER_SLOT + 2u);
    bottom[GAME_LCD_DUEL_PLAYER_COL + 1u] = (char)(GAME_LCD_DUEL_PLAYER_SLOT + 3u);
    bottom[GAME_LCD_DUEL_BOT_COL] = (char)(GAME_LCD_DUEL_BOT_SLOT + 2u);
    bottom[GAME_LCD_DUEL_BOT_COL + 1u] = (char)(GAME_LCD_DUEL_BOT_SLOT + 3u);
    memcpy(&bottom[12u], bot_label, 4u);

    game_lcd_render_line(0u, top, false);
    game_lcd_render_line(1u, bottom, false);
}

void game_lcd_show_result(GameRoundResult result, const GameUser *user)
{
    char line[GAME_LCD_FORMAT_BUFFER_SIZE];

    game_lcd_center_line(0u, game_lcd_result_text(result));
    game_lcd_format_stats(line, (user != NULL) ? &user->stats : NULL);
    game_lcd_write_line(1u, line);
}

static void game_lcd_fill_spaces(char line[GAME_LCD_COLS])
{
    uint8_t index;

    for (index = 0u; index < GAME_LCD_COLS; ++index) {
        line[index] = ' ';
    }
}

static uint8_t game_lcd_text_length_up_to(const char *text, uint8_t max_length)
{
    uint8_t length = 0u;

    if (text == NULL) {
        return 0u;
    }

    while ((length < max_length) && (text[length] != '\0')) {
        ++length;
    }

    return length;
}

static void game_lcd_render_line(uint8_t row, const char target[GAME_LCD_COLS], bool force)
{
    uint8_t col = 0u;

    if ((row >= GAME_LCD_ROWS) || (target == NULL)) {
        return;
    }

    if (force || !lcd_shadow_row_valid[row]) {
        lcd_put_cur(row, 0);
        lcd_send_data_buffer(target, GAME_LCD_COLS);
        memcpy(lcd_shadow[row], target, GAME_LCD_COLS);
        lcd_shadow_row_valid[row] = true;
        return;
    }

    while (col < GAME_LCD_COLS) {
        uint8_t start;

        if (lcd_shadow[row][col] == target[col]) {
            ++col;
            continue;
        }

        start = col;
        do {
            ++col;
        } while ((col < GAME_LCD_COLS) && (lcd_shadow[row][col] != target[col]));

        lcd_put_cur(row, start);
        lcd_send_data_buffer(&target[start], (uint8_t)(col - start));
        memcpy(&lcd_shadow[row][start], &target[start], (size_t)(col - start));
    }
}

static void game_lcd_format_menu_title(char *buffer, uint16_t hash, uint32_t rounds, uint8_t index, uint8_t count)
{
    size_t position;

    game_lcd_format_start(buffer, &position);
    game_lcd_append_hex4(buffer, &position, hash);
    game_lcd_append_string(buffer, &position, " M");
    if (rounds > GAME_LCD_MENU_MATCHES_MAX) {
        game_lcd_append_string(buffer, &position, ">999");
    } else {
        game_lcd_append_char(buffer, &position, ':');
        game_lcd_append_uint32_min_width(buffer, &position, rounds, 3u);
    }
    game_lcd_append_char(buffer, &position, ' ');
    game_lcd_append_uint32_min_width(buffer, &position, (uint32_t)index + 1u, 2u);
    game_lcd_append_char(buffer, &position, '/');
    game_lcd_append_uint32_min_width(buffer, &position, count, 2u);
}

static void game_lcd_format_welcome(char *buffer, uint16_t hash)
{
    size_t position;

    game_lcd_format_start(buffer, &position);
    game_lcd_append_string(buffer, &position, "Welcome, ");
    game_lcd_append_hex4(buffer, &position, hash);
}

static void game_lcd_format_stats(char *buffer, const GameStats *stats)
{
    uint32_t wins = 0u;
    uint32_t draws = 0u;
    uint32_t losses = 0u;
    size_t position;

    if (buffer == NULL) {
        return;
    }

    if (stats != NULL) {
        wins = stats->wins;
        draws = stats->draws;
        losses = stats->losses;
    }

    game_lcd_format_start(buffer, &position);
    game_lcd_append_string(buffer, &position, "W/D/L: ");
    game_lcd_append_uint32(buffer, &position, wins);
    game_lcd_append_char(buffer, &position, '/');
    game_lcd_append_uint32(buffer, &position, draws);
    game_lcd_append_char(buffer, &position, '/');
    game_lcd_append_uint32(buffer, &position, losses);
    if (game_lcd_text_length_up_to(buffer, GAME_LCD_COLS + 1u) <= GAME_LCD_COLS) {
        return;
    }

    game_lcd_format_start(buffer, &position);
    game_lcd_append_string(buffer, &position, "WDL: ");
    game_lcd_append_uint32(buffer, &position, wins);
    game_lcd_append_char(buffer, &position, '/');
    game_lcd_append_uint32(buffer, &position, draws);
    game_lcd_append_char(buffer, &position, '/');
    game_lcd_append_uint32(buffer, &position, losses);
    if (game_lcd_text_length_up_to(buffer, GAME_LCD_COLS + 1u) <= GAME_LCD_COLS) {
        return;
    }

    game_lcd_format_start(buffer, &position);
    game_lcd_append_uint32(buffer, &position, wins);
    game_lcd_append_char(buffer, &position, '/');
    game_lcd_append_uint32(buffer, &position, draws);
    game_lcd_append_char(buffer, &position, '/');
    game_lcd_append_uint32(buffer, &position, losses);
}

static void game_lcd_format_percent_stats(char *buffer, const GameStats *stats)
{
    char stats_text[GAME_LCD_FORMAT_BUFFER_SIZE];
    uint32_t win_pct = 0u;
    uint32_t draw_pct = 0u;
    uint32_t loss_pct = 0u;
    uint8_t left_padding;
    uint8_t text_length;
    size_t position;
    size_t text_position;

    if (buffer == NULL) {
        return;
    }

    if ((stats != NULL) && (stats->rounds != 0u)) {
        win_pct = game_lcd_percent(stats->wins, stats->rounds);
        draw_pct = game_lcd_percent(stats->draws, stats->rounds);

        if ((win_pct + draw_pct) <= GAME_LCD_PERCENT_MAX) {
            loss_pct = GAME_LCD_PERCENT_MAX - win_pct - draw_pct;
        }
    }

    game_lcd_format_start(stats_text, &text_position);
    game_lcd_append_char(stats_text, &text_position, 'W');
    game_lcd_append_uint32_min_width(stats_text, &text_position, win_pct, 2u);
    game_lcd_append_string(stats_text, &text_position, "% D");
    game_lcd_append_uint32_min_width(stats_text, &text_position, draw_pct, 2u);
    game_lcd_append_string(stats_text, &text_position, "% L");
    game_lcd_append_uint32_min_width(stats_text, &text_position, loss_pct, 2u);
    game_lcd_append_char(stats_text, &text_position, '%');

    text_length = game_lcd_text_length_up_to(stats_text, GAME_LCD_COLS);
    left_padding = (text_length < GAME_LCD_COLS) ? (uint8_t)((GAME_LCD_COLS - text_length) / 2u) : 0u;

    game_lcd_format_start(buffer, &position);
    while (left_padding != 0u) {
        game_lcd_append_char(buffer, &position, ' ');
        --left_padding;
    }
    game_lcd_append_string(buffer, &position, stats_text);
}

static uint32_t game_lcd_percent(uint32_t value, uint32_t total)
{
    if (total == 0u) {
        return 0u;
    }

    return (uint32_t)(((uint64_t)value * GAME_LCD_PERCENT_MAX) / total);
}

static const char *game_lcd_result_text(GameRoundResult result)
{
    switch (result) {
    case GAME_ROUND_PLAYER_WIN:
        return "Win";
    case GAME_ROUND_DRAW:
        return "Draw";
    case GAME_ROUND_PLAYER_LOSS:
        return "Lose";
    default:
        return "????";
    }
}

static void game_lcd_format_start(char *buffer, size_t *position)
{
    if (position != NULL) {
        *position = 0u;
    }

    if (buffer != NULL) {
        buffer[0] = '\0';
    }
}

static void game_lcd_append_char(char *buffer, size_t *position, char value)
{
    if ((buffer == NULL) || (position == NULL) || (*position >= (GAME_LCD_FORMAT_BUFFER_SIZE - 1u))) {
        return;
    }

    buffer[*position] = value;
    *position += 1u;
    buffer[*position] = '\0';
}

static void game_lcd_append_string(char *buffer, size_t *position, const char *text)
{
    if (text == NULL) {
        return;
    }

    while (*text != '\0') {
        game_lcd_append_char(buffer, position, *text);
        ++text;
    }
}

static void game_lcd_append_uint32(char *buffer, size_t *position, uint32_t value)
{
    char digits[10];
    uint8_t length = 0u;

    do {
        digits[length] = (char)('0' + (value % 10u));
        value /= 10u;
        ++length;
    } while (value != 0u);

    while (length != 0u) {
        --length;
        game_lcd_append_char(buffer, position, digits[length]);
    }
}

static void game_lcd_append_uint32_min_width(char *buffer, size_t *position, uint32_t value, uint8_t min_width)
{
    uint32_t remaining = value;
    uint8_t digit_count = 1u;

    while (remaining >= 10u) {
        remaining /= 10u;
        ++digit_count;
    }

    while (digit_count < min_width) {
        game_lcd_append_char(buffer, position, '0');
        ++digit_count;
    }

    game_lcd_append_uint32(buffer, position, value);
}

static void game_lcd_append_hex4(char *buffer, size_t *position, uint16_t value)
{
    uint8_t nibble_index;

    for (nibble_index = 0u; nibble_index < 4u; ++nibble_index) {
        uint8_t shift = (uint8_t)((3u - nibble_index) * 4u);
        uint8_t digit = (uint8_t)((value >> shift) & 0x0Fu);
        char character = (digit < 10u) ? (char)('0' + digit) : (char)('A' + (digit - 10u));

        game_lcd_append_char(buffer, position, character);
    }
}
