#include "unity.h"

TEST_SOURCE_FILE("support/fake_game_storage_backend.c")

#include "support/fake_game_storage_backend.h"

#include "game_storage.h"
#include "game_storage_format.h"
#include "uid_hash.h"

#include <stddef.h>

const QuantModel MODEL = {
    .window = 6u,
};

/** Сбрасывает фейковую EEPROM перед каждым тестом. */
void setUp(void)
{
    fake_game_storage_backend_reset();
}

/** Завершение теста не требует действий. */
void tearDown(void)
{
}

/** Создаёт стабильный ID пользователя из начального байта. */
static GameUserId make_user_id(uint8_t seed)
{
    GameUserId id = {
        .bytes = {seed, (uint8_t)(seed + 1u), (uint8_t)(seed + 2u), (uint8_t)(seed + 3u)},
        .length = 4u,
    };

    return id;
}

/** Создаёт пользователя с детерминированной статистикой и историей модели. */
static GameUser make_user(uint8_t seed, uint32_t rounds, uint32_t sessions_played)
{
    GameUser user = {
        .id = {
            .bytes = {seed, (uint8_t)(seed + 1u), (uint8_t)(seed + 2u), (uint8_t)(seed + 3u)},
            .length = 4u,
        },
        .stats = {
            .rounds = rounds,
            .wins = (uint32_t)(seed + 1u),
            .draws = (uint32_t)(seed + 2u),
            .losses = (uint32_t)(seed + 3u),
        },
        .model_history = {
            .pending_move = (uint8_t)(seed + 4u),
            .moves = {
                seed,
                (uint8_t)(seed + 1u),
                (uint8_t)(seed + 2u),
                (uint8_t)(seed + 3u),
                (uint8_t)(seed + 4u),
                (uint8_t)(seed + 5u),
                (uint8_t)(seed + 6u),
                (uint8_t)(seed + 7u),
            },
            .results = {
                (uint8_t)(seed + 10u),
                (uint8_t)(seed + 11u),
                (uint8_t)(seed + 12u),
                (uint8_t)(seed + 13u),
                (uint8_t)(seed + 14u),
                (uint8_t)(seed + 15u),
                (uint8_t)(seed + 16u),
                (uint8_t)(seed + 17u),
            },
        },
        .sessions_played = sessions_played,
        .dirty = true,
        .loaded_from_storage = false,
    };

    return user;
}

/** Сохраняет одного пользователя и возвращает исходный объект. */
static GameUser arrange_saved_user(uint8_t seed, uint32_t sessions_played, uint32_t rounds)
{
    GameUser user = make_user(seed, rounds, sessions_played);

    TEST_ASSERT_EQUAL_INT(GAME_STATUS_OK, game_storage_save_user(&user, sessions_played));
    return user;
}

/** Заполняет все слоты валидными пользователями для тестов вытеснения. */
static void arrange_full_storage_for_reuse(void)
{
    arrange_saved_user(1u, 20u, 20u);
    arrange_saved_user(2u, 1u, 7u);
    arrange_saved_user(3u, 1u, 9u);
    arrange_saved_user(4u, 30u, 30u);
    arrange_saved_user(5u, 31u, 31u);
    arrange_saved_user(6u, 32u, 32u);
    arrange_saved_user(7u, 33u, 33u);
    arrange_saved_user(8u, 34u, 34u);
    arrange_saved_user(9u, 35u, 35u);
    arrange_saved_user(10u, 36u, 36u);
    arrange_saved_user(11u, 37u, 37u);
    arrange_saved_user(12u, 38u, 38u);
    arrange_saved_user(13u, 39u, 39u);
    arrange_saved_user(14u, 40u, 40u);
    arrange_saved_user(15u, 41u, 41u);
    arrange_saved_user(16u, 42u, 42u);
}

/** Сохраняет запись с заданной длиной UID. */
static void arrange_saved_record_with_uid_length(uint8_t seed, uint8_t uid_length)
{
    GameStorageRecord record;

    arrange_saved_user(seed, 1u, 10u);
    fake_game_storage_backend_fetch_record(0u, &record);
    record.uid_len = uid_length;
    fake_game_storage_backend_store_record(0u, &record);
}

