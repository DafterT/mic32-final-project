#include "crc8_calc.h"

#define POLYNOMIAL 0x131	//P(x)=x^8+x^5+x^4+1 = 100110001

static uint8_t calculate(uint8_t *data, uint8_t startingWith, uint8_t length);
static void incrementLengthOf(ByteArray *data);
static void appendCrcTo(ByteArray *data, uint8_t crc);
static uint8_t compare(void);

static uint8_t result;
static uint8_t actual;
static uint8_t expected;

uint8_t CRC8_IsChecksumCorrect(ByteArray *data)
{
	expected 	= data->byteArray[data->length - 1];
	actual 		= calculate(data->byteArray, 0, data->length - 1);
	
	return compare();
}

uint8_t CRC8_IsPartialChecksumCorrect(ByteArray *data, uint8_t startingWith)
{
	expected 	= data->byteArray[data->length - 1];
	actual 		= calculate(data->byteArray, startingWith, data->length - 1);
	
	return compare();
}

uint8_t CRC8_CalculateChecksumOf(uint8_t *data, uint8_t length)
{
	return calculate(data, 0, length);
}

uint8_t CRC8_CalculateChecksumAndAppendTo(ByteArray *data)
{
	return CRC8_CalculatePartialChecksumAndAppendTo(data, 0);
}

uint8_t CRC8_CalculatePartialChecksumAndAppendTo(ByteArray *data, uint8_t startingWith)
{
	uint8_t crc;
	
	crc = calculate(data->byteArray, startingWith, data->length);
	incrementLengthOf(data);
	appendCrcTo(data, crc);
	
	return crc;
}

void CRC8_ExcludeChecksum(ByteArray *byteArray)
{
	byteArray->length--;
}

static uint8_t calculate(uint8_t *data, uint8_t startingWith, uint8_t length)
{
	uint8_t crc = 0;
	
	for(uint8_t i = startingWith; i < length; ++i)
	{
		crc ^= data[i];
		for(uint8_t bit = 8; bit > 0; --bit)
		{
			if(crc & 0x80)
			{
				crc = (crc << 1) ^ POLYNOMIAL;
			}
			else
			{
				crc = (crc << 1);
			}
		}
	}	
	
	return crc;
}

static void incrementLengthOf(ByteArray *data)
{
	data->length++;
}

static void appendCrcTo(ByteArray *data, uint8_t crc)
{
	data->byteArray[data->length - 1] = crc;
}

static uint8_t compare(void)
{
	if(actual == expected)
	{
		result = true;
	}	
	else
	{
		result = false;
	}
	
	return result;
}
