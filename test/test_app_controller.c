#include "unity.h"

#include "app_controller.h"
#include "uid_hash.h"

#include <string.h>

#define TEST_TIMING_SPLASH_MS     10u
#define TEST_TIMING_WELCOME_MS    5u
#define TEST_TIMING_CHANT_STEP_MS 3u
#define TEST_TIMING_DUEL_MS       7u
#define TEST_TIMING_RESULT_MS     5u
#define MAX_UI_CALLS              64u
#define MAX_LOG_CALLS             64u
#define MAX_SOUND_CALLS           32u

typedef enum {
    UI_SPLASH = 0u,
    UI_MENU_EMPTY,
    UI_MENU_USER,
    UI_WELCOME,
    UI_WAIT_MOVE,
    UI_CHANT,
    UI_DUEL,
    UI_RESULT
} UiEvent;

typedef struct {
    UiEvent event;
    GameUser user;
    uint8_t index;
    uint8_t count;
    const char *word;
    GameMove player_move;
    GameMove bot_move;
    GameRoundResult result;
} UiCall;

typedef struct {
    AppLogEvent event;
    AppLogData data;
    GameUserId card_id;
    GameUser user;
} LogCall;

typedef struct {
    AppSound sound;
    uint32_t now_ms;
} SoundCall;

static AppController controller;
static AppControllerPorts ports;
static AppControllerTiming timing;
static UiCall ui_calls[MAX_UI_CALLS];
static uint8_t ui_call_count;
static LogCall log_calls[MAX_LOG_CALLS];
static uint8_t log_call_count;
static SoundCall sound_calls[MAX_SOUND_CALLS];
static uint8_t sound_call_count;
static GameUser listed_users[GAME_EEPROM_SLOT_COUNT];
static uint8_t listed_user_count;
static GameStatus list_status;
static uint8_t list_calls;
static GameStatus init_status;
static GameUser init_user_result;
static uint8_t init_calls;
static GameUserId last_init_id;
static GameRound stage_result;
static uint8_t stage_calls;
static GameMove last_stage_move;
static GameStatus save_status;
static uint8_t save_calls;

static GameUserId make_id(uint8_t seed);
static GameUser make_user(uint8_t seed, uint32_t rounds, uint32_t wins, uint32_t draws, uint32_t losses);
static void start_and_open_menu(void);
static void activate_user_and_enter_wait(uint8_t seed);
static const UiCall *last_ui_call(void);
static const LogCall *last_log_call(void);
static GameStatus list_users_callback(GameUser *users, uint8_t capacity, uint8_t *count);
static GameStatus init_user_callback(const GameUserId *id, GameUser *user);
static GameRound stage_callback(GameUser *user, GameMove player_move);
static GameStatus save_user_callback(GameUser *user);
static void show_splash_callback(void);
static void show_menu_empty_callback(void);
static void show_menu_user_callback(const GameUser *user, uint8_t index, uint8_t count);
static void show_welcome_callback(const GameUser *user);
static void show_wait_move_callback(const GameUser *user);
static void show_chant_callback(const char *word);
static void show_duel_callback(GameMove player_move, GameMove bot_move);
static void show_result_callback(GameRoundResult result, const GameUser *user);
static void play_sound_callback(AppSound sound, uint32_t now_ms);
static void log_callback(AppLogEvent event, const AppLogData *data);
static void record_ui(UiEvent event);
static const SoundCall *last_sound_call(void);

void setUp(void)
{
    memset(&controller, 0, sizeof(controller));
    memset(ui_calls, 0, sizeof(ui_calls));
    memset(log_calls, 0, sizeof(log_calls));
    memset(sound_calls, 0, sizeof(sound_calls));
    memset(listed_users, 0, sizeof(listed_users));
    memset(&init_user_result, 0, sizeof(init_user_result));
    memset(&last_init_id, 0, sizeof(last_init_id));

    ui_call_count = 0u;
    log_call_count = 0u;
    sound_call_count = 0u;
    listed_user_count = 0u;
    list_status = GAME_STATUS_OK;
    list_calls = 0u;
    init_status = GAME_STATUS_OK;
    init_calls = 0u;
    stage_result.device_move = GAME_MOVE_SCISSORS;
    stage_result.result = GAME_ROUND_PLAYER_WIN;
    stage_calls = 0u;
    last_stage_move = GAME_MOVE_ROCK;
    save_status = GAME_STATUS_OK;
    save_calls = 0u;

    timing.splash_ms = TEST_TIMING_SPLASH_MS;
    timing.welcome_ms = TEST_TIMING_WELCOME_MS;
    timing.chant_step_ms = TEST_TIMING_CHANT_STEP_MS;
    timing.duel_ms = TEST_TIMING_DUEL_MS;
    timing.result_ms = TEST_TIMING_RESULT_MS;

    ports.list_users = list_users_callback;
    ports.init_user = init_user_callback;
    ports.stage = stage_callback;
    ports.save_user = save_user_callback;
    ports.show_splash = show_splash_callback;
    ports.show_menu_empty = show_menu_empty_callback;
    ports.show_menu_user = show_menu_user_callback;
    ports.show_welcome = show_welcome_callback;
    ports.show_wait_move = show_wait_move_callback;
    ports.show_chant = show_chant_callback;
    ports.show_duel = show_duel_callback;
    ports.show_result = show_result_callback;
    ports.play_sound = play_sound_callback;
    ports.log = log_callback;
}

void tearDown(void)
{
}

