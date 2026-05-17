#include "buttons.h"
#include "game.h"
#include "game_lcd_ui.h"
#include "game_storage.h"
#include "game_storage_format.h"
#include "mik32_hal.h"
#include "mik32_hal_i2c.h"
#include "mik32_hal_irq.h"
#include "mik32_hal_scr1_timer.h"
#include "mik32_hal_spi.h"
#include "mik32_hal_usart.h"
#include "lcd_driver.h"
#include "mfrc522.h"
#include "rfid_config.h"
#include "uid_hash.h"
#include "xprintf.h"

#include <stdbool.h>
#include <stdint.h>

#define UART_BAUDRATE 115200u
#define MOVE_COUNT    3u

#define APP_SPLASH_MS     4000u
#define APP_WELCOME_MS    2000u
#define APP_CHANT_STEP_MS 400u
#define APP_DUEL_MS       2000u
#define APP_RESULT_MS     2000u

I2C_HandleTypeDef hi2c;

static SPI_HandleTypeDef hspi0;
static MFRC522 mfrc522;

typedef enum {
    APP_STATE_SPLASH = 0u,
    APP_STATE_MENU,
    APP_STATE_WELCOME,
    APP_STATE_WAIT_MOVE,
    APP_STATE_CHANT,
    APP_STATE_DUEL,
    APP_STATE_RESULT
} AppState;

typedef struct {
    GameUser user;
    GameUserId card_id;
    GameUser menu_users[GAME_EEPROM_SLOT_COUNT];
    uint8_t menu_user_count;
    uint8_t menu_index;
    bool user_active;
    uint32_t next_rfid_poll_ms;
    AppState state;
    uint32_t state_until_ms;
    uint8_t chant_step;
    GameMove last_player_move;
    GameRound last_round;
} AppContext;

static void app_enter_splash(AppContext *context, uint32_t now);
static void app_enter_menu(AppContext *context);
static void app_enter_welcome(AppContext *context, uint32_t now);
static void app_enter_wait_move(AppContext *context);
static void app_enter_chant(AppContext *context, uint32_t now);
static void app_enter_duel(AppContext *context, uint32_t now);
static void app_enter_result(AppContext *context, uint32_t now);
static void app_update_state(AppContext *context, uint32_t now);
static void app_poll_inputs(AppContext *context, uint32_t now);
static bool app_poll_rfid(AppContext *context, uint32_t now);
static bool app_poll_buttons(AppContext *context, uint32_t now);
static void app_drain_locked_inputs(AppContext *context, AppState locked_state);
static void app_handle_button(AppContext *context, GameMove move, uint32_t now);
static void app_handle_menu_button(AppContext *context, GameMove move);
static void app_handle_game_button(AppContext *context, GameMove player_move, uint32_t now);
static void app_handle_card(AppContext *context, const GameUserId *card_id, uint32_t now);
static bool app_activate_user(AppContext *context, const GameUserId *card_id, uint32_t now);
static bool app_save_active_user(AppContext *context);
static void app_load_menu_users(AppContext *context);
static void app_sort_menu_users(AppContext *context);
static bool app_menu_user_before(const GameUser *left, const GameUser *right);
static void app_show_menu(const AppContext *context);
static void app_clamp_menu_index(AppContext *context);
static bool app_state_accepts_rfid(AppState state);
static bool app_state_accepts_buttons(AppState state);
static bool app_state_is_locked(AppState state);
static bool time_reached(uint32_t now_ms, uint32_t deadline_ms);
static uint32_t percent_u32(uint32_t value, uint32_t total);
static const char *chant_word(uint8_t step);
static const char *menu_action_name(GameMove move);
static void log_card_accepted(const GameUserId *id);
static void log_card_ignored(AppState state, const GameUserId *id);
static void log_button_accepted_action(const char *action);
static void log_button_accepted_move(GameMove move);
static void log_button_ignored(AppState state, GameMove move);
static void print_round_result(GameMove player_move, const GameRound *round);
static void print_init_status(GameStatus status, const GameUser *user);
static void print_save_status(GameStatus status, const GameUser *user);
static void SystemClock_Config(void);
static void USART0_Init(void);
static void I2C1_Init(void);
static void SPI0_Init(void);
static bool read_rfid_card_once(GameUserId *id);
static void copy_rfid_uid_to_user_id(const Uid *uid, GameUserId *id);
static bool same_user_id(const GameUserId *left, const GameUserId *right);
static void print_user_id(const GameUserId *id);
static const char *move_name(GameMove move);
static const char *result_name(GameRoundResult result);
static const char *state_name(AppState state);
static const char *status_name(GameStatus status);
static bool status_is_fatal(GameStatus status);
static GameStatus save_user(GameUser *user);