/** Проверяет, что загруженный пользователь совпадает с сохранёнными полями. */
static void assert_loaded_user_matches(const GameUser *expected, const GameUser *actual, uint32_t sessions_played)
{
    TEST_ASSERT_EQUAL_UINT8(expected->id.length, actual->id.length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected->id.bytes, actual->id.bytes, expected->id.length);
    TEST_ASSERT_EQUAL_UINT32(expected->stats.rounds, actual->stats.rounds);
    TEST_ASSERT_EQUAL_UINT32(expected->stats.wins, actual->stats.wins);
    TEST_ASSERT_EQUAL_UINT32(expected->stats.draws, actual->stats.draws);
    TEST_ASSERT_EQUAL_UINT32(expected->stats.losses, actual->stats.losses);
    TEST_ASSERT_EQUAL_UINT32(sessions_played, actual->sessions_played);
    TEST_ASSERT_EQUAL_MEMORY(&expected->model_history, &actual->model_history, sizeof(actual->model_history));
    TEST_ASSERT_FALSE(actual->dirty);
    TEST_ASSERT_TRUE(actual->loaded_from_storage);
}

/** Успешная инициализация бэкенда возвращает статус успеха. */
void test_game_storage_init_returns_ok_when_backend_init_succeeds(void)
{
    GameStatus status;

    status = game_storage_init();

    TEST_ASSERT_EQUAL_INT(GAME_STATUS_OK, status);
}

/** Пустая EEPROM возвращает статус отсутствия записи. */
void test_game_storage_load_user_returns_not_found_when_eeprom_is_empty(void)
{
    GameUserId id = make_user_id(1u);
    GameUser user = {0};
    GameStatus status;

    status = game_storage_load_user(&id, &user);

    TEST_ASSERT_EQUAL_INT(GAME_STATUS_NOT_FOUND, status);
}

/** Сохранение и последующая загрузка восстанавливают состояние пользователя. */
void test_game_storage_save_then_load_restores_user_stats_history_and_sessions(void)
{
    GameUser saved = make_user(2u, 12u, 3u);
    GameUser loaded = {0};
    GameStatus save_status;
    GameStatus load_status;

    save_status = game_storage_save_user(&saved, 3u);
    load_status = game_storage_load_user(&saved.id, &loaded);

    TEST_ASSERT_EQUAL_INT(GAME_STATUS_OK, save_status);
    TEST_ASSERT_EQUAL_INT(GAME_STATUS_OK, load_status);
    assert_loaded_user_matches(&saved, &loaded, 3u);
}

/** Сохранение записывает общий hash32 UID в формат EEPROM. */
void test_game_storage_save_writes_uid_hash32_to_record(void)
{
    GameUser saved = make_user(70u, 12u, 3u);
    GameStorageRecord record;
    GameStatus save_status;

    save_status = game_storage_save_user(&saved, 3u);
    fake_game_storage_backend_fetch_record(0u, &record);

    TEST_ASSERT_EQUAL_INT(GAME_STATUS_OK, save_status);
    TEST_ASSERT_EQUAL_HEX32(uid_hash32(&saved.id), record.uid_hash);
}

/** Список пользователей пустой EEPROM возвращает без ошибок. */
void test_game_storage_list_users_returns_empty_list_when_eeprom_is_empty(void)
{
    GameUser users[GAME_EEPROM_SLOT_COUNT];
    uint8_t count = 0xAAu;
    GameStatus status;

    status = game_storage_list_users(users, GAME_EEPROM_SLOT_COUNT, &count);

    TEST_ASSERT_EQUAL_INT(GAME_STATUS_OK, status);
    TEST_ASSERT_EQUAL_UINT8(0u, count);
}

/** Список пользователей возвращает сохранённого пользователя со статистикой и сессиями. */
void test_game_storage_list_users_returns_saved_user_stats_history_and_sessions(void)
{
    GameUser saved = arrange_saved_user(30u, 5u, 21u);
    GameUser users[GAME_EEPROM_SLOT_COUNT];
    uint8_t count = 0u;
    GameStatus status;

    status = game_storage_list_users(users, GAME_EEPROM_SLOT_COUNT, &count);

    TEST_ASSERT_EQUAL_INT(GAME_STATUS_OK, status);
    TEST_ASSERT_EQUAL_UINT8(1u, count);
    assert_loaded_user_matches(&saved, &users[0], 5u);
}