void test_app_controller_starts_with_splash_and_opens_empty_menu_after_timeout(void)
{
    AppControllerState unlocked_from = APP_CONTROLLER_STATE_RESULT;

    app_controller_init(&controller, &ports, &timing, 100u);

    TEST_ASSERT_EQUAL_INT(APP_CONTROLLER_STATE_SPLASH, app_controller_state(&controller));
    TEST_ASSERT_TRUE(app_controller_is_locked(&controller));
    TEST_ASSERT_EQUAL_UINT8(1u, ui_call_count);
    TEST_ASSERT_EQUAL_INT(UI_SPLASH, ui_calls[0].event);
    TEST_ASSERT_EQUAL_UINT8(1u, log_call_count);
    TEST_ASSERT_EQUAL_INT(APP_LOG_GAME_START, log_calls[0].event);

    TEST_ASSERT_FALSE(app_controller_tick(&controller, 109u, &unlocked_from));
    TEST_ASSERT_EQUAL_INT(APP_CONTROLLER_STATE_SPLASH, app_controller_state(&controller));

    TEST_ASSERT_TRUE(app_controller_tick(&controller, 110u, &unlocked_from));

    TEST_ASSERT_EQUAL_INT(APP_CONTROLLER_STATE_SPLASH, unlocked_from);
    TEST_ASSERT_EQUAL_INT(APP_CONTROLLER_STATE_MENU, app_controller_state(&controller));
    TEST_ASSERT_FALSE(app_controller_is_locked(&controller));
    TEST_ASSERT_EQUAL_UINT8(1u, list_calls);
    TEST_ASSERT_EQUAL_INT(UI_MENU_EMPTY, last_ui_call()->event);
    TEST_ASSERT_EQUAL_UINT8(0u, app_controller_menu_count(&controller));
    TEST_ASSERT_EQUAL_UINT8(1u, sound_call_count);
    TEST_ASSERT_EQUAL_INT(APP_SOUND_INTRO, sound_calls[0u].sound);
    TEST_ASSERT_EQUAL_UINT32(100u, sound_calls[0u].now_ms);
}

void test_app_controller_plays_menu_back_sound_for_accepted_card(void)
{
    GameUserId id = make_id(0x21u);

    start_and_open_menu();
    sound_call_count = 0u;
    init_user_result = make_user(0x21u, 0u, 0u, 0u, 0u);

    app_controller_handle_card(&controller, &id, 110u);

    TEST_ASSERT_EQUAL_UINT8(1u, sound_call_count);
    TEST_ASSERT_EQUAL_INT(APP_SOUND_MENU_BACK, last_sound_call()->sound);
    TEST_ASSERT_EQUAL_UINT32(110u, last_sound_call()->now_ms);
}

void test_app_controller_sorts_menu_and_navigates_with_buttons(void)
{
    listed_user_count = 4u;
    listed_users[0] = make_user(1u, 2u, 2u, 0u, 0u);
    listed_users[1] = make_user(2u, 4u, 2u, 0u, 2u);
    listed_users[2] = make_user(3u, 4u, 1u, 3u, 0u);
    listed_users[3] = make_user(4u, 4u, 2u, 1u, 1u);

    start_and_open_menu();

    TEST_ASSERT_EQUAL_UINT8(4u, app_controller_menu_count(&controller));
    TEST_ASSERT_EQUAL_UINT8(4u, app_controller_menu_user(&controller, 0u)->id.bytes[0]);
    TEST_ASSERT_EQUAL_UINT8(2u, app_controller_menu_user(&controller, 1u)->id.bytes[0]);
    TEST_ASSERT_EQUAL_UINT8(3u, app_controller_menu_user(&controller, 2u)->id.bytes[0]);
    TEST_ASSERT_EQUAL_UINT8(1u, app_controller_menu_user(&controller, 3u)->id.bytes[0]);

    app_controller_handle_button(&controller, GAME_MOVE_SCISSORS, 11u);
    TEST_ASSERT_EQUAL_UINT8(1u, app_controller_menu_index(&controller));
    TEST_ASSERT_EQUAL_UINT8(2u, last_ui_call()->user.id.bytes[0]);

    app_controller_handle_button(&controller, GAME_MOVE_ROCK, 12u);
    TEST_ASSERT_EQUAL_UINT8(0u, app_controller_menu_index(&controller));
    TEST_ASSERT_EQUAL_UINT8(4u, last_ui_call()->user.id.bytes[0]);

    app_controller_handle_button(&controller, GAME_MOVE_SCISSORS, 13u);
    app_controller_handle_button(&controller, GAME_MOVE_SCISSORS, 14u);
    TEST_ASSERT_EQUAL_UINT8(2u, app_controller_menu_index(&controller));

    app_controller_handle_button(&controller, GAME_MOVE_PAPER, 15u);
    TEST_ASSERT_EQUAL_UINT8(0u, app_controller_menu_index(&controller));
    TEST_ASSERT_EQUAL_UINT8(4u, last_ui_call()->user.id.bytes[0]);
    TEST_ASSERT_EQUAL_INT(APP_LOG_BUTTON_ACCEPTED_ACTION, last_log_call()->event);
    TEST_ASSERT_EQUAL_INT(APP_MENU_ACTION_RESET, last_log_call()->data.action);
}

void test_app_controller_accepts_card_then_shows_welcome_and_wait_move(void)
{
    GameUserId id = make_id(0x20u);
    AppControllerState unlocked_from = APP_CONTROLLER_STATE_SPLASH;

    start_and_open_menu();
    init_user_result = make_user(0x20u, 6u, 3u, 1u, 2u);

    app_controller_handle_card(&controller, &id, 110u);

    TEST_ASSERT_EQUAL_INT(APP_CONTROLLER_STATE_WELCOME, app_controller_state(&controller));
    TEST_ASSERT_EQUAL_UINT8(1u, init_calls);
    TEST_ASSERT_EQUAL_UINT8(0x20u, last_init_id.bytes[0]);
    TEST_ASSERT_EQUAL_INT(UI_WELCOME, last_ui_call()->event);
    TEST_ASSERT_EQUAL_UINT8(0x20u, last_ui_call()->user.id.bytes[0]);

    TEST_ASSERT_TRUE(app_controller_tick(&controller, 115u, &unlocked_from));

    TEST_ASSERT_EQUAL_INT(APP_CONTROLLER_STATE_WELCOME, unlocked_from);
    TEST_ASSERT_EQUAL_INT(APP_CONTROLLER_STATE_WAIT_MOVE, app_controller_state(&controller));
    TEST_ASSERT_EQUAL_INT(UI_WAIT_MOVE, last_ui_call()->event);
}