uint32_t HAL_Micros(void)
{
    return HAL_Time_SCR1TIM_Micros();
}

uint32_t HAL_Millis(void)
{
    return HAL_Time_SCR1TIM_Millis();
}

void HAL_DelayUs(uint32_t time_us)
{
    HAL_Time_SCR1TIM_DelayUs(time_us);
}

void HAL_DelayMs(uint32_t time_ms)
{
    HAL_Time_SCR1TIM_DelayMs(time_ms);
}

int main(void)
{
    AppContext context = {0};

    SystemClock_Config();
    HAL_Init();
    HAL_Time_SCR1TIM_Init();
    USART0_Init();
    I2C1_Init();
    SPI0_Init();
    buttons_init();

    lcd_init();

    MFRC522_Init(&mfrc522, &hspi0, RFID_CS_PORT, RFID_CS_PIN, RFID_RST_PORT, RFID_RST_PIN);
    PCD_Init(&mfrc522);

    xprintf("\r\ngame start\r\n");
    app_enter_splash(&context, HAL_Millis());

    while (1) {
        uint32_t now = HAL_Millis();

        app_update_state(&context, now);
        app_poll_inputs(&context, now);
    }

    return 0;
}

static void app_enter_splash(AppContext *context, uint32_t now)
{
    if (context == NULL) {
        return;
    }

    context->state = APP_STATE_SPLASH;
    context->state_until_ms = now + APP_SPLASH_MS;
    game_lcd_show_splash();
    buttons_reset();
}

static void app_enter_menu(AppContext *context)
{
    if (context == NULL) {
        return;
    }

    context->state = APP_STATE_MENU;
    context->user_active = false;
    context->menu_index = 0u;
    app_load_menu_users(context);
    app_show_menu(context);
    buttons_reset();
}

static void app_enter_welcome(AppContext *context, uint32_t now)
{
    if (context == NULL) {
        return;
    }

    context->state = APP_STATE_WELCOME;
    context->state_until_ms = now + APP_WELCOME_MS;
    game_lcd_show_welcome(&context->user);
    buttons_reset();
}

static void app_enter_wait_move(AppContext *context)
{
    if (context == NULL) {
        return;
    }

    context->state = APP_STATE_WAIT_MOVE;
    game_lcd_show_wait_move(&context->user);
    buttons_reset();
}

static void app_enter_chant(AppContext *context, uint32_t now)
{
    if (context == NULL) {
        return;
    }

    context->state = APP_STATE_CHANT;
    context->chant_step = 0u;
    context->state_until_ms = now + APP_CHANT_STEP_MS;
    game_lcd_show_chant(chant_word(context->chant_step));
    buttons_reset();
}

static void app_enter_duel(AppContext *context, uint32_t now)
{
    if (context == NULL) {
        return;
    }

    context->state = APP_STATE_DUEL;
    context->state_until_ms = now + APP_DUEL_MS;
    game_lcd_show_duel(context->last_player_move, context->last_round.device_move);
}

static void app_enter_result(AppContext *context, uint32_t now)
{
    if (context == NULL) {
        return;
    }

    context->state = APP_STATE_RESULT;
    context->state_until_ms = now + APP_RESULT_MS;
    game_lcd_show_result(context->last_round.result, &context->user);
}

