#include "app_controller.h"

#include "uid_hash.h"

#include <stdint.h>
#include <string.h>

#define APP_MOVE_COUNT 3u

static void app_enter_splash(AppController *controller, uint32_t now_ms);
static void app_enter_menu(AppController *controller);
static void app_enter_welcome(AppController *controller, uint32_t now_ms);
static void app_enter_wait_move(AppController *controller);
static void app_enter_chant(AppController *controller, uint32_t now_ms);
static void app_enter_duel(AppController *controller, uint32_t now_ms);
static void app_enter_result(AppController *controller, uint32_t now_ms);
static void app_update_state(AppController *controller, uint32_t now_ms);
static void app_handle_menu_button(AppController *controller, GameMove move);
static void app_handle_game_button(AppController *controller, GameMove player_move, uint32_t now_ms);
static bool app_activate_user(AppController *controller, const GameUserId *card_id, uint32_t now_ms);
static bool app_save_active_user(AppController *controller);
static void app_load_menu_users(AppController *controller);
static void app_sort_menu_users(AppController *controller);
static bool app_menu_user_before(const GameUser *left, const GameUser *right);
static void app_show_menu(const AppController *controller);
static void app_clamp_menu_index(AppController *controller);
static bool app_state_accepts_rfid(AppControllerState state);
static bool app_state_accepts_buttons(AppControllerState state);
static bool app_state_is_locked(AppControllerState state);
static bool time_reached(uint32_t now_ms, uint32_t deadline_ms);
static uint32_t percent_u32(uint32_t value, uint32_t total);
static const char *chant_word(uint8_t step);
static AppMenuAction menu_action(GameMove move);
static bool same_user_id(const GameUserId *left, const GameUserId *right);
static bool status_is_fatal(GameStatus status);
static GameRound default_round(void);
static GameStatus list_users(const AppController *controller, GameUser *users, uint8_t capacity, uint8_t *count);
static GameStatus init_user(const AppController *controller, const GameUserId *id, GameUser *user);
static GameRound stage_round(const AppController *controller, GameUser *user, GameMove move);
static GameStatus save_user(const AppController *controller, GameUser *user);
static void log_event(const AppController *controller, AppLogEvent event, const AppLogData *data);

void app_controller_init(AppController *controller,
                         const AppControllerPorts *ports,
                         const AppControllerTiming *timing,
                         uint32_t now_ms)
{
    if (controller == NULL) {
        return;
    }

    memset(controller, 0, sizeof(*controller));
    if (ports != NULL) {
        controller->ports = *ports;
    }
    if (timing != NULL) {
        controller->timing = *timing;
    }

    log_event(controller, APP_LOG_GAME_START, NULL);
    app_enter_splash(controller, now_ms);
}

bool app_controller_tick(AppController *controller, uint32_t now_ms, AppControllerState *unlocked_from)
{
    AppControllerState previous_state;
    bool previous_locked;

    if (controller == NULL) {
        return false;
    }

    previous_state = controller->state;
    previous_locked = app_state_is_locked(previous_state);
    app_update_state(controller, now_ms);

    if (previous_locked && !app_state_is_locked(controller->state)) {
        if (unlocked_from != NULL) {
            *unlocked_from = previous_state;
        }
        return true;
    }

    return false;
}

void app_controller_handle_card(AppController *controller, const GameUserId *card_id, uint32_t now_ms)
{
    AppLogData log = {0};

    if ((controller == NULL) || (card_id == NULL)) {
        return;
    }

    if (!app_state_accepts_rfid(controller->state)) {
        app_controller_handle_ignored_card(controller, controller->state, card_id);
        return;
    }

    log.card_id = card_id;
    log_event(controller, APP_LOG_CARD_ACCEPTED, &log);

    if (!controller->user_active) {
        (void)app_activate_user(controller, card_id, now_ms);
        return;
    }

    if (same_user_id(card_id, &controller->user.id)) {
        if (app_save_active_user(controller)) {
            log_event(controller, APP_LOG_RETURNED_MENU_SAME_CARD, NULL);
            app_enter_menu(controller);
        }
        return;
    }

    if (!app_save_active_user(controller)) {
        return;
    }
    if (!app_activate_user(controller, card_id, now_ms)) {
        app_enter_menu(controller);
    }
}