void test_app_controller_runs_round_animation_and_returns_to_wait_move(void)
{
    AppControllerState unlocked_from = APP_CONTROLLER_STATE_SPLASH;

    activate_user_and_enter_wait(0x30u);
    stage_result.device_move = GAME_MOVE_SCISSORS;
    stage_result.result = GAME_ROUND_PLAYER_WIN;

    app_controller_handle_button(&controller, GAME_MOVE_ROCK, 200u);

    TEST_ASSERT_EQUAL_INT(APP_CONTROLLER_STATE_CHANT, app_controller_state(&controller));
    TEST_ASSERT_EQUAL_UINT8(1u, stage_calls);
    TEST_ASSERT_EQUAL_INT(GAME_MOVE_ROCK, last_stage_move);
    TEST_ASSERT_EQUAL_INT(UI_CHANT, last_ui_call()->event);
    TEST_ASSERT_EQUAL_STRING("Rock", last_ui_call()->word);
    TEST_ASSERT_EQUAL_INT(APP_SOUND_CHANT, last_sound_call()->sound);
    TEST_ASSERT_EQUAL_UINT32(200u, last_sound_call()->now_ms);

    TEST_ASSERT_FALSE(app_controller_tick(&controller, 203u, &unlocked_from));
    TEST_ASSERT_EQUAL_STRING("Paper", last_ui_call()->word);

    TEST_ASSERT_FALSE(app_controller_tick(&controller, 206u, &unlocked_from));
    TEST_ASSERT_EQUAL_STRING("Scissors", last_ui_call()->word);

    TEST_ASSERT_FALSE(app_controller_tick(&controller, 209u, &unlocked_from));
    TEST_ASSERT_EQUAL_INT(APP_CONTROLLER_STATE_DUEL, app_controller_state(&controller));
    TEST_ASSERT_EQUAL_INT(UI_DUEL, last_ui_call()->event);
    TEST_ASSERT_EQUAL_INT(GAME_MOVE_ROCK, last_ui_call()->player_move);
    TEST_ASSERT_EQUAL_INT(GAME_MOVE_SCISSORS, last_ui_call()->bot_move);

    TEST_ASSERT_FALSE(app_controller_tick(&controller, 216u, &unlocked_from));
    TEST_ASSERT_EQUAL_INT(APP_CONTROLLER_STATE_RESULT, app_controller_state(&controller));
    TEST_ASSERT_EQUAL_INT(UI_RESULT, last_ui_call()->event);
    TEST_ASSERT_EQUAL_INT(GAME_ROUND_PLAYER_WIN, last_ui_call()->result);
    TEST_ASSERT_EQUAL_UINT32(1u, last_ui_call()->user.stats.wins);
    TEST_ASSERT_EQUAL_INT(APP_SOUND_WIN, last_sound_call()->sound);
    TEST_ASSERT_EQUAL_UINT32(216u, last_sound_call()->now_ms);

    TEST_ASSERT_TRUE(app_controller_tick(&controller, 221u, &unlocked_from));
    TEST_ASSERT_EQUAL_INT(APP_CONTROLLER_STATE_RESULT, unlocked_from);
    TEST_ASSERT_EQUAL_INT(APP_CONTROLLER_STATE_WAIT_MOVE, app_controller_state(&controller));
    TEST_ASSERT_EQUAL_INT(UI_WAIT_MOVE, last_ui_call()->event);
}

void test_app_controller_plays_result_sounds_for_loss_and_draw(void)
{
    AppControllerState unlocked_from = APP_CONTROLLER_STATE_SPLASH;

    activate_user_and_enter_wait(0x31u);
    sound_call_count = 0u;
    stage_result.result = GAME_ROUND_PLAYER_LOSS;

    app_controller_handle_button(&controller, GAME_MOVE_SCISSORS, 200u);
    app_controller_tick(&controller, 203u, &unlocked_from);
    app_controller_tick(&controller, 206u, &unlocked_from);
    app_controller_tick(&controller, 209u, &unlocked_from);
    app_controller_tick(&controller, 216u, &unlocked_from);

    TEST_ASSERT_EQUAL_INT(APP_SOUND_LOSE, last_sound_call()->sound);
    TEST_ASSERT_EQUAL_UINT32(216u, last_sound_call()->now_ms);

    TEST_ASSERT_TRUE(app_controller_tick(&controller, 221u, &unlocked_from));
    stage_result.result = GAME_ROUND_DRAW;

    app_controller_handle_button(&controller, GAME_MOVE_PAPER, 230u);
    app_controller_tick(&controller, 233u, &unlocked_from);
    app_controller_tick(&controller, 236u, &unlocked_from);
    app_controller_tick(&controller, 239u, &unlocked_from);
    app_controller_tick(&controller, 246u, &unlocked_from);

    TEST_ASSERT_EQUAL_INT(APP_SOUND_DRAW, last_sound_call()->sound);
    TEST_ASSERT_EQUAL_UINT32(246u, last_sound_call()->now_ms);
}

void test_app_controller_same_card_saves_and_returns_to_menu(void)
{
    GameUserId id = make_id(0x40u);

    activate_user_and_enter_wait(0x40u);
    app_controller_handle_card(&controller, &id, 220u);

    TEST_ASSERT_EQUAL_UINT8(1u, save_calls);
    TEST_ASSERT_EQUAL_INT(APP_CONTROLLER_STATE_MENU, app_controller_state(&controller));
    TEST_ASSERT_NULL(app_controller_active_user(&controller));
    TEST_ASSERT_EQUAL_INT(UI_MENU_EMPTY, last_ui_call()->event);
    TEST_ASSERT_EQUAL_INT(APP_LOG_RETURNED_MENU_SAME_CARD, last_log_call()->event);
}