static void app_update_state(AppContext *context, uint32_t now)
{
    if (context == NULL) {
        return;
    }

    switch (context->state) {
    case APP_STATE_SPLASH:
        if (time_reached(now, context->state_until_ms)) {
            app_drain_locked_inputs(context, APP_STATE_SPLASH);
            app_enter_menu(context);
        }
        break;
    case APP_STATE_WELCOME:
        if (time_reached(now, context->state_until_ms)) {
            app_drain_locked_inputs(context, APP_STATE_WELCOME);
            app_enter_wait_move(context);
        }
        break;
    case APP_STATE_CHANT:
        if (!time_reached(now, context->state_until_ms)) {
            break;
        }
        if (context->chant_step < 2u) {
            context->chant_step += 1u;
            context->state_until_ms = now + APP_CHANT_STEP_MS;
            game_lcd_show_chant(chant_word(context->chant_step));
        } else {
            app_enter_duel(context, now);
        }
        break;
    case APP_STATE_DUEL:
        if (time_reached(now, context->state_until_ms)) {
            app_enter_result(context, now);
        }
        break;
    case APP_STATE_RESULT:
        if (time_reached(now, context->state_until_ms)) {
            app_drain_locked_inputs(context, APP_STATE_RESULT);
            app_enter_wait_move(context);
        }
        break;
    case APP_STATE_MENU:
    case APP_STATE_WAIT_MOVE:
    default:
        break;
    }
}

static void app_poll_inputs(AppContext *context, uint32_t now)
{
    if (context == NULL) {
        return;
    }

    if (app_poll_rfid(context, now)) {
        return;
    }

    (void)app_poll_buttons(context, now);
}

static bool app_poll_rfid(AppContext *context, uint32_t now)
{
    if ((context == NULL) || !app_state_accepts_rfid(context->state)) {
        return false;
    }

    if (!time_reached(now, context->next_rfid_poll_ms)) {
        return false;
    }

    context->next_rfid_poll_ms = now + RFID_POLL_DELAY_MS;

    if (!read_rfid_card_once(&context->card_id)) {
        return false;
    }

    app_handle_card(context, &context->card_id, now);
    return true;
}

static bool app_poll_buttons(AppContext *context, uint32_t now)
{
    GameMove move;

    if (context == NULL) {
        return false;
    }

    if (!buttons_poll_move(now, &move)) {
        return false;
    }

    if (!app_state_accepts_buttons(context->state)) {
        log_button_ignored(context->state, move);
        return true;
    }

    app_handle_button(context, move, now);
    return true;
}

static void app_drain_locked_inputs(AppContext *context, AppState locked_state)
{
    GameUserId ignored_id = {0};

    if (context == NULL) {
        return;
    }

    buttons_reset();
    if (read_rfid_card_once(&ignored_id)) {
        log_card_ignored(locked_state, &ignored_id);
        context->next_rfid_poll_ms = HAL_Millis() + RFID_POLL_DELAY_MS;
    }
    buttons_reset();
}

static void app_handle_button(AppContext *context, GameMove move, uint32_t now)
{
    if (context == NULL) {
        return;
    }

    switch (context->state) {
    case APP_STATE_MENU:
        app_handle_menu_button(context, move);
        break;
    case APP_STATE_WAIT_MOVE:
        app_handle_game_button(context, move, now);
        break;
    default:
        log_button_ignored(context->state, move);
        break;
    }
}

static void app_handle_menu_button(AppContext *context, GameMove move)
{
    uint8_t old_index;

    if (context == NULL) {
        return;
    }

    old_index = context->menu_index;
    log_button_accepted_action(menu_action_name(move));

    if (context->menu_user_count == 0u) {
        return;
    }

    switch (move) {
    case GAME_MOVE_ROCK:
        if (context->menu_index > 0u) {
            context->menu_index -= 1u;
        }
        break;
    case GAME_MOVE_SCISSORS:
        if ((uint8_t)(context->menu_index + 1u) < context->menu_user_count) {
            context->menu_index += 1u;
        }
        break;
    case GAME_MOVE_PAPER:
        context->menu_index = 0u;
        break;
    default:
        break;
    }

    if (context->menu_index != old_index) {
        app_show_menu(context);
    }
}