/** Список пользователей возвращает все валидные записи в порядке слотов. */
void test_game_storage_list_users_returns_multiple_valid_users(void)
{
    GameUser first = arrange_saved_user(31u, 2u, 10u);
    GameUser second = arrange_saved_user(32u, 3u, 11u);
    GameUser third = arrange_saved_user(33u, 4u, 12u);
    GameUser users[GAME_EEPROM_SLOT_COUNT];
    uint8_t count = 0u;
    GameStatus status;

    status = game_storage_list_users(users, GAME_EEPROM_SLOT_COUNT, &count);

    TEST_ASSERT_EQUAL_INT(GAME_STATUS_OK, status);
    TEST_ASSERT_EQUAL_UINT8(3u, count);
    assert_loaded_user_matches(&first, &users[0], 2u);
    assert_loaded_user_matches(&second, &users[1], 3u);
    assert_loaded_user_matches(&third, &users[2], 4u);
}

/** Список пользователей пропускает записи с неизвестным заголовком. */
void test_game_storage_list_users_skips_records_with_bad_magic_version_and_size(void)
{
    GameUser valid;
    GameStorageRecord record;
    GameUser users[GAME_EEPROM_SLOT_COUNT];
    uint8_t count = 0u;
    GameStatus status;

    arrange_saved_user(34u, 1u, 10u);
    arrange_saved_user(35u, 2u, 11u);
    arrange_saved_user(36u, 3u, 12u);
    valid = arrange_saved_user(37u, 4u, 13u);

    fake_game_storage_backend_fetch_record(0u, &record);
    record.magic = 0u;
    fake_game_storage_backend_store_record(0u, &record);

    fake_game_storage_backend_fetch_record(1u, &record);
    record.version = 0u;
    fake_game_storage_backend_store_record(1u, &record);

    fake_game_storage_backend_fetch_record(2u, &record);
    record.size = 0u;
    fake_game_storage_backend_store_record(2u, &record);

    status = game_storage_list_users(users, GAME_EEPROM_SLOT_COUNT, &count);

    TEST_ASSERT_EQUAL_INT(GAME_STATUS_OK, status);
    TEST_ASSERT_EQUAL_UINT8(1u, count);
    assert_loaded_user_matches(&valid, &users[0], 4u);
}

/** Список пользователей пропускает запись с битым CRC и не стирает слот. */
void test_game_storage_list_users_skips_corrupt_crc_without_erasing_slot(void)
{
    GameUser users[GAME_EEPROM_SLOT_COUNT];
    uint8_t count = 0u;
    GameStatus status;

    arrange_saved_user(38u, 3u, 18u);
    fake_game_storage_backend_corrupt_byte(0u, (uint8_t)offsetof(GameStorageRecord, stats_rounds), 0x01u);

    status = game_storage_list_users(users, GAME_EEPROM_SLOT_COUNT, &count);

    TEST_ASSERT_EQUAL_INT(GAME_STATUS_OK, status);
    TEST_ASSERT_EQUAL_UINT8(0u, count);
    TEST_ASSERT_FALSE(fake_game_storage_backend_slot_is_erased(0u));
}

/** Список пользователей заполняет только capacity записей и возвращает успех. */
void test_game_storage_list_users_truncates_to_capacity(void)
{
    GameUser first = arrange_saved_user(39u, 2u, 10u);
    GameUser second = arrange_saved_user(40u, 3u, 11u);
    GameUser users[1];
    uint8_t count = 0u;
    GameStatus status;

    status = game_storage_list_users(users, 1u, &count);

    TEST_ASSERT_EQUAL_INT(GAME_STATUS_OK, status);
    TEST_ASSERT_EQUAL_UINT8(1u, count);
    assert_loaded_user_matches(&first, &users[0], 2u);
    TEST_ASSERT_EQUAL_UINT8(2u, fake_game_storage_backend_count_written_slots());
    (void)second;
}