void test_app_controller_logs_ignored_card_with_supplied_locked_state(void)
{
    GameUserId id = make_id(0x50u);

    app_controller_init(&controller, &ports, &timing, 0u);
    app_controller_handle_ignored_card(&controller, APP_CONTROLLER_STATE_SPLASH, &id);

    TEST_ASSERT_EQUAL_INT(APP_LOG_CARD_IGNORED, last_log_call()->event);
    TEST_ASSERT_EQUAL_INT(APP_CONTROLLER_STATE_SPLASH, last_log_call()->data.state);
    TEST_ASSERT_EQUAL_UINT8(0x50u, last_log_call()->card_id.bytes[0]);
}

void test_app_controller_logs_button_ignored_during_welcome(void)
{
    GameUserId id = make_id(0x60u);

    start_and_open_menu();
    init_user_result = make_user(0x60u, 0u, 0u, 0u, 0u);
    app_controller_handle_card(&controller, &id, 110u);
    app_controller_handle_button(&controller, GAME_MOVE_PAPER, 111u);

    TEST_ASSERT_EQUAL_INT(APP_CONTROLLER_STATE_WELCOME, app_controller_state(&controller));
    TEST_ASSERT_EQUAL_INT(APP_LOG_BUTTON_IGNORED, last_log_call()->event);
    TEST_ASSERT_EQUAL_INT(APP_CONTROLLER_STATE_WELCOME, last_log_call()->data.state);
    TEST_ASSERT_EQUAL_INT(GAME_MOVE_PAPER, last_log_call()->data.move);
}

void test_app_controller_handle_card_logs_ignored_when_locked(void)
{
    GameUserId id = make_id(0x61u);

    app_controller_init(&controller, &ports, &timing, 0u);
    app_controller_handle_card(&controller, &id, 1u);

    TEST_ASSERT_EQUAL_INT(APP_CONTROLLER_STATE_SPLASH, app_controller_state(&controller));
    TEST_ASSERT_EQUAL_UINT8(0u, init_calls);
    TEST_ASSERT_EQUAL_INT(APP_LOG_CARD_IGNORED, last_log_call()->event);
    TEST_ASSERT_EQUAL_INT(APP_CONTROLLER_STATE_SPLASH, last_log_call()->data.state);
    TEST_ASSERT_EQUAL_UINT8(0x61u, last_log_call()->card_id.bytes[0]);
}

void test_app_controller_menu_list_error_logs_status_and_shows_empty_menu(void)
{
    AppControllerState unlocked_from = APP_CONTROLLER_STATE_RESULT;

    list_status = GAME_STATUS_STORAGE_ERROR;
    app_controller_init(&controller, &ports, &timing, 0u);

    TEST_ASSERT_TRUE(app_controller_tick(&controller, TEST_TIMING_SPLASH_MS, &unlocked_from));

    TEST_ASSERT_EQUAL_INT(APP_CONTROLLER_STATE_MENU, app_controller_state(&controller));
    TEST_ASSERT_EQUAL_UINT8(1u, list_calls);
    TEST_ASSERT_EQUAL_UINT8(0u, app_controller_menu_count(&controller));
    TEST_ASSERT_EQUAL_INT(UI_MENU_EMPTY, last_ui_call()->event);
    TEST_ASSERT_EQUAL_INT(APP_LOG_MENU_LIST_STATUS, last_log_call()->event);
    TEST_ASSERT_EQUAL_INT(GAME_STATUS_STORAGE_ERROR, last_log_call()->data.status);
}

void test_app_controller_fatal_init_keeps_menu_without_active_user(void)
{
    GameUserId id = make_id(0x62u);

    start_and_open_menu();
    init_status = GAME_STATUS_INVALID_ARG;

    app_controller_handle_card(&controller, &id, 20u);

    TEST_ASSERT_EQUAL_INT(APP_CONTROLLER_STATE_MENU, app_controller_state(&controller));
    TEST_ASSERT_NULL(app_controller_active_user(&controller));
    TEST_ASSERT_EQUAL_UINT8(1u, init_calls);
    TEST_ASSERT_EQUAL_INT(APP_LOG_USER_INIT, last_log_call()->event);
    TEST_ASSERT_EQUAL_INT(GAME_STATUS_INVALID_ARG, last_log_call()->data.status);
    TEST_ASSERT_EQUAL_UINT8(0u, last_log_call()->user.id.length);
}

void test_app_controller_different_card_saves_old_user_and_activates_new_user(void)
{
    GameUserId new_id = make_id(0x71u);
    const GameUser *active;

    activate_user_and_enter_wait(0x70u);
    init_user_result = make_user(0x71u, 4u, 2u, 1u, 1u);

    app_controller_handle_card(&controller, &new_id, 220u);

    active = app_controller_active_user(&controller);
    TEST_ASSERT_EQUAL_UINT8(1u, save_calls);
    TEST_ASSERT_EQUAL_UINT8(2u, init_calls);
    TEST_ASSERT_EQUAL_UINT8(0x71u, last_init_id.bytes[0]);
    TEST_ASSERT_EQUAL_INT(APP_CONTROLLER_STATE_WELCOME, app_controller_state(&controller));
    TEST_ASSERT_TRUE(active != NULL);
    TEST_ASSERT_EQUAL_UINT8(0x71u, active->id.bytes[0]);
    TEST_ASSERT_EQUAL_INT(UI_WELCOME, last_ui_call()->event);
}

