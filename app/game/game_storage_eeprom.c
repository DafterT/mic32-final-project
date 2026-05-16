#include "game_storage_backend.h"

#include "mik32_hal.h"
#include "mik32_hal_eeprom.h"

#include <stdbool.h>
#include <string.h>

#define GAME_EEPROM_TIMEOUT 100000u

static HAL_EEPROM_HandleTypeDef eeprom;
static bool eeprom_initialized = false;

static uint16_t storage_slot_address(uint8_t slot_index);

GameStatus game_storage_backend_init(void)
{
    if (eeprom_initialized) {
        return GAME_STATUS_OK;
    }

    __HAL_PCC_EEPROM_CLK_ENABLE();

    eeprom.Instance = EEPROM_REGS;
    eeprom.Mode = HAL_EEPROM_MODE_TWO_STAGE;
    eeprom.ErrorCorrection = HAL_EEPROM_ECC_ENABLE;
    eeprom.EnableInterrupt = HAL_EEPROM_SERR_DISABLE;

    if (HAL_EEPROM_Init(&eeprom) != HAL_OK) {
        return GAME_STATUS_STORAGE_ERROR;
    }

    HAL_EEPROM_CalculateTimings(&eeprom, OSC_SYSTEM_VALUE);
    eeprom_initialized = true;

    return GAME_STATUS_OK;
}

GameStatus game_storage_backend_read_record(uint8_t slot, GameStorageRecord *record)
{
    uint32_t words[GAME_EEPROM_WORDS_PER_PAGE];

    if ((slot >= GAME_EEPROM_SLOT_COUNT) || (record == NULL)) {
        return GAME_STATUS_INVALID_ARG;
    }

    if (HAL_EEPROM_Read(&eeprom,
                        storage_slot_address(slot),
                        words,
                        GAME_EEPROM_WORDS_PER_PAGE,
                        GAME_EEPROM_TIMEOUT) != HAL_OK) {
        return GAME_STATUS_STORAGE_ERROR;
    }

    memcpy(record, words, sizeof(*record));
    return GAME_STATUS_OK;
}

GameStatus game_storage_backend_erase_record(uint8_t slot)
{
    if (slot >= GAME_EEPROM_SLOT_COUNT) {
        return GAME_STATUS_INVALID_ARG;
    }

    if (HAL_EEPROM_Erase(&eeprom,
                         storage_slot_address(slot),
                         GAME_EEPROM_WORDS_PER_PAGE,
                         HAL_EEPROM_WRITE_SINGLE,
                         GAME_EEPROM_TIMEOUT) != HAL_OK) {
        return GAME_STATUS_STORAGE_ERROR;
    }

    return GAME_STATUS_OK;
}

GameStatus game_storage_backend_write_record(uint8_t slot, const GameStorageRecord *record)
{
    uint32_t write_words[GAME_EEPROM_WORDS_PER_PAGE];

    if ((slot >= GAME_EEPROM_SLOT_COUNT) || (record == NULL)) {
        return GAME_STATUS_INVALID_ARG;
    }

    memcpy(write_words, record, sizeof(*record));

    if (HAL_EEPROM_Write(&eeprom,
                         storage_slot_address(slot),
                         write_words,
                         GAME_EEPROM_WORDS_PER_PAGE,
                         HAL_EEPROM_WRITE_SINGLE,
                         GAME_EEPROM_TIMEOUT) != HAL_OK) {
        return GAME_STATUS_STORAGE_ERROR;
    }

    return GAME_STATUS_OK;
}

static uint16_t storage_slot_address(uint8_t slot_index)
{
    return (uint16_t)(GAME_EEPROM_BASE_OFFSET + ((uint32_t)slot_index * GAME_EEPROM_PAGE_SIZE));
}
