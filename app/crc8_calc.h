#ifndef CRC8_CALC_H
#define CRC8_CALC_H

#include "app_types.h"

uint8_t CRC8_IsChecksumCorrect(ByteArray* byteArray);
uint8_t CRC8_IsPartialChecksumCorrect(ByteArray* byteArray, uint8_t startingWith);
uint8_t CRC8_CalculateChecksumOf(uint8_t* data, uint8_t length);
uint8_t CRC8_CalculateChecksumAndAppendTo(ByteArray* byteArray);
uint8_t CRC8_CalculatePartialChecksumAndAppendTo(ByteArray* data, uint8_t startingWith);
void CRC8_ExcludeChecksum(ByteArray* byteArray);

#endif /* INCLUDE_HELPERS_CRC8_CALCULATOR_H_ */
