#ifndef APP_GAME_GAME_H
#define APP_GAME_GAME_H

#include "quant_model.h"

#include <stdbool.h>
#include <stdint.h>

#define GAME_USER_ID_MAX_BYTES 16u

typedef enum {
    GAME_STATUS_OK = 0u,
    GAME_STATUS_NOT_FOUND,
    GAME_STATUS_INVALID_ARG,
    GAME_STATUS_STORAGE_FULL,
    GAME_STATUS_STORAGE_ERROR,
    GAME_STATUS_CRC_ERROR
} GameStatus;

typedef enum {
    GAME_MOVE_ROCK = 0u,
    GAME_MOVE_PAPER = 1u,
    GAME_MOVE_SCISSORS = 2u
} GameMove;

typedef enum {
    GAME_ROUND_PLAYER_LOSS = 0u,
    GAME_ROUND_DRAW = 1u,
    GAME_ROUND_PLAYER_WIN = 2u
} GameRoundResult;

typedef struct {
    uint8_t bytes[GAME_USER_ID_MAX_BYTES];
    uint8_t length;
} GameUserId;

typedef struct {
    uint32_t rounds;
    uint32_t wins;
    uint32_t draws;
    uint32_t losses;
} GameStats;

typedef struct {
    GameMove device_move;
    GameRoundResult result;
} GameRound;

typedef struct {
    GameUserId id;
    GameStats stats;
    QuantModelHistory model_history;
    uint32_t sessions_played;
    bool dirty;
    bool loaded_from_storage;
} GameUser;

GameStatus game_init_user(const GameUserId *id, GameUser *user);
GameRound game_stage(GameUser *user, GameMove player_move);
GameStatus game_save_user(GameUser *user);

#endif