void test_app_controller_different_card_stays_on_old_user_when_save_is_fatal(void)
{
    GameUserId new_id = make_id(0x73u);
    const GameUser *active;

    activate_user_and_enter_wait(0x72u);
    save_status = GAME_STATUS_STORAGE_ERROR;
    init_user_result = make_user(0x73u, 1u, 1u, 0u, 0u);

    app_controller_handle_card(&controller, &new_id, 230u);

    active = app_controller_active_user(&controller);
    TEST_ASSERT_EQUAL_UINT8(1u, save_calls);
    TEST_ASSERT_EQUAL_UINT8(1u, init_calls);
    TEST_ASSERT_EQUAL_INT(APP_CONTROLLER_STATE_WAIT_MOVE, app_controller_state(&controller));
    TEST_ASSERT_TRUE(active != NULL);
    TEST_ASSERT_EQUAL_UINT8(0x72u, active->id.bytes[0]);
    TEST_ASSERT_EQUAL_INT(APP_LOG_SAVE_STATUS, last_log_call()->event);
    TEST_ASSERT_EQUAL_INT(GAME_STATUS_STORAGE_ERROR, last_log_call()->data.status);
}

void test_app_controller_different_card_returns_menu_when_new_user_init_is_fatal(void)
{
    GameUserId new_id = make_id(0x75u);

    activate_user_and_enter_wait(0x74u);
    init_status = GAME_STATUS_STORAGE_ERROR;

    app_controller_handle_card(&controller, &new_id, 240u);

    TEST_ASSERT_EQUAL_UINT8(1u, save_calls);
    TEST_ASSERT_EQUAL_UINT8(2u, init_calls);
    TEST_ASSERT_EQUAL_INT(APP_CONTROLLER_STATE_MENU, app_controller_state(&controller));
    TEST_ASSERT_NULL(app_controller_active_user(&controller));
    TEST_ASSERT_EQUAL_INT(UI_MENU_EMPTY, last_ui_call()->event);
    TEST_ASSERT_EQUAL_INT(APP_LOG_USER_INIT, last_log_call()->event);
    TEST_ASSERT_EQUAL_INT(GAME_STATUS_STORAGE_ERROR, last_log_call()->data.status);
    TEST_ASSERT_EQUAL_UINT8(0u, last_log_call()->user.id.length);
}

void test_app_controller_same_card_allows_crc_save_warning_to_return_menu(void)
{
    GameUserId id = make_id(0x76u);

    activate_user_and_enter_wait(0x76u);
    save_status = GAME_STATUS_CRC_ERROR;

    app_controller_handle_card(&controller, &id, 250u);

    TEST_ASSERT_EQUAL_UINT8(1u, save_calls);
    TEST_ASSERT_EQUAL_INT(APP_CONTROLLER_STATE_MENU, app_controller_state(&controller));
    TEST_ASSERT_NULL(app_controller_active_user(&controller));
    TEST_ASSERT_EQUAL_INT(APP_LOG_RETURNED_MENU_SAME_CARD, last_log_call()->event);
}

void test_app_controller_same_card_allows_storage_full_save_warning_to_return_menu(void)
{
    GameUserId id = make_id(0x77u);

    activate_user_and_enter_wait(0x77u);
    save_status = GAME_STATUS_STORAGE_FULL;

    app_controller_handle_card(&controller, &id, 260u);

    TEST_ASSERT_EQUAL_UINT8(1u, save_calls);
    TEST_ASSERT_EQUAL_INT(APP_CONTROLLER_STATE_MENU, app_controller_state(&controller));
    TEST_ASSERT_NULL(app_controller_active_user(&controller));
    TEST_ASSERT_EQUAL_INT(APP_LOG_RETURNED_MENU_SAME_CARD, last_log_call()->event);
}

void test_app_controller_ignores_invalid_button_move_without_staging(void)
{
    uint8_t previous_ui_calls;
    uint8_t previous_log_calls;

    activate_user_and_enter_wait(0x78u);
    previous_ui_calls = ui_call_count;
    previous_log_calls = log_call_count;

    app_controller_handle_button(&controller, (GameMove)99u, 270u);

    TEST_ASSERT_EQUAL_INT(APP_CONTROLLER_STATE_WAIT_MOVE, app_controller_state(&controller));
    TEST_ASSERT_EQUAL_UINT8(0u, stage_calls);
    TEST_ASSERT_EQUAL_UINT8(previous_ui_calls, ui_call_count);
    TEST_ASSERT_EQUAL_UINT8(previous_log_calls, log_call_count);
}

void test_app_controller_menu_buttons_do_not_refresh_when_index_stays_same(void)
{
    uint8_t previous_ui_calls;

    listed_user_count = 1u;
    listed_users[0] = make_user(0x79u, 1u, 1u, 0u, 0u);
    start_and_open_menu();

    previous_ui_calls = ui_call_count;
    app_controller_handle_button(&controller, GAME_MOVE_ROCK, 280u);
    TEST_ASSERT_EQUAL_UINT8(0u, app_controller_menu_index(&controller));
    TEST_ASSERT_EQUAL_UINT8(previous_ui_calls, ui_call_count);
    TEST_ASSERT_EQUAL_INT(APP_LOG_BUTTON_ACCEPTED_ACTION, last_log_call()->event);
    TEST_ASSERT_EQUAL_INT(APP_MENU_ACTION_UP, last_log_call()->data.action);

    app_controller_handle_button(&controller, GAME_MOVE_SCISSORS, 281u);
    TEST_ASSERT_EQUAL_UINT8(0u, app_controller_menu_index(&controller));
    TEST_ASSERT_EQUAL_UINT8(previous_ui_calls, ui_call_count);
    TEST_ASSERT_EQUAL_INT(APP_LOG_BUTTON_ACCEPTED_ACTION, last_log_call()->event);
    TEST_ASSERT_EQUAL_INT(APP_MENU_ACTION_DOWN, last_log_call()->data.action);
}

