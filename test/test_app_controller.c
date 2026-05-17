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

static AppController controller;
static AppControllerPorts ports;
static AppControllerTiming timing;
static UiCall ui_calls[MAX_UI_CALLS];
static uint8_t ui_call_count;
static LogCall log_calls[MAX_LOG_CALLS];
static uint8_t log_call_count;
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
static void log_callback(AppLogEvent event, const AppLogData *data);
static void record_ui(UiEvent event);

void setUp(void)
{
    memset(&controller, 0, sizeof(controller));
    memset(ui_calls, 0, sizeof(ui_calls));
    memset(log_calls, 0, sizeof(log_calls));
    memset(listed_users, 0, sizeof(listed_users));
    memset(&init_user_result, 0, sizeof(init_user_result));
    memset(&last_init_id, 0, sizeof(last_init_id));

    ui_call_count = 0u;
    log_call_count = 0u;
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

    TEST_ASSERT_TRUE(app_controller_tick(&controller, 221u, &unlocked_from));
    TEST_ASSERT_EQUAL_INT(APP_CONTROLLER_STATE_RESULT, unlocked_from);
    TEST_ASSERT_EQUAL_INT(APP_CONTROLLER_STATE_WAIT_MOVE, app_controller_state(&controller));
    TEST_ASSERT_EQUAL_INT(UI_WAIT_MOVE, last_ui_call()->event);
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
