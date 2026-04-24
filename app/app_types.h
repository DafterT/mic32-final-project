#ifndef BYTE_ARRAY_H
#define BYTE_ARRAY_H

#define MAX_BYTE_BUFFER_LENGTH 255

#include "inttypes.h"
#include "stdbool.h"

typedef struct {
	uint8_t length;
	uint8_t byteArray[MAX_BYTE_BUFFER_LENGTH];
} ByteArray;

#endif