static void app_handle_game_button(AppContext *context, GameMove player_move, uint32_t now)
{
    if (context == NULL) {
        return;
    }

    if (!context->user_active) {
        app_enter_menu(context);
        return;
    }

    log_button_accepted_move(player_move);
    context->last_player_move = player_move;
    context->last_round = game_stage(&context->user, player_move);
    print_round_result(player_move, &context->last_round);
    app_enter_chant(context, now);
}

static void app_handle_card(AppContext *context, const GameUserId *card_id, uint32_t now)
{
    if ((context == NULL) || (card_id == NULL)) {
        return;
    }

    log_card_accepted(card_id);

    if (!context->user_active) {
        (void)app_activate_user(context, card_id, now);
        return;
    }

    if (same_user_id(card_id, &context->user.id)) {
        if (app_save_active_user(context)) {
            xprintf("user returned menu reason=same_card\r\n");
            app_enter_menu(context);
        }
        return;
    }

    if (!app_save_active_user(context)) {
        return;
    }

    if (!app_activate_user(context, card_id, now)) {
        app_enter_menu(context);
    }
}

static bool app_activate_user(AppContext *context, const GameUserId *card_id, uint32_t now)
{
    GameStatus status;

    if ((context == NULL) || (card_id == NULL)) {
        return false;
    }

    status = game_init_user(card_id, &context->user);
    print_init_status(status, &context->user);

    if (status_is_fatal(status)) {
        return false;
    }

    context->user_active = true;
    app_enter_welcome(context, now);
    return true;
}

static bool app_save_active_user(AppContext *context)
{
    GameStatus status;

    if ((context == NULL) || !context->user_active) {
        return false;
    }

    status = save_user(&context->user);
    return !status_is_fatal(status);
}

static void app_load_menu_users(AppContext *context)
{
    GameStatus status;

    if (context == NULL) {
        return;
    }

    context->menu_user_count = 0u;

    status = game_storage_list_users(context->menu_users, GAME_EEPROM_SLOT_COUNT, &context->menu_user_count);
    if (status != GAME_STATUS_OK) {
        xprintf("menu list status=%s\r\n", status_name(status));
        context->menu_user_count = 0u;
        return;
    }

    app_sort_menu_users(context);
    app_clamp_menu_index(context);
}

static void app_sort_menu_users(AppContext *context)
{
    uint8_t index;

    if (context == NULL) {
        return;
    }

    for (index = 1u; index < context->menu_user_count; ++index) {
        GameUser key = context->menu_users[index];
        uint8_t insert_index = index;

        while ((insert_index > 0u) && app_menu_user_before(&key, &context->menu_users[insert_index - 1u])) {
            context->menu_users[insert_index] = context->menu_users[insert_index - 1u];
            insert_index -= 1u;
        }
        context->menu_users[insert_index] = key;
    }
}

static bool app_menu_user_before(const GameUser *left, const GameUser *right)
{
    uint32_t left_win_pct;
    uint32_t right_win_pct;
    uint32_t left_draw_pct;
    uint32_t right_draw_pct;
    uint16_t left_hash;
    uint16_t right_hash;

    if ((left == NULL) || (right == NULL)) {
        return false;
    }

    if (left->stats.rounds != right->stats.rounds) {
        return left->stats.rounds > right->stats.rounds;
    }

    left_win_pct = percent_u32(left->stats.wins, left->stats.rounds);
    right_win_pct = percent_u32(right->stats.wins, right->stats.rounds);
    if (left_win_pct != right_win_pct) {
        return left_win_pct > right_win_pct;
    }

    left_draw_pct = percent_u32(left->stats.draws, left->stats.rounds);
    right_draw_pct = percent_u32(right->stats.draws, right->stats.rounds);
    if (left_draw_pct != right_draw_pct) {
        return left_draw_pct > right_draw_pct;
    }

    left_hash = uid_hash16(&left->id);
    right_hash = uid_hash16(&right->id);
    return left_hash < right_hash;
}

