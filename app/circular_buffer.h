#ifndef CIRCULAR_BUFFER_H
#define CIRCULAR_BUFFER_H

#include "app_types.h"

typedef struct CircularBuffer* ByteCircularBuffer;

ByteCircularBuffer ByteCircularBuffer_Create(uint8_t capacity);
void ByteCircularBuffer_Destroy(ByteCircularBuffer circularBuffer);
uint8_t ByteCircularBuffer_GetSize(ByteCircularBuffer circularBuffer);
bool ByteCircularBuffer_IsEmpty(ByteCircularBuffer circularBuffer);
bool ByteCircularBuffer_Push(ByteCircularBuffer circularBuffer, uint8_t value);
bool ByteCircularBuffer_PushFromISR(ByteCircularBuffer circularBuffer, uint8_t value);
uint8_t ByteCircularBuffer_Pop(ByteCircularBuffer circularBuffer);
uint8_t ByteCircularBuffer_PopFromISR(ByteCircularBuffer circularBuffer);

#endif 