/** Список пользователей отвергает недопустимые аргументы. */
void test_game_storage_list_users_rejects_invalid_arguments(void)
{
    GameUser users[GAME_EEPROM_SLOT_COUNT];
    uint8_t count = 0u;
    GameStatus null_users_status;
    GameStatus null_count_status;
    GameStatus zero_capacity_status;

    null_users_status = game_storage_list_users(NULL, GAME_EEPROM_SLOT_COUNT, &count);
    null_count_status = game_storage_list_users(users, GAME_EEPROM_SLOT_COUNT, NULL);
    zero_capacity_status = game_storage_list_users(users, 0u, &count);

    TEST_ASSERT_EQUAL_INT(GAME_STATUS_INVALID_ARG, null_users_status);
    TEST_ASSERT_EQUAL_INT(GAME_STATUS_INVALID_ARG, null_count_status);
    TEST_ASSERT_EQUAL_INT(GAME_STATUS_INVALID_ARG, zero_capacity_status);
}

/** Список пользователей возвращает ошибку инициализации бэкенда. */
void test_game_storage_list_users_returns_storage_error_when_backend_init_fails(void)
{
    GameUser users[GAME_EEPROM_SLOT_COUNT];
    uint8_t count = 0u;
    GameStatus status;

    fake_game_storage_backend_set_init_status(GAME_STATUS_STORAGE_ERROR);

    status = game_storage_list_users(users, GAME_EEPROM_SLOT_COUNT, &count);

    TEST_ASSERT_EQUAL_INT(GAME_STATUS_STORAGE_ERROR, status);
}

/** Список пользователей возвращает ошибку чтения бэкенда. */
void test_game_storage_list_users_returns_storage_error_when_backend_read_fails(void)
{
    GameUser users[GAME_EEPROM_SLOT_COUNT];
    uint8_t count = 0u;
    GameStatus status;

    fake_game_storage_backend_set_read_status(GAME_STATUS_STORAGE_ERROR);

    status = game_storage_list_users(users, GAME_EEPROM_SLOT_COUNT, &count);

    TEST_ASSERT_EQUAL_INT(GAME_STATUS_STORAGE_ERROR, status);
}

/** Повторное сохранение того же UID обновляет существующий слот. */
void test_game_storage_save_same_uid_twice_updates_existing_slot(void)
{
    GameUser first = make_user(3u, 10u, 1u);
    GameUser second = make_user(3u, 25u, 7u);
    GameUser loaded = {0};
    GameStatus first_status;
    GameStatus second_status;
    GameStatus load_status;

    first_status = game_storage_save_user(&first, 1u);
    second_status = game_storage_save_user(&second, 7u);
    load_status = game_storage_load_user(&second.id, &loaded);

    TEST_ASSERT_EQUAL_INT(GAME_STATUS_OK, first_status);
    TEST_ASSERT_EQUAL_INT(GAME_STATUS_OK, second_status);
    TEST_ASSERT_EQUAL_INT(GAME_STATUS_OK, load_status);
    TEST_ASSERT_EQUAL_UINT8(1u, fake_game_storage_backend_count_written_slots());
    assert_loaded_user_matches(&second, &loaded, 7u);
}

/** Битый CRC при загрузке возвращает ошибку CRC и стирает слот. */
void test_game_storage_load_user_erases_matching_record_with_corrupted_crc(void)
{
    GameUser saved = arrange_saved_user(4u, 3u, 18u);
    GameUser loaded = {0};
    GameStatus status;

    fake_game_storage_backend_corrupt_byte(0u, (uint8_t)offsetof(GameStorageRecord, stats_rounds), 0x01u);

    status = game_storage_load_user(&saved.id, &loaded);

    TEST_ASSERT_EQUAL_INT(GAME_STATUS_CRC_ERROR, status);
    TEST_ASSERT_TRUE(fake_game_storage_backend_slot_is_erased(0u));
}

/** Ошибка стирания при восстановлении после CRC возвращает ошибку хранилища. */
void test_game_storage_load_user_returns_storage_error_when_crc_erase_fails(void)
{
    GameUser saved = arrange_saved_user(17u, 3u, 18u);
    GameUser loaded = {0};
    GameStatus status;

    fake_game_storage_backend_corrupt_byte(0u, (uint8_t)offsetof(GameStorageRecord, stats_rounds), 0x01u);
    fake_game_storage_backend_set_erase_status(GAME_STATUS_STORAGE_ERROR);

    status = game_storage_load_user(&saved.id, &loaded);

    TEST_ASSERT_EQUAL_INT(GAME_STATUS_STORAGE_ERROR, status);
    TEST_ASSERT_FALSE(fake_game_storage_backend_slot_is_erased(0u));
}