void app_controller_handle_ignored_card(AppController *controller,
                                        AppControllerState ignored_state,
                                        const GameUserId *card_id)
{
    AppLogData log = {0};

    if ((controller == NULL) || (card_id == NULL)) {
        return;
    }

    log.state = ignored_state;
    log.card_id = card_id;
    log_event(controller, APP_LOG_CARD_IGNORED, &log);
}

void app_controller_handle_button(AppController *controller, GameMove move, uint32_t now_ms)
{
    AppLogData log = {0};

    if (controller == NULL) {
        return;
    }

    if (!app_state_accepts_buttons(controller->state)) {
        if (app_state_is_locked(controller->state)) {
            log.state = controller->state;
            log.move = move;
            log_event(controller, APP_LOG_BUTTON_IGNORED, &log);
        }
        return;
    }

    switch (controller->state) {
    case APP_CONTROLLER_STATE_MENU:
        app_handle_menu_button(controller, move);
        break;
    case APP_CONTROLLER_STATE_WAIT_MOVE:
        app_handle_game_button(controller, move, now_ms);
        break;
    default:
        break;
    }
}

bool app_controller_accepts_rfid(const AppController *controller)
{
    return (controller != NULL) && app_state_accepts_rfid(controller->state);
}

bool app_controller_accepts_buttons(const AppController *controller)
{
    return (controller != NULL) && app_state_accepts_buttons(controller->state);
}

bool app_controller_is_locked(const AppController *controller)
{
    return (controller != NULL) && app_state_is_locked(controller->state);
}

AppControllerState app_controller_state(const AppController *controller)
{
    if (controller == NULL) {
        return APP_CONTROLLER_STATE_SPLASH;
    }

    return controller->state;
}

uint8_t app_controller_menu_index(const AppController *controller)
{
    return (controller != NULL) ? controller->menu_index : 0u;
}

uint8_t app_controller_menu_count(const AppController *controller)
{
    return (controller != NULL) ? controller->menu_user_count : 0u;
}

const GameUser *app_controller_active_user(const AppController *controller)
{
    if ((controller == NULL) || !controller->user_active) {
        return NULL;
    }

    return &controller->user;
}

const GameUser *app_controller_menu_user(const AppController *controller, uint8_t index)
{
    if ((controller == NULL) || (index >= controller->menu_user_count)) {
        return NULL;
    }

    return &controller->menu_users[index];
}

const char *app_controller_state_name(AppControllerState state)
{
    switch (state) {
    case APP_CONTROLLER_STATE_SPLASH:
        return "splash";
    case APP_CONTROLLER_STATE_MENU:
        return "menu";
    case APP_CONTROLLER_STATE_WELCOME:
        return "welcome";
    case APP_CONTROLLER_STATE_WAIT_MOVE:
        return "wait_move";
    case APP_CONTROLLER_STATE_CHANT:
        return "chant";
    case APP_CONTROLLER_STATE_DUEL:
        return "duel";
    case APP_CONTROLLER_STATE_RESULT:
        return "result";
    default:
        return "unknown";
    }
}

const char *app_controller_menu_action_name(AppMenuAction action)
{
    switch (action) {
    case APP_MENU_ACTION_UP:
        return "menu_up";
    case APP_MENU_ACTION_RESET:
        return "menu_reset";
    case APP_MENU_ACTION_DOWN:
        return "menu_down";
    case APP_MENU_ACTION_UNKNOWN:
    default:
        return "menu_unknown";
    }
}

