#include "fake_game_storage_backend.h"

#include <stddef.h>
#include <string.h>

#define GAME_EEPROM_ERASED_BYTE ((uint8_t)GAME_EEPROM_ERASED_WORD)

static GameStorageRecord fake_records[GAME_EEPROM_SLOT_COUNT];
static GameStatus fake_init_status;
static GameStatus fake_read_status;
static GameStatus fake_write_status;
static GameStatus fake_erase_status;
static bool fake_read_error_after_enabled;
static uint32_t fake_reads_before_error;
static bool fake_write_corruption_enabled;
static uint8_t fake_write_corruption_offset;
static uint8_t fake_write_corruption_mask;
static bool fake_write_replacement_enabled;
static GameStorageRecord fake_write_replacement;

static void erase_record(GameStorageRecord *record);
static bool record_is_erased(const GameStorageRecord *record);

void fake_game_storage_backend_reset(void)
{
    uint8_t slot;

    fake_init_status = GAME_STATUS_OK;
    fake_read_status = GAME_STATUS_OK;
    fake_write_status = GAME_STATUS_OK;
    fake_erase_status = GAME_STATUS_OK;
    fake_read_error_after_enabled = false;
    fake_reads_before_error = 0u;
    fake_write_corruption_enabled = false;
    fake_write_corruption_offset = 0u;
    fake_write_corruption_mask = 0u;
    fake_write_replacement_enabled = false;
    erase_record(&fake_write_replacement);

    for (slot = 0u; slot < GAME_EEPROM_SLOT_COUNT; ++slot) {
        erase_record(&fake_records[slot]);
    }
}

void fake_game_storage_backend_set_init_status(GameStatus status)
{
    fake_init_status = status;
}

void fake_game_storage_backend_set_read_status(GameStatus status)
{
    fake_read_status = status;
}

void fake_game_storage_backend_set_write_status(GameStatus status)
{
    fake_write_status = status;
}

void fake_game_storage_backend_set_erase_status(GameStatus status)
{
    fake_erase_status = status;
}

void fake_game_storage_backend_set_read_error_after(uint32_t successful_reads)
{
    fake_read_error_after_enabled = true;
    fake_reads_before_error = successful_reads;
}

void fake_game_storage_backend_enable_write_corruption(uint8_t offset, uint8_t mask)
{
    fake_write_corruption_enabled = true;
    fake_write_corruption_offset = offset;
    fake_write_corruption_mask = mask;
}

void fake_game_storage_backend_replace_writes_with(const GameStorageRecord *record)
{
    if (record == NULL) {
        return;
    }

    fake_write_replacement_enabled = true;
    fake_write_replacement = *record;
}

void fake_game_storage_backend_store_record(uint8_t slot, const GameStorageRecord *record)
{
    if ((slot >= GAME_EEPROM_SLOT_COUNT) || (record == NULL)) {
        return;
    }

    fake_records[slot] = *record;
}

void fake_game_storage_backend_fetch_record(uint8_t slot, GameStorageRecord *record)
{
    if ((slot >= GAME_EEPROM_SLOT_COUNT) || (record == NULL)) {
        return;
    }

    *record = fake_records[slot];
}

void fake_game_storage_backend_corrupt_byte(uint8_t slot, uint8_t offset, uint8_t mask)
{
    uint8_t *bytes;

    if ((slot >= GAME_EEPROM_SLOT_COUNT) || (offset >= sizeof(GameStorageRecord))) {
        return;
    }

    bytes = (uint8_t *)&fake_records[slot];
    bytes[offset] ^= mask;
}

bool fake_game_storage_backend_slot_is_erased(uint8_t slot)
{
    if (slot >= GAME_EEPROM_SLOT_COUNT) {
        return false;
    }

    return record_is_erased(&fake_records[slot]);
}

uint8_t fake_game_storage_backend_count_written_slots(void)
{
    uint8_t slot;
    uint8_t count = 0u;

    for (slot = 0u; slot < GAME_EEPROM_SLOT_COUNT; ++slot) {
        if (!record_is_erased(&fake_records[slot])) {
            count += 1u;
        }
    }

    return count;
}

GameStatus game_storage_backend_init(void)
{
    return fake_init_status;
}

GameStatus game_storage_backend_read_record(uint8_t slot, GameStorageRecord *record)
{
    if ((slot >= GAME_EEPROM_SLOT_COUNT) || (record == NULL)) {
        return GAME_STATUS_INVALID_ARG;
    }

    if (fake_read_status != GAME_STATUS_OK) {
        return fake_read_status;
    }

    if (fake_read_error_after_enabled) {
        if (fake_reads_before_error == 0u) {
            return GAME_STATUS_STORAGE_ERROR;
        }
        fake_reads_before_error -= 1u;
    }

    *record = fake_records[slot];
    return GAME_STATUS_OK;
}

GameStatus game_storage_backend_erase_record(uint8_t slot)
{
    if (slot >= GAME_EEPROM_SLOT_COUNT) {
        return GAME_STATUS_INVALID_ARG;
    }

    if (fake_erase_status != GAME_STATUS_OK) {
        return fake_erase_status;
    }

    erase_record(&fake_records[slot]);
    return GAME_STATUS_OK;
}

GameStatus game_storage_backend_write_record(uint8_t slot, const GameStorageRecord *record)
{
    if ((slot >= GAME_EEPROM_SLOT_COUNT) || (record == NULL)) {
        return GAME_STATUS_INVALID_ARG;
    }

    if (fake_write_status != GAME_STATUS_OK) {
        return fake_write_status;
    }

    fake_records[slot] = *record;
    if (fake_write_replacement_enabled) {
        fake_records[slot] = fake_write_replacement;
    }

    if (fake_write_corruption_enabled && (fake_write_corruption_offset < sizeof(fake_records[slot]))) {
        uint8_t *bytes = (uint8_t *)&fake_records[slot];

        bytes[fake_write_corruption_offset] ^= fake_write_corruption_mask;
    }

    return GAME_STATUS_OK;
}

static void erase_record(GameStorageRecord *record)
{
    memset(record, GAME_EEPROM_ERASED_BYTE, sizeof(*record));
}

static bool record_is_erased(const GameStorageRecord *record)
{
    const uint8_t *bytes = (const uint8_t *)record;
    size_t index;

    for (index = 0u; index < sizeof(*record); ++index) {
        if (bytes[index] != GAME_EEPROM_ERASED_BYTE) {
            return false;
        }
    }

    return true;
}