void test_app_controller_empty_menu_accepts_button_without_ui_refresh(void)
{
    uint8_t previous_ui_calls;

    start_and_open_menu();
    previous_ui_calls = ui_call_count;

    app_controller_handle_button(&controller, GAME_MOVE_SCISSORS, 290u);

    TEST_ASSERT_EQUAL_UINT8(0u, app_controller_menu_count(&controller));
    TEST_ASSERT_EQUAL_UINT8(0u, app_controller_menu_index(&controller));
    TEST_ASSERT_EQUAL_UINT8(previous_ui_calls, ui_call_count);
    TEST_ASSERT_EQUAL_INT(APP_LOG_BUTTON_ACCEPTED_ACTION, last_log_call()->event);
    TEST_ASSERT_EQUAL_INT(APP_MENU_ACTION_DOWN, last_log_call()->data.action);
}

void test_app_controller_tick_reports_unlock_without_output_pointer(void)
{
    app_controller_init(&controller, &ports, &timing, 0u);

    TEST_ASSERT_TRUE(app_controller_tick(&controller, TEST_TIMING_SPLASH_MS, NULL));
    TEST_ASSERT_EQUAL_INT(APP_CONTROLLER_STATE_MENU, app_controller_state(&controller));
}

void test_app_controller_timing_handles_millisecond_wraparound(void)
{
    AppControllerState unlocked_from = APP_CONTROLLER_STATE_RESULT;

    app_controller_init(&controller, &ports, &timing, 0xFFFFFFFBu);

    TEST_ASSERT_FALSE(app_controller_tick(&controller, 4u, &unlocked_from));
    TEST_ASSERT_EQUAL_INT(APP_CONTROLLER_STATE_SPLASH, app_controller_state(&controller));

    TEST_ASSERT_TRUE(app_controller_tick(&controller, 5u, &unlocked_from));
    TEST_ASSERT_EQUAL_INT(APP_CONTROLLER_STATE_SPLASH, unlocked_from);
    TEST_ASSERT_EQUAL_INT(APP_CONTROLLER_STATE_MENU, app_controller_state(&controller));
}

void test_app_controller_stage_missing_uses_default_round(void)
{
    activate_user_and_enter_wait(0x7Au);
    ports.stage = NULL;
    controller.ports.stage = NULL;

    app_controller_handle_button(&controller, GAME_MOVE_PAPER, 300u);

    TEST_ASSERT_EQUAL_UINT8(0u, stage_calls);
    TEST_ASSERT_EQUAL_INT(APP_CONTROLLER_STATE_CHANT, app_controller_state(&controller));
    TEST_ASSERT_EQUAL_INT(APP_LOG_ROUND_RESULT, last_log_call()->event);
    TEST_ASSERT_EQUAL_INT(GAME_MOVE_ROCK, last_log_call()->data.round.device_move);
    TEST_ASSERT_EQUAL_INT(GAME_ROUND_DRAW, last_log_call()->data.round.result);
}

void test_app_controller_sorts_zero_round_users_by_uid_hash(void)
{
    uint8_t index;

    listed_user_count = 3u;
    listed_users[0] = make_user(0x82u, 0u, 0u, 0u, 0u);
    listed_users[1] = make_user(0x80u, 0u, 0u, 0u, 0u);
    listed_users[2] = make_user(0x81u, 0u, 0u, 0u, 0u);

    start_and_open_menu();

    TEST_ASSERT_EQUAL_UINT8(3u, app_controller_menu_count(&controller));
    for (index = 1u; index < app_controller_menu_count(&controller); ++index) {
        const GameUser *previous = app_controller_menu_user(&controller, (uint8_t)(index - 1u));
        const GameUser *current = app_controller_menu_user(&controller, index);

        TEST_ASSERT_TRUE(uid_hash16(&previous->id) <= uid_hash16(&current->id));
    }
}

void test_app_controller_accessors_return_safe_defaults(void)
{
    GameUserId id = make_id(0x83u);
    AppControllerState unlocked_from = APP_CONTROLLER_STATE_RESULT;

    TEST_ASSERT_FALSE(app_controller_tick(NULL, 0u, &unlocked_from));
    app_controller_handle_card(NULL, &id, 0u);
    app_controller_handle_ignored_card(NULL, APP_CONTROLLER_STATE_SPLASH, &id);
    app_controller_handle_button(NULL, GAME_MOVE_ROCK, 0u);

    TEST_ASSERT_FALSE(app_controller_accepts_rfid(NULL));
    TEST_ASSERT_FALSE(app_controller_accepts_buttons(NULL));
    TEST_ASSERT_FALSE(app_controller_is_locked(NULL));
    TEST_ASSERT_EQUAL_INT(APP_CONTROLLER_STATE_SPLASH, app_controller_state(NULL));
    TEST_ASSERT_EQUAL_UINT8(0u, app_controller_menu_index(NULL));
    TEST_ASSERT_EQUAL_UINT8(0u, app_controller_menu_count(NULL));
    TEST_ASSERT_NULL(app_controller_active_user(NULL));
    TEST_ASSERT_NULL(app_controller_menu_user(NULL, 0u));

    listed_user_count = 1u;
    listed_users[0] = make_user(0x83u, 1u, 1u, 0u, 0u);
    start_and_open_menu();
    TEST_ASSERT_NULL(app_controller_menu_user(&controller, 1u));
}