const char *app_controller_move_name(GameMove move)
{
    static const char *const names[APP_MOVE_COUNT] = {
        "rock",
        "paper",
        "scissors"
    };

    if ((uint8_t)move >= APP_MOVE_COUNT) {
        return "unknown";
    }

    return names[(uint8_t)move];
}

const char *app_controller_result_name(GameRoundResult result)
{
    static const char *const names[APP_MOVE_COUNT] = {
        "loss",
        "draw",
        "win"
    };

    if ((uint8_t)result >= APP_MOVE_COUNT) {
        return "unknown";
    }

    return names[(uint8_t)result];
}

const char *app_controller_status_name(GameStatus status)
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

static void app_enter_splash(AppController *controller, uint32_t now_ms)
{
    if (controller == NULL) {
        return;
    }

    controller->state = APP_CONTROLLER_STATE_SPLASH;
    controller->state_until_ms = now_ms + controller->timing.splash_ms;
    if (controller->ports.show_splash != NULL) {
        controller->ports.show_splash();
    }
}

static void app_enter_menu(AppController *controller)
{
    if (controller == NULL) {
        return;
    }

    controller->state = APP_CONTROLLER_STATE_MENU;
    controller->user_active = false;
    controller->menu_index = 0u;
    app_load_menu_users(controller);
    app_show_menu(controller);
}

static void app_enter_welcome(AppController *controller, uint32_t now_ms)
{
    if (controller == NULL) {
        return;
    }

    controller->state = APP_CONTROLLER_STATE_WELCOME;
    controller->state_until_ms = now_ms + controller->timing.welcome_ms;
    if (controller->ports.show_welcome != NULL) {
        controller->ports.show_welcome(&controller->user);
    }
}

static void app_enter_wait_move(AppController *controller)
{
    if (controller == NULL) {
        return;
    }

    controller->state = APP_CONTROLLER_STATE_WAIT_MOVE;
    if (controller->ports.show_wait_move != NULL) {
        controller->ports.show_wait_move(&controller->user);
    }
}

static void app_enter_chant(AppController *controller, uint32_t now_ms)
{
    if (controller == NULL) {
        return;
    }

    controller->state = APP_CONTROLLER_STATE_CHANT;
    controller->chant_step = 0u;
    controller->state_until_ms = now_ms + controller->timing.chant_step_ms;
    if (controller->ports.show_chant != NULL) {
        controller->ports.show_chant(chant_word(controller->chant_step));
    }
}

static void app_enter_duel(AppController *controller, uint32_t now_ms)
{
    if (controller == NULL) {
        return;
    }

    controller->state = APP_CONTROLLER_STATE_DUEL;
    controller->state_until_ms = now_ms + controller->timing.duel_ms;
    if (controller->ports.show_duel != NULL) {
        controller->ports.show_duel(controller->last_player_move, controller->last_round.device_move);
    }
}

static void app_enter_result(AppController *controller, uint32_t now_ms)
{
    if (controller == NULL) {
        return;
    }

    controller->state = APP_CONTROLLER_STATE_RESULT;
    controller->state_until_ms = now_ms + controller->timing.result_ms;
    if (controller->ports.show_result != NULL) {
        controller->ports.show_result(controller->last_round.result, &controller->user);
    }
}

static void app_update_state(AppController *controller, uint32_t now_ms)
{
    if (controller == NULL) {
        return;
    }

    switch (controller->state) {
    case APP_CONTROLLER_STATE_SPLASH:
        if (time_reached(now_ms, controller->state_until_ms)) {
            app_enter_menu(controller);
        }
        break;
    case APP_CONTROLLER_STATE_WELCOME:
        if (time_reached(now_ms, controller->state_until_ms)) {
            app_enter_wait_move(controller);
        }
        break;
    case APP_CONTROLLER_STATE_CHANT:
        if (!time_reached(now_ms, controller->state_until_ms)) {
            break;
        }
        if (controller->chant_step < 2u) {
            controller->chant_step += 1u;
            controller->state_until_ms = now_ms + controller->timing.chant_step_ms;
            if (controller->ports.show_chant != NULL) {
                controller->ports.show_chant(chant_word(controller->chant_step));
            }
        } else {
            app_enter_duel(controller, now_ms);
        }
        break;
    case APP_CONTROLLER_STATE_DUEL:
        if (time_reached(now_ms, controller->state_until_ms)) {
            app_enter_result(controller, now_ms);
        }
        break;
    case APP_CONTROLLER_STATE_RESULT:
        if (time_reached(now_ms, controller->state_until_ms)) {
            app_enter_wait_move(controller);
        }
        break;
    case APP_CONTROLLER_STATE_MENU:
    case APP_CONTROLLER_STATE_WAIT_MOVE:
    default:
        break;
    }
}

