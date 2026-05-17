#include "unity.h"

#include "uid_hash.h"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_uid_hash_returns_legacy_hash_for_known_uid(void)
{
    const GameUserId id = {
        .bytes = {0xDEu, 0xADu, 0xBEu, 0xEFu},
        .length = 4u,
    };

    TEST_ASSERT_EQUAL_HEX32(0x84649C8Du, uid_hash32(&id));
    TEST_ASSERT_EQUAL_HEX16(0x9C8Du, uid_hash16(&id));
}

void test_uid_hash_includes_uid_length(void)
{
    const GameUserId short_id = {
        .bytes = {0x01u, 0x02u},
        .length = 2u,
    };
    const GameUserId long_id = {
        .bytes = {0x01u, 0x02u, 0x00u},
        .length = 3u,
    };

    TEST_ASSERT_EQUAL_HEX32(0xA47BE70Au, uid_hash32(&short_id));
    TEST_ASSERT_EQUAL_HEX32(0x26ADDC43u, uid_hash32(&long_id));
    TEST_ASSERT_NOT_EQUAL(uid_hash32(&short_id), uid_hash32(&long_id));
}

void test_uid_hash16_returns_low_16_bits_of_hash32(void)
{
    const GameUserId id = {
        .bytes = {0x01u, 0x02u, 0x03u, 0x04u},
        .length = 4u,
    };

    TEST_ASSERT_EQUAL_HEX32(0x9781744Bu, uid_hash32(&id));
    TEST_ASSERT_EQUAL_HEX16(0x744Bu, uid_hash16(&id));
}

void test_uid_hash_returns_zero_for_invalid_id(void)
{
    GameUserId empty_id = {.length = 0u};
    GameUserId too_long_id = {.length = (uint8_t)(GAME_USER_ID_MAX_BYTES + 1u)};

    TEST_ASSERT_EQUAL_UINT32(0u, uid_hash32(NULL));
    TEST_ASSERT_EQUAL_UINT16(0u, uid_hash16(NULL));
    TEST_ASSERT_EQUAL_UINT32(0u, uid_hash32(&empty_id));
    TEST_ASSERT_EQUAL_UINT16(0u, uid_hash16(&empty_id));
    TEST_ASSERT_EQUAL_UINT32(0u, uid_hash32(&too_long_id));
    TEST_ASSERT_EQUAL_UINT16(0u, uid_hash16(&too_long_id));
}