static void app_show_menu(const AppContext *context)
{
    if (context == NULL) {
        return;
    }

    if (context->menu_user_count == 0u) {
        game_lcd_show_menu_empty();
        return;
    }

    game_lcd_show_menu_user(&context->menu_users[context->menu_index],
                            context->menu_index,
                            context->menu_user_count);
}

static void app_clamp_menu_index(AppContext *context)
{
    if (context == NULL) {
        return;
    }

    if (context->menu_user_count == 0u) {
        context->menu_index = 0u;
        return;
    }

    if (context->menu_index >= context->menu_user_count) {
        context->menu_index = (uint8_t)(context->menu_user_count - 1u);
    }
}

static bool app_state_accepts_rfid(AppState state)
{
    return (state == APP_STATE_MENU) || (state == APP_STATE_WAIT_MOVE);
}

static bool app_state_accepts_buttons(AppState state)
{
    return (state == APP_STATE_MENU) || (state == APP_STATE_WAIT_MOVE);
}

static bool app_state_is_locked(AppState state)
{
    return !app_state_accepts_rfid(state) && !app_state_accepts_buttons(state);
}

static bool time_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return ((int32_t)(now_ms - deadline_ms)) >= 0;
}

static uint32_t percent_u32(uint32_t value, uint32_t total)
{
    if (total == 0u) {
        return 0u;
    }

    return (uint32_t)(((uint64_t)value * 100u) / total);
}

static const char *chant_word(uint8_t step)
{
    static const char *const words[] = {
        "Rock",
        "Paper",
        "Scissors"
    };

    if (step >= (sizeof(words) / sizeof(words[0]))) {
        return "Scissors";
    }

    return words[step];
}

static const char *menu_action_name(GameMove move)
{
    switch (move) {
    case GAME_MOVE_ROCK:
        return "menu_up";
    case GAME_MOVE_PAPER:
        return "menu_reset";
    case GAME_MOVE_SCISSORS:
        return "menu_down";
    default:
        return "menu_unknown";
    }
}

static void log_card_accepted(const GameUserId *id)
{
    xprintf("card accepted hash=0x%04X uid=", (unsigned int)uid_hash16(id));
    print_user_id(id);
    xprintf("\r\n");
}

static void log_card_ignored(AppState state, const GameUserId *id)
{
    xprintf("card ignored state=%s hash=0x%04X uid=",
            state_name(state),
            (unsigned int)uid_hash16(id));
    print_user_id(id);
    xprintf("\r\n");
}

static void log_button_accepted_action(const char *action)
{
    xprintf("button accepted action=%s\r\n", (action != NULL) ? action : "unknown");
}

static void log_button_accepted_move(GameMove move)
{
    xprintf("button accepted move=%s\r\n", move_name(move));
}

static void log_button_ignored(AppState state, GameMove move)
{
    if (!app_state_is_locked(state)) {
        return;
    }

    xprintf("button ignored state=%s move=%s\r\n", state_name(state), move_name(move));
}

static void print_round_result(GameMove player_move, const GameRound *round)
{
    if (round == NULL) {
        return;
    }

    xprintf("round you=%s bot=%s result=%s\r\n",
            move_name(player_move),
            move_name(round->device_move),
            result_name(round->result));
}

void trap_handler(void)
{
    if (EPIC_CHECK_GPIO_IRQ()) {
        buttons_handle_irq();
    }

    HAL_EPIC_Clear(0xFFFFFFFF);
}