static void app_handle_menu_button(AppController *controller, GameMove move)
{
    uint8_t old_index;
    AppLogData log = {0};

    if (controller == NULL) {
        return;
    }

    old_index = controller->menu_index;
    log.action = menu_action(move);
    log_event(controller, APP_LOG_BUTTON_ACCEPTED_ACTION, &log);

    if (controller->menu_user_count == 0u) {
        return;
    }

    switch (move) {
    case GAME_MOVE_ROCK:
        if (controller->menu_index > 0u) {
            controller->menu_index -= 1u;
        }
        break;
    case GAME_MOVE_SCISSORS:
        if ((uint8_t)(controller->menu_index + 1u) < controller->menu_user_count) {
            controller->menu_index += 1u;
        }
        break;
    case GAME_MOVE_PAPER:
        controller->menu_index = 0u;
        break;
    default:
        break;
    }

    if (controller->menu_index != old_index) {
        app_show_menu(controller);
    }
}

static void app_handle_game_button(AppController *controller, GameMove player_move, uint32_t now_ms)
{
    AppLogData log = {0};

    if (controller == NULL) {
        return;
    }
    if (!controller->user_active) {
        app_enter_menu(controller);
        return;
    }

    log.move = player_move;
    log_event(controller, APP_LOG_BUTTON_ACCEPTED_MOVE, &log);

    controller->last_player_move = player_move;
    controller->last_round = stage_round(controller, &controller->user, player_move);

    log.round = controller->last_round;
    log_event(controller, APP_LOG_ROUND_RESULT, &log);
    app_enter_chant(controller, now_ms);
}

static bool app_activate_user(AppController *controller, const GameUserId *card_id, uint32_t now_ms)
{
    GameStatus status;
    AppLogData log = {0};

    if ((controller == NULL) || (card_id == NULL)) {
        return false;
    }

    status = init_user(controller, card_id, &controller->user);

    log.status = status;
    log.user = &controller->user;
    log_event(controller, APP_LOG_USER_INIT, &log);

    if (status_is_fatal(status)) {
        return false;
    }

    controller->user_active = true;
    app_enter_welcome(controller, now_ms);
    return true;
}

static bool app_save_active_user(AppController *controller)
{
    GameStatus status;
    AppLogData log = {0};

    if ((controller == NULL) || !controller->user_active) {
        return false;
    }

    status = save_user(controller, &controller->user);

    log.status = status;
    log.user = &controller->user;
    log_event(controller, APP_LOG_SAVE_STATUS, &log);
    return !status_is_fatal(status);
}

static void app_load_menu_users(AppController *controller)
{
    GameStatus status;
    AppLogData log = {0};

    if (controller == NULL) {
        return;
    }
    controller->menu_user_count = 0u;

    status = list_users(controller, controller->menu_users, GAME_EEPROM_SLOT_COUNT, &controller->menu_user_count);
    if (status != GAME_STATUS_OK) {
        log.status = status;
        log_event(controller, APP_LOG_MENU_LIST_STATUS, &log);
        controller->menu_user_count = 0u;
        return;
    }

    app_sort_menu_users(controller);
    app_clamp_menu_index(controller);
}

