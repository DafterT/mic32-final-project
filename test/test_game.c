#include "unity.h"

#include "mock_game_storage.h"
#include "mock_quant_model.h"

#include "game.h"

const QuantModel MODEL = {
    .window = 6u,
};

static GameUser load_user_result;
static GameStatus load_user_status;
static const GameUserId *load_user_expected_id;
static int load_user_calls;
static QuantModelHistory history_init_result;

void setUp(void)
{
}

void tearDown(void)
{
}

static GameUserId make_user_id(void)
{
    GameUserId id = {
        .bytes = {0xDEu, 0xADu, 0xBEu, 0xEFu},
        .length = 4u,
    };

    return id;
}

static QuantModelHistory make_history(uint8_t base)
{
    QuantModelHistory history = {
        .pending_move = (uint8_t)(base + 1u),
        .moves = {
            base, (uint8_t)(base + 1u), (uint8_t)(base + 2u),
            (uint8_t)(base + 3u), (uint8_t)(base + 4u), (uint8_t)(base + 5u),
        },
        .results = {
            (uint8_t)(base + 10u), (uint8_t)(base + 11u), (uint8_t)(base + 12u),
            (uint8_t)(base + 13u), (uint8_t)(base + 14u), (uint8_t)(base + 15u),
        },
    };

    return history;
}

static GameUser make_dirty_user(uint32_t sessions_played)
{
    GameUser user = {
        .id = {
            .bytes = {1u, 2u, 3u},
            .length = 3u,
        },
        .stats = {
            .rounds = 4u,
            .wins = 1u,
            .draws = 2u,
            .losses = 1u,
        },
        .model_history = {
            .pending_move = 0u,
            .moves = {0u, 1u, 2u, 0u, 1u, 2u},
            .results = {1u, 2u, 0u, 1u, 2u, 0u},
        },
        .sessions_played = sessions_played,
        .dirty = true,
        .loaded_from_storage = false,
    };

    return user;
}

static GameStatus game_storage_load_user_callback(const GameUserId *id, GameUser *user, int cmock_num_calls)
{
    (void)cmock_num_calls;

    load_user_calls++;
    TEST_ASSERT_EQUAL_MEMORY(load_user_expected_id, id, sizeof(*id));
    TEST_ASSERT_NOT_NULL(user);

    if (load_user_status == GAME_STATUS_OK) {
        *user = load_user_result;
    }

    return load_user_status;
}

static void quant_model_history_init_callback(
    const QuantModel *model,
    QuantModelHistory *history,
    uint8_t fill_move,
    uint8_t fill_result,
    int cmock_num_calls)
{
    (void)model;
    (void)fill_move;
    (void)fill_result;
    (void)cmock_num_calls;

    *history = history_init_result;
}

static void expect_load_user(const GameUserId *id, GameStatus status)
{
    load_user_expected_id = id;
    load_user_status = status;
    load_user_calls = 0;
    game_storage_load_user_StubWithCallback(game_storage_load_user_callback);
}

static void expect_history_init(QuantModelHistory *history, const QuantModelHistory *initialized_history)
{
    history_init_result = *initialized_history;
    quant_model_history_init_Expect(&MODEL, history, (uint8_t)GAME_MOVE_ROCK, (uint8_t)GAME_ROUND_DRAW);
    quant_model_history_init_AddCallback(quant_model_history_init_callback);
}

static void expect_stage_model_calls(
    QuantModelHistory *history,
    GameMove player_move,
    GameRoundResult result,
    uint8_t predicted_move)
{
    quant_model_predict_ExpectAndReturn(&MODEL, history, predicted_move);

    quant_model_history_add_move_Expect(&MODEL, history, (uint8_t)player_move);

    quant_model_history_add_result_Expect(&MODEL, history, (uint8_t)result);
}

void test_game_init_user_rejects_invalid_arguments(void)
{
    GameUserId id = make_user_id();
    GameUserId empty_id = {.length = 0u};
    GameUserId too_long_id = {.length = (uint8_t)(GAME_USER_ID_MAX_BYTES + 1u)};
    GameUser user = {0};

    TEST_ASSERT_EQUAL_INT(GAME_STATUS_INVALID_ARG, game_init_user(NULL, &user));
    TEST_ASSERT_EQUAL_INT(GAME_STATUS_INVALID_ARG, game_init_user(&id, NULL));
    TEST_ASSERT_EQUAL_INT(GAME_STATUS_INVALID_ARG, game_init_user(&empty_id, &user));
    TEST_ASSERT_EQUAL_INT(GAME_STATUS_INVALID_ARG, game_init_user(&too_long_id, &user));
}

void test_game_init_user_load_ok_uses_storage_user(void)
{
    GameUserId id = make_user_id();
    GameUser loaded = {
        .id = {
            .bytes = {0xDEu, 0xADu, 0xBEu, 0xEFu},
            .length = 4u,
        },
        .stats = {
            .rounds = 8u,
            .wins = 2u,
            .draws = 3u,
            .losses = 3u,
        },
        .model_history = {
            .pending_move = 2u,
            .moves = {2u, 1u, 0u, 2u, 1u, 0u},
            .results = {0u, 1u, 2u, 0u, 1u, 2u},
        },
        .sessions_played = 5u,
        .dirty = false,
        .loaded_from_storage = true,
    };
    GameUser user = {0};
    GameStatus status;

    load_user_result = loaded;
    expect_load_user(&id, GAME_STATUS_OK);

    status = game_init_user(&id, &user);

    TEST_ASSERT_EQUAL_INT(1, load_user_calls);
    TEST_ASSERT_EQUAL_INT(GAME_STATUS_OK, status);
    TEST_ASSERT_EQUAL_MEMORY(&loaded, &user, sizeof(user));
}