static void SystemClock_Config(void)
{
    PCC_InitTypeDef pcc = {0};

    pcc.OscillatorEnable = PCC_OSCILLATORTYPE_ALL;
    pcc.FreqMon.OscillatorSystem = PCC_OSCILLATORTYPE_OSC32M;
    pcc.FreqMon.ForceOscSys = PCC_FORCE_OSC_SYS_UNFIXED;
    pcc.FreqMon.Force32KClk = PCC_FREQ_MONITOR_SOURCE_OSC32K;
    pcc.AHBDivider = 0;
    pcc.APBMDivider = 0;
    pcc.APBPDivider = 0;
    pcc.HSI32MCalibrationValue = 128;
    pcc.LSI32KCalibrationValue = 8;
    pcc.RTCClockSelection = PCC_RTC_CLOCK_SOURCE_AUTO;
    pcc.RTCClockCPUSelection = PCC_CPU_RTC_CLOCK_SOURCE_OSC32K;

    HAL_PCC_Config(&pcc);
}

static void USART0_Init(void)
{
    USART_HandleTypeDef husart0 = {0};

    husart0.Instance = UART_0;
    husart0.transmitting = Enable;
    husart0.receiving = Disable;
    husart0.xck_mode = XCK_Mode3;
    husart0.baudrate = UART_BAUDRATE;

    (void)HAL_USART_Init(&husart0);
}

static void I2C1_Init(void)
{
    hi2c = (I2C_HandleTypeDef){0};
    hi2c.Instance = I2C_1;
    hi2c.Init.Mode = HAL_I2C_MODE_MASTER;
    hi2c.Init.DigitalFilter = I2C_DIGITALFILTER_OFF;
    hi2c.Init.AnalogFilter = I2C_ANALOGFILTER_DISABLE;
    hi2c.Init.AutoEnd = I2C_AUTOEND_ENABLE;
    hi2c.Clock.PRESC = 5;
    hi2c.Clock.SCLDEL = 15;
    hi2c.Clock.SDADEL = 15;
    hi2c.Clock.SCLH = 15;
    hi2c.Clock.SCLL = 15;

    if (HAL_I2C_Init(&hi2c) != HAL_OK) {
        xprintf("I2C_Init_Error\r\n");
    }
}

static void SPI0_Init(void)
{
    hspi0 = (SPI_HandleTypeDef){0};
    hspi0.Instance = RFID_SPI_INSTANCE;
    hspi0.Init.SPI_Mode = HAL_SPI_MODE_MASTER;
    hspi0.Init.CLKPhase = RFID_SPI_PHASE;
    hspi0.Init.CLKPolarity = RFID_SPI_POLARITY;
    hspi0.Init.ThresholdTX = 1;
    hspi0.Init.BaudRateDiv = RFID_SPI_BAUDRATE_DIV;
    hspi0.Init.Decoder = SPI_DECODER_NONE;
    hspi0.Init.ManualCS = SPI_MANUALCS_ON;
    hspi0.Init.ChipSelect = SPI_CS_NONE;

    if (HAL_SPI_Init(&hspi0) != HAL_OK) {
        xprintf("SPI_Init_Error\r\n");
    }
}

static bool read_rfid_card_once(GameUserId *id)
{
    if (id == NULL) {
        return false;
    }

    if (!PICC_IsNewCardPresent(&mfrc522)) {
        return false;
    }

    if (!PICC_ReadCardSerial(&mfrc522)) {
        return false;
    }

    copy_rfid_uid_to_user_id(&mfrc522.uid, id);
    (void)PICC_HaltA(&mfrc522);

    return id->length > 0u;
}

static void copy_rfid_uid_to_user_id(const Uid *uid, GameUserId *id)
{
    uint8_t index;

    if (id == NULL) {
        return;
    }

    id->length = 0u;
    for (index = 0u; index < GAME_USER_ID_MAX_BYTES; ++index) {
        id->bytes[index] = 0u;
    }

    if ((uid == NULL) || (uid->size == 0u) || (uid->size > GAME_USER_ID_MAX_BYTES)) {
        return;
    }

    id->length = uid->size;
    for (index = 0u; index < id->length; ++index) {
        id->bytes[index] = uid->uidByte[index];
    }
}

