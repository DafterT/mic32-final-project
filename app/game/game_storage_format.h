#ifndef APP_GAME_GAME_STORAGE_FORMAT_H
#define APP_GAME_GAME_STORAGE_FORMAT_H

#include "game.h"

#include <stddef.h>
#include <stdint.h>

#define GAME_EEPROM_USABLE_SIZE_BYTES 8192u
#define GAME_EEPROM_SIZE_BYTES        2048u
#define GAME_EEPROM_BASE_OFFSET       (GAME_EEPROM_USABLE_SIZE_BYTES - GAME_EEPROM_SIZE_BYTES)
#define GAME_EEPROM_PAGE_SIZE         128u
#define GAME_EEPROM_SLOT_COUNT        16u
#define GAME_EEPROM_WORDS_PER_PAGE    32u
#define GAME_EEPROM_ERASED_WORD       0xFFFFFFFFu

#define GAME_STORAGE_MAGIC       0x52535547u
#define GAME_STORAGE_VERSION     1u
#define GAME_STORAGE_RECORD_SIZE 128u

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t uid_hash;

    uint8_t uid_len;
    uint8_t flags;
    uint16_t reserved0;
    uint8_t uid[GAME_USER_ID_MAX_BYTES];

    uint32_t stats_rounds;
    uint32_t stats_wins;
    uint32_t stats_draws;
    uint32_t stats_losses;
    uint32_t sessions_played;

    uint8_t model_pending_move;
    uint8_t model_window;
    uint8_t reserved1[2];
    uint8_t model_moves[QUANT_MODEL_MAX_WINDOW];
    uint8_t model_results[QUANT_MODEL_MAX_WINDOW];

    uint32_t reserved2[13];
    uint32_t crc32;
} GameStorageRecord;

_Static_assert(GAME_EEPROM_BASE_OFFSET == 0x1800u, "game EEPROM starts at 0x1800");
_Static_assert((GAME_EEPROM_BASE_OFFSET + GAME_EEPROM_SIZE_BYTES) == GAME_EEPROM_USABLE_SIZE_BYTES,
               "game EEPROM ends at 0x1FFF");
_Static_assert((GAME_EEPROM_PAGE_SIZE * GAME_EEPROM_SLOT_COUNT) == GAME_EEPROM_SIZE_BYTES,
               "game EEPROM storage is 16 pages");
_Static_assert(GAME_STORAGE_RECORD_SIZE == GAME_EEPROM_PAGE_SIZE,
               "game storage record fits one EEPROM page");
_Static_assert(sizeof(GameStorageRecord) == GAME_STORAGE_RECORD_SIZE,
               "GameStorageRecord must be 128 bytes");
_Static_assert(offsetof(GameStorageRecord, crc32) == (GAME_STORAGE_RECORD_SIZE - sizeof(uint32_t)),
               "GameStorageRecord CRC must be last field");

#endif
