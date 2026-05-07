#include "game_storage.h"

#include "game_storage_format.h"
#include "model_weights.h"
#include "mik32_hal.h"
#include "mik32_hal_eeprom.h"

#include <stddef.h>
#include <string.h>

#define GAME_EEPROM_TIMEOUT 100000u
#define STORAGE_SLOT_NONE   (-1)
#define GAME_EEPROM_ERASED_BYTE ((uint8_t)GAME_EEPROM_ERASED_WORD)
#define GAME_STORAGE_CRC_LENGTH ((uint32_t)offsetof(GameStorageRecord, crc32))

typedef enum {
    STORAGE_SLOT_EMPTY = 0u,
    STORAGE_SLOT_VALID,
    STORAGE_SLOT_CORRUPT,
    STORAGE_SLOT_UNKNOWN
} StorageSlotState;

typedef struct {
    int8_t user_slot;
    int8_t matching_corrupt_slot;
    int8_t empty_slot;
    int8_t corrupt_or_unknown_slot;
    int8_t eviction_slot;
    uint32_t eviction_sessions;
    uint32_t eviction_rounds;
} StorageScan;

static GameStatus eeprom_erase_slot(uint8_t slot_index);
static bool storage_record_is_empty(const GameStorageRecord *record);
static bool storage_record_is_valid(const GameStorageRecord *record);
static uint32_t storage_crc32(const void *data, uint32_t length);
static uint32_t storage_uid_hash(const GameUserId *id);

static uint16_t storage_slot_address(uint8_t slot_index);
static bool storage_user_id_is_valid(const GameUserId *id);
static bool storage_record_header_is_known(const GameStorageRecord *record);
static bool storage_record_uid_is_valid(const GameStorageRecord *record);
static StorageSlotState storage_record_state(const GameStorageRecord *record);
static bool storage_record_matches_uid(const GameStorageRecord *record, const GameUserId *id);
static bool storage_record_is_better_eviction(const StorageScan *scan, const GameStorageRecord *record);
static GameStatus storage_read_record(uint8_t slot_index, GameStorageRecord *record);
static GameStatus storage_write_record(uint8_t slot_index, const GameStorageRecord *record);
static GameStatus storage_scan_slots(const GameUserId *id, StorageScan *scan);
static void storage_scan_init(StorageScan *scan);
static void storage_scan_update(StorageScan *scan,
                                uint8_t slot_index,
                                const GameStorageRecord *record,
                                StorageSlotState state,
                                const GameUserId *id);
static void storage_record_get_id(const GameStorageRecord *record, GameUserId *id);
static void storage_record_to_user(const GameStorageRecord *record, GameUser *user);
static void storage_user_to_record(const GameUser *user, uint32_t sessions_to_store, GameStorageRecord *record);
static GameStatus storage_verify_written_record(uint8_t slot_index, const GameStorageRecord *expected);

static HAL_EEPROM_HandleTypeDef eeprom;
static bool eeprom_initialized = false;

GameStatus game_storage_init(void)
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

GameStatus game_storage_load_user(const GameUserId *id, GameUser *user)
{
    uint8_t slot_index;
    GameStatus status;

    if (!storage_user_id_is_valid(id) || (user == NULL)) {
        return GAME_STATUS_INVALID_ARG;
    }

    status = game_storage_init();
    if (status != GAME_STATUS_OK) {
        return status;
    }

    for (slot_index = 0u; slot_index < GAME_EEPROM_SLOT_COUNT; ++slot_index) {
        GameStorageRecord record;

        status = storage_read_record(slot_index, &record);
        if (status != GAME_STATUS_OK) {
            return status;
        }

        if (storage_record_is_empty(&record) ||
            !storage_record_header_is_known(&record) ||
            !storage_record_matches_uid(&record, id)) {
            continue;
        }

        if (record.crc32 != storage_crc32(&record, GAME_STORAGE_CRC_LENGTH)) {
            status = eeprom_erase_slot(slot_index);
            if (status != GAME_STATUS_OK) {
                return status;
            }
            return GAME_STATUS_CRC_ERROR;
        }

        storage_record_to_user(&record, user);
        return GAME_STATUS_OK;
    }

    return GAME_STATUS_NOT_FOUND;
}

