#include "game.h"

#include "game_storage.h"
#include "model_weights.h"

#include <string.h>

#define GAME_MOVE_COUNT 3u

_Static_assert(MODEL_WINDOW <= QUANT_MODEL_MAX_WINDOW,
               "MODEL.window must fit QuantModelHistory");

static bool user_id_is_valid(const GameUserId *id);
static void user_id_copy(GameUserId *dst, const GameUserId *src);
static void init_history(QuantModelHistory *history);
static void init_new_user(const GameUserId *id, GameUser *user);
static void init_loaded_user(GameUser *user, const GameUser *loaded);
static GameMove counter_move(uint8_t predicted_move);
static GameRoundResult round_result(GameMove player_move, GameMove device_move);
static void update_stats(GameStats *stats, GameRoundResult result);
static bool save_completed(GameStatus status);

GameStatus game_init_user(const GameUserId *id, GameUser *user)
{
    GameUser loaded = {0};
    GameStatus status;

    if (!user_id_is_valid(id) || (user == NULL)) {
        return GAME_STATUS_INVALID_ARG;
    }

    status = game_storage_load_user(id, &loaded);
    if (status == GAME_STATUS_OK)
        init_loaded_user(user, &loaded);
    else
        init_new_user(id, user);
    return status;
}

GameRound game_stage(GameUser *user, GameMove player_move)
{
    GameRound round = {
        .device_move = GAME_MOVE_ROCK,
        .result = GAME_ROUND_DRAW
    };

    if (user == NULL) {
        return round;
    }

    round.device_move = counter_move(quant_model_predict(&MODEL, &user->model_history));
    round.result = round_result(player_move, round.device_move);

    update_stats(&user->stats, round.result);
    quant_model_history_add_move(&MODEL, &user->model_history, (uint8_t)player_move);
    quant_model_history_add_result(&MODEL, &user->model_history, (uint8_t)round.result);
    user->dirty = true;

    return round;
}

GameStatus game_save_user(GameUser *user)
{
    uint32_t next_session;
    GameStatus status;

    if ((user == NULL) || !user_id_is_valid(&user->id)) {
        return GAME_STATUS_INVALID_ARG;
    }

    if (!user->dirty) {
        return GAME_STATUS_OK;
    }

    next_session = user->sessions_played + 1u;
    status = game_storage_save_user(user, next_session);

    if (save_completed(status)) {
        user->sessions_played = next_session;
        user->dirty = false;
        user->loaded_from_storage = true;
    }

    return status;
}

static bool user_id_is_valid(const GameUserId *id)
{
    return (id != NULL) && (id->length > 0u) && (id->length <= GAME_USER_ID_MAX_BYTES);
}

static void user_id_copy(GameUserId *dst, const GameUserId *src)
{
    memset(dst->bytes, 0, sizeof(dst->bytes));
    dst->length = src->length;
    memcpy(dst->bytes, src->bytes, src->length);
}

static void init_history(QuantModelHistory *history)
{
    memset(history, 0, sizeof(*history));
    quant_model_history_init(&MODEL, history, (uint8_t)GAME_MOVE_ROCK, (uint8_t)GAME_ROUND_DRAW);
}

static void init_new_user(const GameUserId *id, GameUser *user)
{
    memset(user, 0, sizeof(*user));
    user_id_copy(&user->id, id);
    init_history(&user->model_history);
}

static void init_loaded_user(GameUser *user, const GameUser *loaded)
{
    *user = *loaded;
    user->dirty = false;
    user->loaded_from_storage = true;
}

static GameMove counter_move(uint8_t predicted_move)
{
    return (GameMove)((predicted_move + 1u) % GAME_MOVE_COUNT);
}

static GameRoundResult round_result(GameMove player_move, GameMove device_move)
{
    if (player_move == device_move) {
        return GAME_ROUND_DRAW;
    }

    if ((((uint8_t)player_move + 1u) % GAME_MOVE_COUNT) == (uint8_t)device_move) {
        return GAME_ROUND_PLAYER_LOSS;
    }

    return GAME_ROUND_PLAYER_WIN;
}

static void update_stats(GameStats *stats, GameRoundResult result)
{
    stats->rounds += 1u;

    switch (result) {
    case GAME_ROUND_PLAYER_WIN:
        stats->wins += 1u;
        break;
    case GAME_ROUND_DRAW:
        stats->draws += 1u;
        break;
    case GAME_ROUND_PLAYER_LOSS:
        stats->losses += 1u;
        break;
    default:
        break;
    }
}

static bool save_completed(GameStatus status)
{
    return (status == GAME_STATUS_OK) ||
           (status == GAME_STATUS_STORAGE_FULL) ||
           (status == GAME_STATUS_CRC_ERROR);
}