static bool same_user_id(const GameUserId *left, const GameUserId *right)
{
    uint8_t index;

    if ((left == NULL) || (right == NULL) || (left->length != right->length)) {
        return false;
    }

    for (index = 0u; index < left->length; ++index) {
        if (left->bytes[index] != right->bytes[index]) {
            return false;
        }
    }

    return true;
}

static void print_init_status(GameStatus status, const GameUser *user)
{
    if (user == NULL) {
        return;
    }

    xprintf("user init status=%s hash=0x%04X uid=",
            status_name(status),
            (unsigned int)uid_hash16(&user->id));
    print_user_id(&user->id);
    xprintf(" loaded=%u sessions=%lu rounds=%lu\r\n",
            user->loaded_from_storage ? 1u : 0u,
            (unsigned long)user->sessions_played,
            (unsigned long)user->stats.rounds);
}

static void print_save_status(GameStatus status, const GameUser *user)
{
    if (user == NULL) {
        return;
    }

    if ((status == GAME_STATUS_CRC_ERROR) && !user->dirty) {
        xprintf("save status=ok warning=crc_reused hash=0x%04X uid=", (unsigned int)uid_hash16(&user->id));
    } else if ((status == GAME_STATUS_STORAGE_FULL) && !user->dirty) {
        xprintf("save status=ok warning=evicted_old_user hash=0x%04X uid=", (unsigned int)uid_hash16(&user->id));
    } else {
        xprintf("save status=%s hash=0x%04X uid=", status_name(status), (unsigned int)uid_hash16(&user->id));
    }
    print_user_id(&user->id);
    xprintf(" sessions=%lu dirty=%u\r\n",
            (unsigned long)user->sessions_played,
            user->dirty ? 1u : 0u);
}

static void print_user_id(const GameUserId *id)
{
    uint8_t index;

    if (id == NULL) {
        return;
    }

    for (index = 0u; index < id->length; ++index) {
        if (index != 0u) {
            xprintf(" ");
        }
        xprintf("%02X", id->bytes[index]);
    }
}

static const char *move_name(GameMove move)
{
    static const char *const names[MOVE_COUNT] = {
        "rock",
        "paper",
        "scissors"
    };

    if ((uint8_t)move >= MOVE_COUNT) {
        return "unknown";
    }

    return names[(uint8_t)move];
}

static const char *result_name(GameRoundResult result)
{
    static const char *const names[MOVE_COUNT] = {
        "loss",
        "draw",
        "win"
    };

    if ((uint8_t)result >= MOVE_COUNT) {
        return "unknown";
    }

    return names[(uint8_t)result];
}

static const char *state_name(AppState state)
{
    switch (state) {
    case APP_STATE_SPLASH:
        return "splash";
    case APP_STATE_MENU:
        return "menu";
    case APP_STATE_WELCOME:
        return "welcome";
    case APP_STATE_WAIT_MOVE:
        return "wait_move";
    case APP_STATE_CHANT:
        return "chant";
    case APP_STATE_DUEL:
        return "duel";
    case APP_STATE_RESULT:
        return "result";
    default:
        return "unknown";
    }
}

static const char *status_name(GameStatus status)
{
    switch (status) {
    case GAME_STATUS_OK:
        return "ok";
    case GAME_STATUS_NOT_FOUND:
        return "not_found";
    case GAME_STATUS_INVALID_ARG:
        return "invalid_arg";
    case GAME_STATUS_STORAGE_FULL:
        return "storage_full";
    case GAME_STATUS_STORAGE_ERROR:
        return "storage_error";
    case GAME_STATUS_CRC_ERROR:
        return "crc_error";
    default:
        return "unknown";
    }
}

static bool status_is_fatal(GameStatus status)
{
    return (status == GAME_STATUS_INVALID_ARG) || (status == GAME_STATUS_STORAGE_ERROR);
}

static GameStatus save_user(GameUser *user)
{
    GameStatus status = game_save_user(user);

    print_save_status(status, user);
    return status;
}