/** Неверный magic заставляет загрузку игнорировать запись. */
void test_game_storage_load_user_skips_record_with_bad_magic(void)
{
    GameUser saved = arrange_saved_user(18u, 4u, 20u);
    GameStorageRecord record;
    GameUser loaded = {0};
    GameStatus status;

    fake_game_storage_backend_fetch_record(0u, &record);
    record.magic = 0u;
    fake_game_storage_backend_store_record(0u, &record);

    status = game_storage_load_user(&saved.id, &loaded);

    TEST_ASSERT_EQUAL_INT(GAME_STATUS_NOT_FOUND, status);
}

/** Неверная version заставляет загрузку игнорировать запись. */
void test_game_storage_load_user_skips_record_with_bad_version(void)
{
    GameUser saved = arrange_saved_user(19u, 4u, 20u);
    GameStorageRecord record;
    GameUser loaded = {0};
    GameStatus status;

    fake_game_storage_backend_fetch_record(0u, &record);
    record.version = 0u;
    fake_game_storage_backend_store_record(0u, &record);

    status = game_storage_load_user(&saved.id, &loaded);

    TEST_ASSERT_EQUAL_INT(GAME_STATUS_NOT_FOUND, status);
}

/** Неверный размер записи заставляет загрузку игнорировать запись. */
void test_game_storage_load_user_skips_record_with_bad_size(void)
{
    GameUser saved = arrange_saved_user(20u, 4u, 20u);
    GameStorageRecord record;
    GameUser loaded = {0};
    GameStatus status;

    fake_game_storage_backend_fetch_record(0u, &record);
    record.size = 0u;
    fake_game_storage_backend_store_record(0u, &record);

    status = game_storage_load_user(&saved.id, &loaded);

    TEST_ASSERT_EQUAL_INT(GAME_STATUS_NOT_FOUND, status);
}

/** Невалидная сохранённая длина UID заставляет загрузку игнорировать запись. */
void test_game_storage_load_user_skips_record_with_invalid_stored_uid_length(void)
{
    GameUserId id = make_user_id(21u);
    GameUser loaded = {0};
    GameStatus status;

    arrange_saved_record_with_uid_length(21u, 0u);

    status = game_storage_load_user(&id, &loaded);

    TEST_ASSERT_EQUAL_INT(GAME_STATUS_NOT_FOUND, status);
}

/** Другая длина UID не совпадает с сохранённым пользователем. */
void test_game_storage_load_user_skips_record_with_different_uid_length(void)
{
    GameUser saved = arrange_saved_user(22u, 4u, 20u);
    GameUserId id = saved.id;
    GameUser loaded = {0};
    GameStatus status;

    id.length = 5u;
    id.bytes[4] = 0x99u;

    status = game_storage_load_user(&id, &loaded);

    TEST_ASSERT_EQUAL_INT(GAME_STATUS_NOT_FOUND, status);
}

/** Другой хэш UID не совпадает с сохранённым пользователем. */
void test_game_storage_load_user_skips_record_with_different_uid_hash(void)
{
    GameUserId id = make_user_id(24u);
    GameUser loaded = {0};
    GameStatus status;

    arrange_saved_user(23u, 4u, 20u);

    status = game_storage_load_user(&id, &loaded);

    TEST_ASSERT_EQUAL_INT(GAME_STATUS_NOT_FOUND, status);
}

/** Другие байты UID не совпадают даже при сохранённом хэше. */
void test_game_storage_load_user_skips_record_with_different_uid_bytes(void)
{
    GameUser saved = arrange_saved_user(25u, 4u, 20u);
    GameUser loaded = {0};
    GameStatus status;

    fake_game_storage_backend_corrupt_byte(0u, (uint8_t)offsetof(GameStorageRecord, uid), 0x01u);

    status = game_storage_load_user(&saved.id, &loaded);

    TEST_ASSERT_EQUAL_INT(GAME_STATUS_NOT_FOUND, status);
}

