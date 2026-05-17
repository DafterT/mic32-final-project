#ifndef APP_CONTROLLER_APP_CONTROLLER_H
#define APP_CONTROLLER_APP_CONTROLLER_H

#include "game.h"
#include "game_storage_format.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    APP_CONTROLLER_STATE_SPLASH = 0u,
    APP_CONTROLLER_STATE_MENU,
    APP_CONTROLLER_STATE_WELCOME,
    APP_CONTROLLER_STATE_WAIT_MOVE,
    APP_CONTROLLER_STATE_CHANT,
    APP_CONTROLLER_STATE_DUEL,
    APP_CONTROLLER_STATE_RESULT
} AppControllerState;

typedef enum {
    APP_MENU_ACTION_UP = 0u,
    APP_MENU_ACTION_RESET,
    APP_MENU_ACTION_DOWN,
    APP_MENU_ACTION_UNKNOWN
} AppMenuAction;

typedef enum {
    APP_SOUND_INTRO = 0u,
    APP_SOUND_MENU_BACK,
    APP_SOUND_CHANT,
    APP_SOUND_LOSE,
    APP_SOUND_DRAW,
    APP_SOUND_WIN
} AppSound;

typedef enum {
    APP_LOG_GAME_START = 0u,
    APP_LOG_MENU_LIST_STATUS,
    APP_LOG_CARD_ACCEPTED,
    APP_LOG_CARD_IGNORED,
    APP_LOG_BUTTON_ACCEPTED_ACTION,
    APP_LOG_BUTTON_ACCEPTED_MOVE,
    APP_LOG_BUTTON_IGNORED,
    APP_LOG_ROUND_RESULT,
    APP_LOG_USER_INIT,
    APP_LOG_SAVE_STATUS,
    APP_LOG_RETURNED_MENU_SAME_CARD
} AppLogEvent;

typedef struct {
    uint32_t splash_ms;
    uint32_t welcome_ms;
    uint32_t chant_step_ms;
    uint32_t duel_ms;
    uint32_t result_ms;
} AppControllerTiming;

typedef struct {
    const GameUserId *card_id;
    const GameUser *user;
    AppControllerState state;
    AppMenuAction action;
    GameMove move;
    GameRound round;
    GameStatus status;
} AppLogData;

typedef GameStatus (*AppListUsersFn)(GameUser *users, uint8_t capacity, uint8_t *count);
typedef GameStatus (*AppInitUserFn)(const GameUserId *id, GameUser *user);
typedef GameRound (*AppStageFn)(GameUser *user, GameMove player_move);
typedef GameStatus (*AppSaveUserFn)(GameUser *user);
typedef void (*AppShowSplashFn)(void);
typedef void (*AppShowMenuEmptyFn)(void);
typedef void (*AppShowMenuUserFn)(const GameUser *user, uint8_t index, uint8_t count);
typedef void (*AppShowUserFn)(const GameUser *user);
typedef void (*AppShowChantFn)(const char *word);
typedef void (*AppShowDuelFn)(GameMove player_move, GameMove bot_move);
typedef void (*AppShowResultFn)(GameRoundResult result, const GameUser *user);
typedef void (*AppPlaySoundFn)(AppSound sound, uint32_t now_ms);
typedef void (*AppLogFn)(AppLogEvent event, const AppLogData *data);

typedef struct {
    AppListUsersFn list_users;
    AppInitUserFn init_user;
    AppStageFn stage;
    AppSaveUserFn save_user;
    AppShowSplashFn show_splash;
    AppShowMenuEmptyFn show_menu_empty;
    AppShowMenuUserFn show_menu_user;
    AppShowUserFn show_welcome;
    AppShowUserFn show_wait_move;
    AppShowChantFn show_chant;
    AppShowDuelFn show_duel;
    AppShowResultFn show_result;
    AppPlaySoundFn play_sound;
    AppLogFn log;
} AppControllerPorts;

typedef struct {
    AppControllerPorts ports;
    AppControllerTiming timing;
    GameUser user;
    GameUser menu_users[GAME_EEPROM_SLOT_COUNT];
    uint8_t menu_user_count;
    uint8_t menu_index;
    bool user_active;
    AppControllerState state;
    uint32_t state_until_ms;
    uint8_t chant_step;
    GameMove last_player_move;
    GameRound last_round;
} AppController;

void app_controller_init(AppController *controller,
                         const AppControllerPorts *ports,
                         const AppControllerTiming *timing,
                         uint32_t now_ms);
bool app_controller_tick(AppController *controller, uint32_t now_ms, AppControllerState *unlocked_from);
void app_controller_handle_card(AppController *controller, const GameUserId *card_id, uint32_t now_ms);
void app_controller_handle_ignored_card(AppController *controller,
                                        AppControllerState ignored_state,
                                        const GameUserId *card_id);
void app_controller_handle_button(AppController *controller, GameMove move, uint32_t now_ms);
bool app_controller_accepts_rfid(const AppController *controller);
bool app_controller_accepts_buttons(const AppController *controller);
bool app_controller_is_locked(const AppController *controller);
AppControllerState app_controller_state(const AppController *controller);
uint8_t app_controller_menu_index(const AppController *controller);
uint8_t app_controller_menu_count(const AppController *controller);
const GameUser *app_controller_active_user(const AppController *controller);
const GameUser *app_controller_menu_user(const AppController *controller, uint8_t index);
const char *app_controller_state_name(AppControllerState state);
const char *app_controller_menu_action_name(AppMenuAction action);
const char *app_controller_move_name(GameMove move);
const char *app_controller_result_name(GameRoundResult result);
const char *app_controller_status_name(GameStatus status);

#endif
