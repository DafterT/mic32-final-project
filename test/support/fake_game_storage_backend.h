#ifndef TEST_SUPPORT_FAKE_GAME_STORAGE_BACKEND_H
#define TEST_SUPPORT_FAKE_GAME_STORAGE_BACKEND_H

#include "game_storage_backend.h"

#include <stdbool.h>
#include <stdint.h>

void fake_game_storage_backend_reset(void);
void fake_game_storage_backend_set_init_status(GameStatus status);
void fake_game_storage_backend_set_read_status(GameStatus status);
void fake_game_storage_backend_set_write_status(GameStatus status);
void fake_game_storage_backend_set_erase_status(GameStatus status);
void fake_game_storage_backend_set_read_error_after(uint32_t successful_reads);
void fake_game_storage_backend_enable_write_corruption(uint8_t offset, uint8_t mask);
void fake_game_storage_backend_replace_writes_with(const GameStorageRecord *record);
void fake_game_storage_backend_store_record(uint8_t slot, const GameStorageRecord *record);
void fake_game_storage_backend_fetch_record(uint8_t slot, GameStorageRecord *record);
void fake_game_storage_backend_corrupt_byte(uint8_t slot, uint8_t offset, uint8_t mask);
bool fake_game_storage_backend_slot_is_erased(uint8_t slot);
uint8_t fake_game_storage_backend_count_written_slots(void);

#endif