/** Сохранение в совпадающий повреждённый слот возвращает ошибку CRC и сохраняет данные. */
void test_game_storage_save_user_rewrites_matching_corrupt_slot_and_reports_crc_error(void)
{
    GameUser old_user = arrange_saved_user(5u, 2u, 10u);
    GameUser updated = make_user(5u, 44u, 9u);
    GameUser loaded = {0};
    GameStatus save_status;
    GameStatus load_status;

    fake_game_storage_backend_corrupt_byte(0u, (uint8_t)offsetof(GameStorageRecord, stats_rounds), 0x01u);

    save_status = game_storage_save_user(&updated, 9u);
    load_status = game_storage_load_user(&old_user.id, &loaded);

    TEST_ASSERT_EQUAL_INT(GAME_STATUS_CRC_ERROR, save_status);
    TEST_ASSERT_EQUAL_INT(GAME_STATUS_OK, load_status);
    TEST_ASSERT_EQUAL_UINT8(1u, fake_game_storage_backend_count_written_slots());
    assert_loaded_user_matches(&updated, &loaded, 9u);
}

/** Неизвестные переиспользуемые слоты выбираются раньше вытеснения валидных слотов. */
void test_game_storage_save_user_reuses_unknown_slot_before_valid_eviction(void)
{
    GameUser new_user = make_user(60u, 88u, 12u);
    GameUserId eviction_candidate_id = make_user_id(2u);
    GameUser loaded_new = {0};
    GameUser loaded_candidate = {0};
    GameStatus save_status;
    GameStatus new_load_status;
    GameStatus candidate_load_status;

    arrange_full_storage_for_reuse();
    fake_game_storage_backend_corrupt_byte(7u, (uint8_t)offsetof(GameStorageRecord, magic), 0x01u);
    fake_game_storage_backend_corrupt_byte(8u, (uint8_t)offsetof(GameStorageRecord, magic), 0x01u);

    save_status = game_storage_save_user(&new_user, 12u);
    new_load_status = game_storage_load_user(&new_user.id, &loaded_new);
    candidate_load_status = game_storage_load_user(&eviction_candidate_id, &loaded_candidate);

    TEST_ASSERT_EQUAL_INT(GAME_STATUS_CRC_ERROR, save_status);
    TEST_ASSERT_EQUAL_INT(GAME_STATUS_OK, new_load_status);
    TEST_ASSERT_EQUAL_INT(GAME_STATUS_OK, candidate_load_status);
    assert_loaded_user_matches(&new_user, &loaded_new, 12u);
}

/** Несовпадающий повреждённый слот переиспользуется раньше вытеснения валидного слота. */
void test_game_storage_save_user_reuses_nonmatching_corrupt_slot_before_valid_eviction(void)
{
    GameUser new_user = make_user(62u, 88u, 13u);
    GameUserId eviction_candidate_id = make_user_id(2u);
    GameUser loaded_new = {0};
    GameUser loaded_candidate = {0};
    GameStatus save_status;
    GameStatus new_load_status;
    GameStatus candidate_load_status;

    arrange_full_storage_for_reuse();
    fake_game_storage_backend_corrupt_byte(6u, (uint8_t)offsetof(GameStorageRecord, stats_rounds), 0x01u);

    save_status = game_storage_save_user(&new_user, 13u);
    new_load_status = game_storage_load_user(&new_user.id, &loaded_new);
    candidate_load_status = game_storage_load_user(&eviction_candidate_id, &loaded_candidate);

    TEST_ASSERT_EQUAL_INT(GAME_STATUS_CRC_ERROR, save_status);
    TEST_ASSERT_EQUAL_INT(GAME_STATUS_OK, new_load_status);
    TEST_ASSERT_EQUAL_INT(GAME_STATUS_OK, candidate_load_status);
    assert_loaded_user_matches(&new_user, &loaded_new, 13u);
}