void test_app_controller_name_helpers_return_expected_strings(void)
{
    TEST_ASSERT_EQUAL_STRING("splash", app_controller_state_name(APP_CONTROLLER_STATE_SPLASH));
    TEST_ASSERT_EQUAL_STRING("menu", app_controller_state_name(APP_CONTROLLER_STATE_MENU));
    TEST_ASSERT_EQUAL_STRING("welcome", app_controller_state_name(APP_CONTROLLER_STATE_WELCOME));
    TEST_ASSERT_EQUAL_STRING("wait_move", app_controller_state_name(APP_CONTROLLER_STATE_WAIT_MOVE));
    TEST_ASSERT_EQUAL_STRING("chant", app_controller_state_name(APP_CONTROLLER_STATE_CHANT));
    TEST_ASSERT_EQUAL_STRING("duel", app_controller_state_name(APP_CONTROLLER_STATE_DUEL));
    TEST_ASSERT_EQUAL_STRING("result", app_controller_state_name(APP_CONTROLLER_STATE_RESULT));
    TEST_ASSERT_EQUAL_STRING("unknown", app_controller_state_name((AppControllerState)99u));

    TEST_ASSERT_EQUAL_STRING("menu_up", app_controller_menu_action_name(APP_MENU_ACTION_UP));
    TEST_ASSERT_EQUAL_STRING("menu_reset", app_controller_menu_action_name(APP_MENU_ACTION_RESET));
    TEST_ASSERT_EQUAL_STRING("menu_down", app_controller_menu_action_name(APP_MENU_ACTION_DOWN));
    TEST_ASSERT_EQUAL_STRING("menu_unknown", app_controller_menu_action_name(APP_MENU_ACTION_UNKNOWN));
    TEST_ASSERT_EQUAL_STRING("menu_unknown", app_controller_menu_action_name((AppMenuAction)99u));

    TEST_ASSERT_EQUAL_STRING("rock", app_controller_move_name(GAME_MOVE_ROCK));
    TEST_ASSERT_EQUAL_STRING("paper", app_controller_move_name(GAME_MOVE_PAPER));
    TEST_ASSERT_EQUAL_STRING("scissors", app_controller_move_name(GAME_MOVE_SCISSORS));
    TEST_ASSERT_EQUAL_STRING("unknown", app_controller_move_name((GameMove)99u));

    TEST_ASSERT_EQUAL_STRING("loss", app_controller_result_name(GAME_ROUND_PLAYER_LOSS));
    TEST_ASSERT_EQUAL_STRING("draw", app_controller_result_name(GAME_ROUND_DRAW));
    TEST_ASSERT_EQUAL_STRING("win", app_controller_result_name(GAME_ROUND_PLAYER_WIN));
    TEST_ASSERT_EQUAL_STRING("unknown", app_controller_result_name((GameRoundResult)99u));

    TEST_ASSERT_EQUAL_STRING("ok", app_controller_status_name(GAME_STATUS_OK));
    TEST_ASSERT_EQUAL_STRING("not_found", app_controller_status_name(GAME_STATUS_NOT_FOUND));
    TEST_ASSERT_EQUAL_STRING("invalid_arg", app_controller_status_name(GAME_STATUS_INVALID_ARG));
    TEST_ASSERT_EQUAL_STRING("storage_full", app_controller_status_name(GAME_STATUS_STORAGE_FULL));
    TEST_ASSERT_EQUAL_STRING("storage_error", app_controller_status_name(GAME_STATUS_STORAGE_ERROR));
    TEST_ASSERT_EQUAL_STRING("crc_error", app_controller_status_name(GAME_STATUS_CRC_ERROR));
    TEST_ASSERT_EQUAL_STRING("unknown", app_controller_status_name((GameStatus)99u));
}

void test_app_controller_tolerates_missing_output_callbacks(void)
{
    AppControllerPorts no_output = ports;
    AppControllerState unlocked_from = APP_CONTROLLER_STATE_RESULT;

    no_output.show_splash = NULL;
    no_output.show_menu_empty = NULL;
    no_output.show_menu_user = NULL;
    no_output.log = NULL;

    app_controller_init(&controller, &no_output, &timing, 0u);

    TEST_ASSERT_EQUAL_UINT8(0u, ui_call_count);
    TEST_ASSERT_EQUAL_UINT8(0u, log_call_count);
    TEST_ASSERT_TRUE(app_controller_tick(&controller, TEST_TIMING_SPLASH_MS, &unlocked_from));
    TEST_ASSERT_EQUAL_INT(APP_CONTROLLER_STATE_MENU, app_controller_state(&controller));
    TEST_ASSERT_EQUAL_UINT8(0u, ui_call_count);
    TEST_ASSERT_EQUAL_UINT8(0u, log_call_count);
}

static GameUserId make_id(uint8_t seed)
{
    GameUserId id = {
        .bytes = {seed, (uint8_t)(seed + 1u), (uint8_t)(seed + 2u), (uint8_t)(seed + 3u)},
        .length = 4u,
    };

    return id;
}

static GameUser make_user(uint8_t seed, uint32_t rounds, uint32_t wins, uint32_t draws, uint32_t losses)
{
    GameUser user = {
        .id = {
            .bytes = {seed, (uint8_t)(seed + 1u), (uint8_t)(seed + 2u), (uint8_t)(seed + 3u)},
            .length = 4u,
        },
        .stats = {
            .rounds = rounds,
            .wins = wins,
            .draws = draws,
            .losses = losses,
        },
        .sessions_played = 1u,
        .dirty = false,
        .loaded_from_storage = true,
    };

    return user;
}

static void start_and_open_menu(void)
{
    AppControllerState unlocked_from = APP_CONTROLLER_STATE_RESULT;

    app_controller_init(&controller, &ports, &timing, 0u);
    TEST_ASSERT_TRUE(app_controller_tick(&controller, TEST_TIMING_SPLASH_MS, &unlocked_from));
    TEST_ASSERT_EQUAL_INT(APP_CONTROLLER_STATE_SPLASH, unlocked_from);
    TEST_ASSERT_EQUAL_INT(APP_CONTROLLER_STATE_MENU, app_controller_state(&controller));
}