static void app_sort_menu_users(AppController *controller)
{
    uint8_t index;

    if (controller == NULL) {
        return;
    }
    for (index = 1u; index < controller->menu_user_count; ++index) {
        GameUser key = controller->menu_users[index];
        uint8_t insert_index = index;

        while ((insert_index > 0u) && app_menu_user_before(&key, &controller->menu_users[insert_index - 1u])) {
            controller->menu_users[insert_index] = controller->menu_users[insert_index - 1u];
            insert_index -= 1u;
        }
        controller->menu_users[insert_index] = key;
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

static void app_show_menu(const AppController *controller)
{
    if (controller == NULL) {
        return;
    }
    if (controller->menu_user_count == 0u) {
        if (controller->ports.show_menu_empty != NULL) {
            controller->ports.show_menu_empty();
        }
        return;
    }

    if (controller->ports.show_menu_user != NULL) {
        controller->ports.show_menu_user(&controller->menu_users[controller->menu_index],
                                         controller->menu_index,
                                         controller->menu_user_count);
    }
}

static void app_clamp_menu_index(AppController *controller)
{
    if (controller == NULL) {
        return;
    }
    if (controller->menu_user_count == 0u) {
        controller->menu_index = 0u;
        return;
    }
    if (controller->menu_index >= controller->menu_user_count) {
        controller->menu_index = (uint8_t)(controller->menu_user_count - 1u);
    }
}

static bool app_state_accepts_rfid(AppControllerState state)
{
    return (state == APP_CONTROLLER_STATE_MENU) || (state == APP_CONTROLLER_STATE_WAIT_MOVE);
}

static bool app_state_accepts_buttons(AppControllerState state)
{
    return (state == APP_CONTROLLER_STATE_MENU) || (state == APP_CONTROLLER_STATE_WAIT_MOVE);
}

static bool app_state_is_locked(AppControllerState state)
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

static AppMenuAction menu_action(GameMove move)
{
    switch (move) {
    case GAME_MOVE_ROCK:
        return APP_MENU_ACTION_UP;
    case GAME_MOVE_PAPER:
        return APP_MENU_ACTION_RESET;
    case GAME_MOVE_SCISSORS:
        return APP_MENU_ACTION_DOWN;
    default:
        return APP_MENU_ACTION_UNKNOWN;
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

static bool status_is_fatal(GameStatus status)
{
    return (status == GAME_STATUS_INVALID_ARG) || (status == GAME_STATUS_STORAGE_ERROR);
}

static GameRound default_round(void)
{
    GameRound round = {
        .device_move = GAME_MOVE_ROCK,
        .result = GAME_ROUND_DRAW,
    };

    return round;
}

static GameStatus list_users(const AppController *controller, GameUser *users, uint8_t capacity, uint8_t *count)
{
    if ((controller == NULL) || (controller->ports.list_users == NULL)) {
        if (count != NULL) {
            *count = 0u;
        }
        return GAME_STATUS_STORAGE_ERROR;
    }

    return controller->ports.list_users(users, capacity, count);
}

static GameStatus init_user(const AppController *controller, const GameUserId *id, GameUser *user)
{
    if ((controller == NULL) || (controller->ports.init_user == NULL)) {
        return GAME_STATUS_STORAGE_ERROR;
    }

    return controller->ports.init_user(id, user);
}

static GameRound stage_round(const AppController *controller, GameUser *user, GameMove move)
{
    if ((controller == NULL) || (controller->ports.stage == NULL)) {
        return default_round();
    }

    return controller->ports.stage(user, move);
}

static GameStatus save_user(const AppController *controller, GameUser *user)
{
    if ((controller == NULL) || (controller->ports.save_user == NULL)) {
        return GAME_STATUS_STORAGE_ERROR;
    }

    return controller->ports.save_user(user);
}

static void log_event(const AppController *controller, AppLogEvent event, const AppLogData *data)
{
    if ((controller != NULL) && (controller->ports.log != NULL)) {
        controller->ports.log(event, data);
    }
}