/** Полное хранилище вытесняет минимум сессий, затем минимум раундов. */
void test_game_storage_save_user_evicts_lowest_sessions_then_lowest_rounds_when_full(void)
{
    GameUser new_user = make_user(61u, 77u, 50u);
    GameUserId evicted_id = make_user_id(2u);
    GameUserId kept_tie_id = make_user_id(3u);
    GameUser loaded_new = {0};
    GameUser loaded_tie = {0};
    GameStatus save_status;
    GameStatus evicted_load_status;
    GameStatus tie_load_status;
    GameStatus new_load_status;

    arrange_full_storage_for_reuse();

    save_status = game_storage_save_user(&new_user, 50u);
    evicted_load_status = game_storage_load_user(&evicted_id, &loaded_new);
    tie_load_status = game_storage_load_user(&kept_tie_id, &loaded_tie);
    new_load_status = game_storage_load_user(&new_user.id, &loaded_new);

    TEST_ASSERT_EQUAL_INT(GAME_STATUS_STORAGE_FULL, save_status);
    TEST_ASSERT_EQUAL_INT(GAME_STATUS_NOT_FOUND, evicted_load_status);
    TEST_ASSERT_EQUAL_INT(GAME_STATUS_OK, tie_load_status);
    TEST_ASSERT_EQUAL_INT(GAME_STATUS_OK, new_load_status);
    assert_loaded_user_matches(&new_user, &loaded_new, 50u);
}

/** Прямая инициализация возвращает ошибку инициализации бэкенда. */
void test_game_storage_init_returns_storage_error_when_backend_init_fails(void)
{
    GameStatus status;

    fake_game_storage_backend_set_init_status(GAME_STATUS_STORAGE_ERROR);

    status = game_storage_init();

    TEST_ASSERT_EQUAL_INT(GAME_STATUS_STORAGE_ERROR, status);
}

/** Загрузка возвращает ошибку инициализации бэкенда до чтения слотов. */
void test_game_storage_load_user_returns_storage_error_when_backend_init_fails(void)
{
    GameUserId id = make_user_id(6u);
    GameUser user = {0};
    GameStatus status;

    fake_game_storage_backend_set_init_status(GAME_STATUS_STORAGE_ERROR);

    status = game_storage_load_user(&id, &user);

    TEST_ASSERT_EQUAL_INT(GAME_STATUS_STORAGE_ERROR, status);
}

/** Сохранение возвращает ошибку инициализации бэкенда до сканирования слотов. */
void test_game_storage_save_user_returns_storage_error_when_backend_init_fails(void)
{
    GameUser user = make_user(6u, 12u, 2u);
    GameStatus status;

    fake_game_storage_backend_set_init_status(GAME_STATUS_STORAGE_ERROR);

    status = game_storage_save_user(&user, 2u);

    TEST_ASSERT_EQUAL_INT(GAME_STATUS_STORAGE_ERROR, status);
}

/** Загрузка возвращает ошибку чтения бэкенда. */
void test_game_storage_load_user_returns_storage_error_when_backend_read_fails(void)
{
    GameUserId id = make_user_id(6u);
    GameUser user = {0};
    GameStatus status;

    fake_game_storage_backend_set_read_status(GAME_STATUS_STORAGE_ERROR);

    status = game_storage_load_user(&id, &user);

    TEST_ASSERT_EQUAL_INT(GAME_STATUS_STORAGE_ERROR, status);
}

/** Сохранение возвращает ошибку чтения при сканировании слотов. */
void test_game_storage_save_user_returns_storage_error_when_backend_read_fails(void)
{
    GameUser user = make_user(7u, 12u, 2u);
    GameStatus status;

    fake_game_storage_backend_set_read_status(GAME_STATUS_STORAGE_ERROR);

    status = game_storage_save_user(&user, 2u);

    TEST_ASSERT_EQUAL_INT(GAME_STATUS_STORAGE_ERROR, status);
}

/** Сохранение возвращает ошибку стирания выбранного слота. */
void test_game_storage_save_user_returns_storage_error_when_backend_erase_fails(void)
{
    GameUser user = make_user(8u, 12u, 2u);
    GameStatus status;

    fake_game_storage_backend_set_erase_status(GAME_STATUS_STORAGE_ERROR);

    status = game_storage_save_user(&user, 2u);

    TEST_ASSERT_EQUAL_INT(GAME_STATUS_STORAGE_ERROR, status);
}

/** Сохранение возвращает ошибку записи выбранного слота. */
void test_game_storage_save_user_returns_storage_error_when_backend_write_fails(void)
{
    GameUser user = make_user(9u, 12u, 2u);
    GameStatus status;

    fake_game_storage_backend_set_write_status(GAME_STATUS_STORAGE_ERROR);

    status = game_storage_save_user(&user, 2u);

    TEST_ASSERT_EQUAL_INT(GAME_STATUS_STORAGE_ERROR, status);
}