GameStatus game_storage_save_user(const GameUser *user, uint32_t sessions_to_store)
{
    StorageScan scan;
    int8_t target_slot;
    GameStatus status;
    GameStatus save_status = GAME_STATUS_OK;
    GameStorageRecord record;

    if ((user == NULL) || !storage_user_id_is_valid(&user->id)) {
        return GAME_STATUS_INVALID_ARG;
    }

    status = game_storage_init();
    if (status != GAME_STATUS_OK) {
        return status;
    }

    status = storage_scan_slots(&user->id, &scan);
    if (status != GAME_STATUS_OK) {
        return status;
    }

    if (scan.user_slot >= 0) {
        target_slot = scan.user_slot;
    } else if (scan.matching_corrupt_slot >= 0) {
        target_slot = scan.matching_corrupt_slot;
        save_status = GAME_STATUS_CRC_ERROR;
    } else if (scan.empty_slot >= 0) {
        target_slot = scan.empty_slot;
    } else if (scan.corrupt_or_unknown_slot >= 0) {
        target_slot = scan.corrupt_or_unknown_slot;
        save_status = GAME_STATUS_CRC_ERROR;
    } else if (scan.eviction_slot >= 0) {
        target_slot = scan.eviction_slot;
        save_status = GAME_STATUS_STORAGE_FULL;
    } else {
        return GAME_STATUS_STORAGE_FULL;
    }

    status = eeprom_erase_slot((uint8_t)target_slot);
    if (status != GAME_STATUS_OK) {
        return status;
    }

    storage_user_to_record(user, sessions_to_store, &record);
    status = storage_write_record((uint8_t)target_slot, &record);
    if (status != GAME_STATUS_OK) {
        return status;
    }

    status = storage_verify_written_record((uint8_t)target_slot, &record);
    if (status != GAME_STATUS_OK) {
        return status;
    }

    return save_status;
}

static GameStatus eeprom_erase_slot(uint8_t slot_index)
{
    if (slot_index >= GAME_EEPROM_SLOT_COUNT) {
        return GAME_STATUS_INVALID_ARG;
    }

    if (HAL_EEPROM_Erase(&eeprom,
                         storage_slot_address(slot_index),
                         GAME_EEPROM_WORDS_PER_PAGE,
                         HAL_EEPROM_WRITE_SINGLE,
                         GAME_EEPROM_TIMEOUT) != HAL_OK) {
        return GAME_STATUS_STORAGE_ERROR;
    }

    return GAME_STATUS_OK;
}

static bool storage_record_is_empty(const GameStorageRecord *record)
{
    const uint8_t *bytes = (const uint8_t *)record;
    size_t index;

    if (record == NULL) {
        return false;
    }

    for (index = 0u; index < sizeof(*record); ++index) {
        if (bytes[index] != GAME_EEPROM_ERASED_BYTE) {
            return false;
        }
    }

    return true;
}

static bool storage_record_is_valid(const GameStorageRecord *record)
{
    return storage_record_header_is_known(record) &&
           storage_record_uid_is_valid(record) &&
           (record->crc32 == storage_crc32(record, GAME_STORAGE_CRC_LENGTH));
}

static uint32_t storage_crc32(const void *data, uint32_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t index;

    for (index = 0u; index < length; ++index) {
        uint8_t bit;

        crc ^= bytes[index];
        for (bit = 0u; bit < 8u; ++bit) {
            crc = ((crc & 1u) != 0u) ? ((crc >> 1u) ^ 0xEDB88320u) : (crc >> 1u);
        }
    }

    return crc ^ 0xFFFFFFFFu;
}

static uint32_t storage_uid_hash(const GameUserId *id)
{
    uint32_t hash = 2166136261u;
    uint8_t index;

    if (!storage_user_id_is_valid(id)) {
        return 0u;
    }

    hash = (hash ^ id->length) * 16777619u;
    for (index = 0u; index < id->length; ++index) {
        hash = (hash ^ id->bytes[index]) * 16777619u;
    }

    return hash;
}

static uint16_t storage_slot_address(uint8_t slot_index)
{
    return (uint16_t)(GAME_EEPROM_BASE_OFFSET + ((uint32_t)slot_index * GAME_EEPROM_PAGE_SIZE));
}

