#include "uid_hash.h"

#include <stddef.h>

#define UID_HASH_FNV_OFFSET 2166136261u
#define UID_HASH_FNV_PRIME  16777619u

uint32_t uid_hash32(const GameUserId *id)
{
    uint32_t hash = UID_HASH_FNV_OFFSET;
    uint8_t index;

    if ((id == NULL) || (id->length == 0u) || (id->length > GAME_USER_ID_MAX_BYTES)) {
        return 0u;
    }

    hash = (hash ^ id->length) * UID_HASH_FNV_PRIME;
    for (index = 0u; index < id->length; ++index) {
        hash = (hash ^ id->bytes[index]) * UID_HASH_FNV_PRIME;
    }

    return hash;
}

uint16_t uid_hash16(const GameUserId *id)
{
    return (uint16_t)uid_hash32(id);
}
