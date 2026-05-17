#ifndef APP_MATH_UID_HASH_H
#define APP_MATH_UID_HASH_H

#include "game.h"

#include <stdint.h>

uint32_t uid_hash32(const GameUserId *id);
uint16_t uid_hash16(const GameUserId *id);

#endif
