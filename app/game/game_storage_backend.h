#ifndef APP_GAME_GAME_STORAGE_BACKEND_H
#define APP_GAME_GAME_STORAGE_BACKEND_H

#include "game.h"
#include "game_storage_format.h"

GameStatus game_storage_backend_init(void);
GameStatus game_storage_backend_read_record(uint8_t slot, GameStorageRecord *record);
GameStatus game_storage_backend_erase_record(uint8_t slot);
GameStatus game_storage_backend_write_record(uint8_t slot, const GameStorageRecord *record);

#endif