static void activate_user_and_enter_wait(uint8_t seed)
{
    GameUserId id = make_id(seed);
    AppControllerState unlocked_from = APP_CONTROLLER_STATE_SPLASH;

    start_and_open_menu();
    init_user_result = make_user(seed, 0u, 0u, 0u, 0u);
    app_controller_handle_card(&controller, &id, 100u);
    TEST_ASSERT_EQUAL_INT(APP_CONTROLLER_STATE_WELCOME, app_controller_state(&controller));
    TEST_ASSERT_TRUE(app_controller_tick(&controller, 100u + TEST_TIMING_WELCOME_MS, &unlocked_from));
    TEST_ASSERT_EQUAL_INT(APP_CONTROLLER_STATE_WELCOME, unlocked_from);
    TEST_ASSERT_EQUAL_INT(APP_CONTROLLER_STATE_WAIT_MOVE, app_controller_state(&controller));
}

static const UiCall *last_ui_call(void)
{
    TEST_ASSERT_GREATER_THAN_UINT8(0u, ui_call_count);
    return &ui_calls[ui_call_count - 1u];
}

static const LogCall *last_log_call(void)
{
    TEST_ASSERT_GREATER_THAN_UINT8(0u, log_call_count);
    return &log_calls[log_call_count - 1u];
}

static const SoundCall *last_sound_call(void)
{
    TEST_ASSERT_GREATER_THAN_UINT8(0u, sound_call_count);
    return &sound_calls[sound_call_count - 1u];
}

static GameStatus list_users_callback(GameUser *users, uint8_t capacity, uint8_t *count)
{
    uint8_t index;

    ++list_calls;
    if ((users == NULL) || (count == NULL)) {
        return GAME_STATUS_INVALID_ARG;
    }
    if (list_status != GAME_STATUS_OK) {
        *count = 0u;
        return list_status;
    }

    *count = listed_user_count;
    if (*count > capacity) {
        *count = capacity;
    }
    for (index = 0u; index < *count; ++index) {
        users[index] = listed_users[index];
    }

    return GAME_STATUS_OK;
}

static GameStatus init_user_callback(const GameUserId *id, GameUser *user)
{
    ++init_calls;
    if ((id == NULL) || (user == NULL)) {
        return GAME_STATUS_INVALID_ARG;
    }

    last_init_id = *id;
    if (init_status == GAME_STATUS_OK) {
        *user = init_user_result;
    }
    return init_status;
}

static GameRound stage_callback(GameUser *user, GameMove player_move)
{
    ++stage_calls;
    last_stage_move = player_move;
    if (user != NULL) {
        user->stats.rounds += 1u;
        if (stage_result.result == GAME_ROUND_PLAYER_WIN) {
            user->stats.wins += 1u;
        } else if (stage_result.result == GAME_ROUND_DRAW) {
            user->stats.draws += 1u;
        } else if (stage_result.result == GAME_ROUND_PLAYER_LOSS) {
            user->stats.losses += 1u;
        }
        user->dirty = true;
    }

    return stage_result;
}

static GameStatus save_user_callback(GameUser *user)
{
    ++save_calls;
    if (user == NULL) {
        return GAME_STATUS_INVALID_ARG;
    }
    if (save_status == GAME_STATUS_OK) {
        user->dirty = false;
    }

    return save_status;
}

static void show_splash_callback(void)
{
    record_ui(UI_SPLASH);
}

static void show_menu_empty_callback(void)
{
    record_ui(UI_MENU_EMPTY);
}

static void show_menu_user_callback(const GameUser *user, uint8_t index, uint8_t count)
{
    record_ui(UI_MENU_USER);
    ui_calls[ui_call_count - 1u].index = index;
    ui_calls[ui_call_count - 1u].count = count;
    if (user != NULL) {
        ui_calls[ui_call_count - 1u].user = *user;
    }
}

static void show_welcome_callback(const GameUser *user)
{
    record_ui(UI_WELCOME);
    if (user != NULL) {
        ui_calls[ui_call_count - 1u].user = *user;
    }
}

static void show_wait_move_callback(const GameUser *user)
{
    record_ui(UI_WAIT_MOVE);
    if (user != NULL) {
        ui_calls[ui_call_count - 1u].user = *user;
    }
}

static void show_chant_callback(const char *word)
{
    record_ui(UI_CHANT);
    ui_calls[ui_call_count - 1u].word = word;
}

static void show_duel_callback(GameMove player_move, GameMove bot_move)
{
    record_ui(UI_DUEL);
    ui_calls[ui_call_count - 1u].player_move = player_move;
    ui_calls[ui_call_count - 1u].bot_move = bot_move;
}

static void show_result_callback(GameRoundResult result, const GameUser *user)
{
    record_ui(UI_RESULT);
    ui_calls[ui_call_count - 1u].result = result;
    if (user != NULL) {
        ui_calls[ui_call_count - 1u].user = *user;
    }
}

static void play_sound_callback(AppSound sound, uint32_t now_ms)
{
    TEST_ASSERT_LESS_THAN_UINT8(MAX_SOUND_CALLS, sound_call_count);
    sound_calls[sound_call_count].sound = sound;
    sound_calls[sound_call_count].now_ms = now_ms;
    ++sound_call_count;
}

static void log_callback(AppLogEvent event, const AppLogData *data)
{
    LogCall *call;

    TEST_ASSERT_LESS_THAN_UINT8(MAX_LOG_CALLS, log_call_count);
    call = &log_calls[log_call_count];
    call->event = event;
    if (data != NULL) {
        call->data = *data;
        if (data->card_id != NULL) {
            call->card_id = *data->card_id;
            call->data.card_id = &call->card_id;
        }
        if (data->user != NULL) {
            call->user = *data->user;
            call->data.user = &call->user;
        }
    }
    ++log_call_count;
}

static void record_ui(UiEvent event)
{
    TEST_ASSERT_LESS_THAN_UINT8(MAX_UI_CALLS, ui_call_count);
    ui_calls[ui_call_count].event = event;
    ++ui_call_count;
}
