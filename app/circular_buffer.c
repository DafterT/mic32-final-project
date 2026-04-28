#include "circular_buffer.h"
#include "mik32_hal_irq.h"

#define zeroValue 0
#define CAPACITY 255

struct CircularBuffer
{
		uint8_t *values;
		uint8_t capacity;
		volatile uint8_t size;
		volatile uint8_t head;
		volatile uint8_t tail;
};

static struct CircularBuffer circular_buffer;
static uint8_t values_array[CAPACITY];

static uint8_t pop(ByteCircularBuffer buffer);
static bool push(ByteCircularBuffer buffer, uint8_t value);
static bool isFull(ByteCircularBuffer buffer);
static void advanceIndex(volatile uint8_t* index, uint8_t capacity);

ByteCircularBuffer ByteCircularBuffer_Create(uint8_t capacity)
{
	ByteCircularBuffer buffer = &circular_buffer;
	
	if(0 != buffer) {
		buffer->values = values_array;
		buffer->capacity = (capacity == 0 || capacity > CAPACITY) ? CAPACITY : capacity;
		buffer->size   = 0;
		buffer->head   = 0;
		buffer->tail   = 0;
	}
	
	return buffer;
}

void ByteCircularBuffer_Destroy(ByteCircularBuffer buffer)
{
	if(0 != buffer) {
		buffer->size = 0;
		buffer->head = 0;
		buffer->tail = 0;
	}
}

bool ByteCircularBuffer_IsEmpty(ByteCircularBuffer buffer)
{
	return buffer->size == 0;
}

bool ByteCircularBuffer_Push(ByteCircularBuffer buffer, uint8_t value)
{
	bool pushed;

	HAL_IRQ_DisableInterrupts();
	pushed = push(buffer, value);
	HAL_IRQ_EnableInterrupts();

	return pushed;
}

bool ByteCircularBuffer_PushFromISR(ByteCircularBuffer buffer, uint8_t value)
{
	return push(buffer, value);
}

uint8_t ByteCircularBuffer_Pop(ByteCircularBuffer buffer)
{
	uint8_t result;
	
	HAL_IRQ_DisableInterrupts();
	result = pop(buffer);	
	HAL_IRQ_EnableInterrupts();
	
	return result;
}

uint8_t ByteCircularBuffer_PopFromISR(ByteCircularBuffer buffer)
{		
	uint8_t result;
	
	result = pop(buffer);
	
	return result;
}

uint8_t ByteCircularBuffer_GetSize(ByteCircularBuffer buffer)
{
	return buffer->size;
}

static uint8_t pop(ByteCircularBuffer buffer)
{
	uint8_t result;
	
	if(ByteCircularBuffer_IsEmpty(buffer)) 
	{
		result = zeroValue;
	}
	else
	{
		result = buffer->values[buffer->tail];
		advanceIndex(&buffer->tail, buffer->capacity);
		buffer->size--;
	}
	
	return result;
}

static bool push(ByteCircularBuffer buffer, uint8_t value)
{
	if (isFull(buffer)) {
		return false;
	}

	buffer->values[buffer->head] = value;
	advanceIndex(&buffer->head, buffer->capacity);
	buffer->size++;

	return true;
}

static bool isFull(ByteCircularBuffer buffer)
{
	return buffer->size == buffer->capacity;
}

static void advanceIndex(volatile uint8_t* index, uint8_t capacity)
{
	if (((uint8_t)(*index + 1)) >= capacity) {
		*index = 0;
		return;
	}

	(*index)++;
}