void test_game_init_user_not_found_initializes_new_user(void)
{
    GameUserId id = make_user_id();
    GameUser user = {.dirty = true, .loaded_from_storage = true, .sessions_played = 99u};
    QuantModelHistory initialized_history = make_history(20u);
    GameStatus status;

    expect_load_user(&id, GAME_STATUS_NOT_FOUND);
    expect_history_init(&user.model_history, &initialized_history);

    status = game_init_user(&id, &user);

    TEST_ASSERT_EQUAL_INT(1, load_user_calls);
    TEST_ASSERT_EQUAL_INT(GAME_STATUS_NOT_FOUND, status);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(id.bytes, user.id.bytes, id.length);
    TEST_ASSERT_EQUAL_UINT8(id.length, user.id.length);
    TEST_ASSERT_EQUAL_UINT32(0u, user.stats.rounds);
    TEST_ASSERT_EQUAL_UINT32(0u, user.stats.wins);
    TEST_ASSERT_EQUAL_UINT32(0u, user.stats.draws);
    TEST_ASSERT_EQUAL_UINT32(0u, user.stats.losses);
    TEST_ASSERT_EQUAL_UINT32(0u, user.sessions_played);
    TEST_ASSERT_FALSE(user.dirty);
    TEST_ASSERT_FALSE(user.loaded_from_storage);
    TEST_ASSERT_EQUAL_MEMORY(&initialized_history, &user.model_history, sizeof(user.model_history));
}

void test_game_stage_returns_default_round_for_null_user(void)
{
    GameRound round = game_stage(NULL, GAME_MOVE_SCISSORS);

    TEST_ASSERT_EQUAL_INT(GAME_MOVE_ROCK, round.device_move);
    TEST_ASSERT_EQUAL_INT(GAME_ROUND_DRAW, round.result);
}

TEST_CASE(GAME_MOVE_ROCK, GAME_MOVE_ROCK, GAME_MOVE_PAPER, GAME_ROUND_PLAYER_LOSS, 3u, 4u, 4u)
TEST_CASE(GAME_MOVE_PAPER, GAME_MOVE_ROCK, GAME_MOVE_PAPER, GAME_ROUND_DRAW, 3u, 5u, 3u)
TEST_CASE(GAME_MOVE_SCISSORS, GAME_MOVE_ROCK, GAME_MOVE_PAPER, GAME_ROUND_PLAYER_WIN, 4u, 4u, 3u)
void test_game_stage_counters_prediction_updates_stats_and_history(
    GameMove player_move,
    uint8_t predicted_move,
    GameMove expected_device_move,
    GameRoundResult expected_result,
    uint32_t expected_wins,
    uint32_t expected_draws,
    uint32_t expected_losses)
{
    GameUser user = make_dirty_user(2u);
    GameRound round;

    user.dirty = false;
    user.stats.rounds = 10u;
    user.stats.wins = 3u;
    user.stats.draws = 4u;
    user.stats.losses = 3u;

    expect_stage_model_calls(&user.model_history, player_move, expected_result, predicted_move);

    round = game_stage(&user, player_move);

    TEST_ASSERT_EQUAL_INT(expected_device_move, round.device_move);
    TEST_ASSERT_EQUAL_INT(expected_result, round.result);
    TEST_ASSERT_EQUAL_UINT32(11u, user.stats.rounds);
    TEST_ASSERT_EQUAL_UINT32(expected_wins, user.stats.wins);
    TEST_ASSERT_EQUAL_UINT32(expected_draws, user.stats.draws);
    TEST_ASSERT_EQUAL_UINT32(expected_losses, user.stats.losses);
    TEST_ASSERT_TRUE(user.dirty);
}

void test_game_save_user_rejects_invalid_user(void)
{
    GameUser invalid_user = make_dirty_user(0u);

    invalid_user.id.length = 0u;

    TEST_ASSERT_EQUAL_INT(GAME_STATUS_INVALID_ARG, game_save_user(NULL));
    TEST_ASSERT_EQUAL_INT(GAME_STATUS_INVALID_ARG, game_save_user(&invalid_user));
}

void test_game_save_user_clean_user_does_not_write_storage(void)
{
    GameUser user = make_dirty_user(3u);

    user.dirty = false;

    TEST_ASSERT_EQUAL_INT(GAME_STATUS_OK, game_save_user(&user));
    TEST_ASSERT_EQUAL_UINT32(3u, user.sessions_played);
    TEST_ASSERT_FALSE(user.dirty);
}

TEST_CASE(GAME_STATUS_OK, true)
TEST_CASE(GAME_STATUS_STORAGE_FULL, true)
TEST_CASE(GAME_STATUS_CRC_ERROR, true)
TEST_CASE(GAME_STATUS_STORAGE_ERROR, false)
void test_game_save_user_updates_dirty_state_for_storage_status(GameStatus storage_status, bool expected_persisted)
{
    GameUser user = make_dirty_user(7u);
    GameStatus status;

    game_storage_save_user_ExpectAndReturn(&user, 8u, storage_status);

    status = game_save_user(&user);

    TEST_ASSERT_EQUAL_INT(storage_status, status);
    TEST_ASSERT_EQUAL_UINT32(expected_persisted ? 8u : 7u, user.sessions_played);
    TEST_ASSERT_EQUAL(expected_persisted, user.loaded_from_storage);
    TEST_ASSERT_EQUAL(!expected_persisted, user.dirty);
}