static bool storage_user_id_is_valid(const GameUserId *id)
{
    return (id != NULL) && (id->length > 0u) && (id->length <= GAME_USER_ID_MAX_BYTES);
}

static bool storage_record_header_is_known(const GameStorageRecord *record)
{
    return (record != NULL) &&
           (record->magic == GAME_STORAGE_MAGIC) &&
           (record->version == GAME_STORAGE_VERSION) &&
           (record->size == GAME_STORAGE_RECORD_SIZE);
}

static bool storage_record_uid_is_valid(const GameStorageRecord *record)
{
    return (record != NULL) && (record->uid_len > 0u) && (record->uid_len <= GAME_USER_ID_MAX_BYTES);
}

static StorageSlotState storage_record_state(const GameStorageRecord *record)
{
    if (storage_record_is_empty(record)) {
        return STORAGE_SLOT_EMPTY;
    }

    if (!storage_record_header_is_known(record)) {
        return STORAGE_SLOT_UNKNOWN;
    }

    return storage_record_is_valid(record) ? STORAGE_SLOT_VALID : STORAGE_SLOT_CORRUPT;
}

static bool storage_record_matches_uid(const GameStorageRecord *record, const GameUserId *id)
{
    if (!storage_record_uid_is_valid(record) || !storage_user_id_is_valid(id)) {
        return false;
    }

    if (record->uid_len != id->length) {
        return false;
    }

    if (record->uid_hash != storage_uid_hash(id)) {
        return false;
    }

    return memcmp(record->uid, id->bytes, id->length) == 0;
}

static bool storage_record_is_better_eviction(const StorageScan *scan, const GameStorageRecord *record)
{
    if (scan->eviction_slot < 0) {
        return true;
    }

    if (record->sessions_played != scan->eviction_sessions) {
        return record->sessions_played < scan->eviction_sessions;
    }

    return record->stats_rounds < scan->eviction_rounds;
}

static GameStatus storage_read_record(uint8_t slot_index, GameStorageRecord *record)
{
    uint32_t words[GAME_EEPROM_WORDS_PER_PAGE];

    if ((slot_index >= GAME_EEPROM_SLOT_COUNT) || (record == NULL)) {
        return GAME_STATUS_INVALID_ARG;
    }

    if (HAL_EEPROM_Read(&eeprom,
                        storage_slot_address(slot_index),
                        words,
                        GAME_EEPROM_WORDS_PER_PAGE,
                        GAME_EEPROM_TIMEOUT) != HAL_OK) {
        return GAME_STATUS_STORAGE_ERROR;
    }

    memcpy(record, words, sizeof(*record));
    return GAME_STATUS_OK;
}

static GameStatus storage_write_record(uint8_t slot_index, const GameStorageRecord *record)
{
    uint32_t write_words[GAME_EEPROM_WORDS_PER_PAGE];

    if ((slot_index >= GAME_EEPROM_SLOT_COUNT) || (record == NULL)) {
        return GAME_STATUS_INVALID_ARG;
    }

    memcpy(write_words, record, sizeof(*record));

    if (HAL_EEPROM_Write(&eeprom,
                         storage_slot_address(slot_index),
                         write_words,
                         GAME_EEPROM_WORDS_PER_PAGE,
                         HAL_EEPROM_WRITE_SINGLE,
                         GAME_EEPROM_TIMEOUT) != HAL_OK) {
        return GAME_STATUS_STORAGE_ERROR;
    }

    return GAME_STATUS_OK;
}

static GameStatus storage_scan_slots(const GameUserId *id, StorageScan *scan)
{
    uint8_t slot_index;

    if ((scan == NULL) || !storage_user_id_is_valid(id)) {
        return GAME_STATUS_INVALID_ARG;
    }

    storage_scan_init(scan);

    for (slot_index = 0u; slot_index < GAME_EEPROM_SLOT_COUNT; ++slot_index) {
        GameStorageRecord record;
        GameStatus status;

        status = storage_read_record(slot_index, &record);
        if (status != GAME_STATUS_OK) {
            return status;
        }

        storage_scan_update(scan, slot_index, &record, storage_record_state(&record), id);
    }

    return GAME_STATUS_OK;
}

