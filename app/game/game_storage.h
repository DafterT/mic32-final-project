#ifndef APP_GAME_GAME_STORAGE_H
#define APP_GAME_GAME_STORAGE_H

#include "game.h"

GameStatus game_storage_init(void);
GameStatus game_storage_load_user(const GameUserId *id, GameUser *user);
GameStatus game_storage_save_user(const GameUser *user, uint32_t sessions_to_store);

#endif