/** Сохранение возвращает ошибку проверочного чтения после записи. */
void test_game_storage_save_user_returns_storage_error_when_verify_read_fails(void)
{
    GameUser user = make_user(26u, 12u, 2u);
    GameStatus status;

    fake_game_storage_backend_set_read_error_after(GAME_EEPROM_SLOT_COUNT);

    status = game_storage_save_user(&user, 2u);

    TEST_ASSERT_EQUAL_INT(GAME_STATUS_STORAGE_ERROR, status);
}

/** Сохранение возвращает ошибку проверки, если бэкенд записал другую валидную запись. */
void test_game_storage_save_user_returns_storage_error_when_written_record_differs(void)
{
    GameUser existing = arrange_saved_user(28u, 1u, 10u);
    GameUser user = make_user(29u, 12u, 2u);
    GameStorageRecord replacement;
    GameStatus status;

    fake_game_storage_backend_fetch_record(0u, &replacement);
    fake_game_storage_backend_replace_writes_with(&replacement);

    status = game_storage_save_user(&user, 2u);

    TEST_ASSERT_EQUAL_INT(GAME_STATUS_STORAGE_ERROR, status);
    TEST_ASSERT_EQUAL_UINT8(existing.id.length, replacement.uid_len);
}

/** Сохранение возвращает ошибку проверки, если записанная запись повреждена. */
TEST_CASE((uint8_t)offsetof(GameStorageRecord, magic), 0x01u)
TEST_CASE((uint8_t)offsetof(GameStorageRecord, version), 0x01u)
TEST_CASE((uint8_t)offsetof(GameStorageRecord, size), 0x01u)
TEST_CASE((uint8_t)offsetof(GameStorageRecord, uid_len), 0xFFu)
TEST_CASE((uint8_t)offsetof(GameStorageRecord, stats_rounds), 0x01u)
void test_game_storage_save_user_returns_storage_error_when_written_record_fails_verify(
    uint8_t corrupt_offset,
    uint8_t corrupt_mask)
{
    GameUser user = make_user(27u, 12u, 2u);
    GameStatus status;

    fake_game_storage_backend_enable_write_corruption(corrupt_offset, corrupt_mask);

    status = game_storage_save_user(&user, 2u);

    TEST_ASSERT_EQUAL_INT(GAME_STATUS_STORAGE_ERROR, status);
}

/** Загрузка отвергает null-аргументы. */
void test_game_storage_load_user_rejects_null_arguments(void)
{
    GameUserId id = make_user_id(10u);
    GameUser user = {0};
    GameStatus null_id_status;
    GameStatus null_user_status;

    null_id_status = game_storage_load_user(NULL, &user);
    null_user_status = game_storage_load_user(&id, NULL);

    TEST_ASSERT_EQUAL_INT(GAME_STATUS_INVALID_ARG, null_id_status);
    TEST_ASSERT_EQUAL_INT(GAME_STATUS_INVALID_ARG, null_user_status);
}

/** Загрузка отвергает невалидные длины UID. */
TEST_CASE(0u)
TEST_CASE((uint8_t)(GAME_USER_ID_MAX_BYTES + 1u))
void test_game_storage_load_user_rejects_invalid_id_length(uint8_t length)
{
    GameUserId id = make_user_id(11u);
    GameUser user = {0};
    GameStatus status;

    id.length = length;

    status = game_storage_load_user(&id, &user);

    TEST_ASSERT_EQUAL_INT(GAME_STATUS_INVALID_ARG, status);
}

/** Сохранение отвергает null-пользователя. */
void test_game_storage_save_user_rejects_null_user(void)
{
    GameStatus status;

    status = game_storage_save_user(NULL, 1u);

    TEST_ASSERT_EQUAL_INT(GAME_STATUS_INVALID_ARG, status);
}

/** Сохранение отвергает невалидные длины UID. */
TEST_CASE(0u)
TEST_CASE((uint8_t)(GAME_USER_ID_MAX_BYTES + 1u))
void test_game_storage_save_user_rejects_invalid_id_length(uint8_t length)
{
    GameUser user = make_user(12u, 10u, 1u);
    GameStatus status;

    user.id.length = length;

    status = game_storage_save_user(&user, 1u);

    TEST_ASSERT_EQUAL_INT(GAME_STATUS_INVALID_ARG, status);
}