static void storage_scan_init(StorageScan *scan)
{
    scan->user_slot = STORAGE_SLOT_NONE;
    scan->matching_corrupt_slot = STORAGE_SLOT_NONE;
    scan->empty_slot = STORAGE_SLOT_NONE;
    scan->corrupt_or_unknown_slot = STORAGE_SLOT_NONE;
    scan->eviction_slot = STORAGE_SLOT_NONE;
    scan->eviction_sessions = UINT32_MAX;
    scan->eviction_rounds = UINT32_MAX;
}

static void storage_scan_update(StorageScan *scan,
                                uint8_t slot_index,
                                const GameStorageRecord *record,
                                StorageSlotState state,
                                const GameUserId *id)
{
    if (state == STORAGE_SLOT_EMPTY) {
        if (scan->empty_slot < 0) {
            scan->empty_slot = (int8_t)slot_index;
        }
        return;
    }

    if ((state == STORAGE_SLOT_CORRUPT) || (state == STORAGE_SLOT_UNKNOWN)) {
        if ((state == STORAGE_SLOT_CORRUPT) && storage_record_matches_uid(record, id)) {
            scan->matching_corrupt_slot = (int8_t)slot_index;
        } else if (scan->corrupt_or_unknown_slot < 0) {
            scan->corrupt_or_unknown_slot = (int8_t)slot_index;
        }
        return;
    }

    if (storage_record_matches_uid(record, id)) {
        scan->user_slot = (int8_t)slot_index;
    }

    if (storage_record_is_better_eviction(scan, record)) {
        scan->eviction_slot = (int8_t)slot_index;
        scan->eviction_sessions = record->sessions_played;
        scan->eviction_rounds = record->stats_rounds;
    }
}

static void storage_record_get_id(const GameStorageRecord *record, GameUserId *id)
{
    memset(id->bytes, 0, sizeof(id->bytes));
    id->length = record->uid_len;
    memcpy(id->bytes, record->uid, record->uid_len);
}

static void storage_record_to_user(const GameStorageRecord *record, GameUser *user)
{
    memset(user, 0, sizeof(*user));
    storage_record_get_id(record, &user->id);

    user->stats.rounds = record->stats_rounds;
    user->stats.wins = record->stats_wins;
    user->stats.draws = record->stats_draws;
    user->stats.losses = record->stats_losses;
    user->sessions_played = record->sessions_played;

    user->model_history.pending_move = record->model_pending_move;
    memcpy(user->model_history.moves, record->model_moves, sizeof(user->model_history.moves));
    memcpy(user->model_history.results, record->model_results, sizeof(user->model_history.results));

    user->dirty = false;
    user->loaded_from_storage = true;
}

static void storage_user_to_record(const GameUser *user, uint32_t sessions_to_store, GameStorageRecord *record)
{
    memset(record, 0, sizeof(*record));

    record->magic = GAME_STORAGE_MAGIC;
    record->version = GAME_STORAGE_VERSION;
    record->size = GAME_STORAGE_RECORD_SIZE;
    record->uid_hash = storage_uid_hash(&user->id);

    record->uid_len = user->id.length;
    memcpy(record->uid, user->id.bytes, user->id.length);

    record->stats_rounds = user->stats.rounds;
    record->stats_wins = user->stats.wins;
    record->stats_draws = user->stats.draws;
    record->stats_losses = user->stats.losses;
    record->sessions_played = sessions_to_store;

    record->model_pending_move = user->model_history.pending_move;
    record->model_window = MODEL.window;
    memcpy(record->model_moves, user->model_history.moves, sizeof(record->model_moves));
    memcpy(record->model_results, user->model_history.results, sizeof(record->model_results));

    record->crc32 = storage_crc32(record, GAME_STORAGE_CRC_LENGTH);
}

static GameStatus storage_verify_written_record(uint8_t slot_index, const GameStorageRecord *expected)
{
    GameStorageRecord actual;
    GameStatus status;

    status = storage_read_record(slot_index, &actual);
    if (status != GAME_STATUS_OK) {
        return status;
    }

    if (!storage_record_is_valid(&actual) || (memcmp(&actual, expected, sizeof(actual)) != 0)) {
        return GAME_STATUS_STORAGE_ERROR;
    }

    return GAME_STATUS_OK;
}
