#include "mfrc522.h"

#include "mik32_hal.h"
#include "mik32_hal_pcc.h"

#include <stddef.h>

static void __attribute__((section(".ram_text.mfrc522"))) mfrc522_select(MFRC522 *mfrc522)
{
    HAL_SPI_Enable(mfrc522->spi);
    HAL_GPIO_WritePin(mfrc522->_chipSelectPort, mfrc522->_chipSelectPin, GPIO_PIN_LOW);
}

static void __attribute__((section(".ram_text.mfrc522"))) mfrc522_unselect(MFRC522 *mfrc522)
{
    HAL_GPIO_WritePin(mfrc522->_chipSelectPort, mfrc522->_chipSelectPin, GPIO_PIN_HIGH);
    HAL_SPI_Disable(mfrc522->spi);
}

static byte __attribute__((section(".ram_text.mfrc522"))) mfrc522_transfer(MFRC522 *mfrc522, byte value)
{
    byte rx = 0;
    byte tx = value;

    if (HAL_SPI_Exchange(mfrc522->spi, &tx, &rx, 1u, SPI_TIMEOUT_DEFAULT) != HAL_OK) {
        HAL_SPI_ClearError(mfrc522->spi);
    }
    return rx;
}

void MFRC522_Init(MFRC522 *mfrc522,
                  SPI_HandleTypeDef *spi,
                  GPIO_TypeDef *chipSelectPort,
                  HAL_PinsTypeDef chipSelectPin,
                  GPIO_TypeDef *resetPowerDownPort,
                  HAL_PinsTypeDef resetPowerDownPin)
{
    mfrc522->uid.size = 0;
    mfrc522->uid.sak = 0;
    mfrc522->spi = spi;
    mfrc522->_chipSelectPort = chipSelectPort;
    mfrc522->_chipSelectPin = chipSelectPin;
    mfrc522->_resetPowerDownPort = resetPowerDownPort;
    mfrc522->_resetPowerDownPin = resetPowerDownPin;

    __HAL_PCC_GPIO_0_CLK_ENABLE();
    __HAL_PCC_GPIO_1_CLK_ENABLE();
    __HAL_PCC_GPIO_2_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = chipSelectPin;
    gpio.Mode = HAL_GPIO_MODE_GPIO_OUTPUT;
    gpio.Pull = HAL_GPIO_PULL_NONE;
    HAL_GPIO_Init(chipSelectPort, &gpio);
    HAL_GPIO_WritePin(chipSelectPort, chipSelectPin, GPIO_PIN_HIGH);

    if (resetPowerDownPort != NULL) {
        gpio.Pin = resetPowerDownPin;
        gpio.Mode = HAL_GPIO_MODE_GPIO_OUTPUT;
        gpio.Pull = HAL_GPIO_PULL_NONE;
        HAL_GPIO_Init(resetPowerDownPort, &gpio);
        HAL_GPIO_WritePin(resetPowerDownPort, resetPowerDownPin, GPIO_PIN_HIGH);
    }
}

void __attribute__((section(".ram_text.mfrc522"))) PCD_WriteRegister(MFRC522 *mfrc522, PCD_Register reg, byte value)
{
    mfrc522_select(mfrc522);
    mfrc522_transfer(mfrc522, (byte)reg);
    mfrc522_transfer(mfrc522, value);
    mfrc522_unselect(mfrc522);
}

void __attribute__((section(".ram_text.mfrc522"))) PCD_WriteRegisterMany(MFRC522 *mfrc522, PCD_Register reg, byte count, byte *values)
{
    byte index;

    mfrc522_select(mfrc522);
    mfrc522_transfer(mfrc522, (byte)reg);
    for (index = 0; index < count; index++) {
        mfrc522_transfer(mfrc522, values[index]);
    }
    mfrc522_unselect(mfrc522);
}

byte __attribute__((section(".ram_text.mfrc522"))) PCD_ReadRegister(MFRC522 *mfrc522, PCD_Register reg)
{
    byte value;

    mfrc522_select(mfrc522);
    mfrc522_transfer(mfrc522, (byte)(0x80 | reg));
    value = mfrc522_transfer(mfrc522, 0);
    mfrc522_unselect(mfrc522);
    return value;
}

void __attribute__((section(".ram_text.mfrc522"))) PCD_ReadRegisterMany(MFRC522 *mfrc522, PCD_Register reg, byte count, byte *values, byte rxAlign)
{
    byte address;
    byte index;

    if (count == 0) {
        return;
    }

    address = (byte)(0x80 | reg);
    index = 0;
    mfrc522_select(mfrc522);
    count--;
    mfrc522_transfer(mfrc522, address);
    if (rxAlign) {
        byte mask = (byte)((0xFFu << rxAlign) & 0xFFu);
        byte value = mfrc522_transfer(mfrc522, address);
        values[0] = (byte)((values[0] & ~mask) | (value & mask));
        index++;
    }
    while (index < count) {
        values[index] = mfrc522_transfer(mfrc522, address);
        index++;
    }
    values[index] = mfrc522_transfer(mfrc522, 0);
    mfrc522_unselect(mfrc522);
}

void __attribute__((section(".ram_text.mfrc522"))) PCD_SetRegisterBitMask(MFRC522 *mfrc522, PCD_Register reg, byte mask)
{
    byte tmp;

    tmp = PCD_ReadRegister(mfrc522, reg);
    PCD_WriteRegister(mfrc522, reg, (byte)(tmp | mask));
}

void __attribute__((section(".ram_text.mfrc522"))) PCD_ClearRegisterBitMask(MFRC522 *mfrc522, PCD_Register reg, byte mask)
{
    byte tmp;

    tmp = PCD_ReadRegister(mfrc522, reg);
    PCD_WriteRegister(mfrc522, reg, (byte)(tmp & (~mask)));
}

StatusCode __attribute__((section(".ram_text.mfrc522"))) PCD_CalculateCRC(MFRC522 *mfrc522, byte *data, byte length, byte *result)
{
    const uint32_t deadline = HAL_Millis() + 89u;

    PCD_WriteRegister(mfrc522, CommandReg, PCD_Idle);
    PCD_WriteRegister(mfrc522, DivIrqReg, 0x04);
    PCD_WriteRegister(mfrc522, FIFOLevelReg, 0x80);
    PCD_WriteRegisterMany(mfrc522, FIFODataReg, length, data);
    PCD_WriteRegister(mfrc522, CommandReg, PCD_CalcCRC);

    do {
        byte n = PCD_ReadRegister(mfrc522, DivIrqReg);
        if (n & 0x04) {
            PCD_WriteRegister(mfrc522, CommandReg, PCD_Idle);
            result[0] = PCD_ReadRegister(mfrc522, CRCResultRegL);
            result[1] = PCD_ReadRegister(mfrc522, CRCResultRegH);
            return STATUS_OK;
        }
    } while ((uint32_t)HAL_Millis() < deadline);

    return STATUS_TIMEOUT;
}

void PCD_Init(MFRC522 *mfrc522)
{
    bool hardReset = false;

    HAL_GPIO_WritePin(mfrc522->_chipSelectPort, mfrc522->_chipSelectPin, GPIO_PIN_HIGH);

    if (mfrc522->_resetPowerDownPort != NULL) {
        HAL_GPIO_WritePin(mfrc522->_resetPowerDownPort, mfrc522->_resetPowerDownPin, GPIO_PIN_LOW);
        HAL_DelayUs(2);
        HAL_GPIO_WritePin(mfrc522->_resetPowerDownPort, mfrc522->_resetPowerDownPin, GPIO_PIN_HIGH);
        HAL_DelayMs(50);
        hardReset = true;
    }

    if (!hardReset) {
        PCD_Reset(mfrc522);
    }

    PCD_WriteRegister(mfrc522, TxModeReg, 0x00);
    PCD_WriteRegister(mfrc522, RxModeReg, 0x00);
    PCD_WriteRegister(mfrc522, ModWidthReg, 0x26);
    PCD_WriteRegister(mfrc522, TModeReg, 0x80);
    PCD_WriteRegister(mfrc522, TPrescalerReg, 0xA9);
    PCD_WriteRegister(mfrc522, TReloadRegH, 0x03);
    PCD_WriteRegister(mfrc522, TReloadRegL, 0xE8);
    PCD_WriteRegister(mfrc522, TxASKReg, 0x40);
    PCD_WriteRegister(mfrc522, ModeReg, 0x3D);
    PCD_AntennaOn(mfrc522);
}

void PCD_Reset(MFRC522 *mfrc522)
{
    uint8_t count = 0;

    PCD_WriteRegister(mfrc522, CommandReg, PCD_SoftReset);
    do {
        HAL_DelayMs(50);
    } while ((PCD_ReadRegister(mfrc522, CommandReg) & (1 << 4)) && (++count) < 3);
}

void PCD_AntennaOn(MFRC522 *mfrc522)
{
    byte value = PCD_ReadRegister(mfrc522, TxControlReg);
    if ((value & 0x03) != 0x03) {
        PCD_WriteRegister(mfrc522, TxControlReg, (byte)(value | 0x03));
    }
}

void PCD_AntennaOff(MFRC522 *mfrc522)
{
    PCD_ClearRegisterBitMask(mfrc522, TxControlReg, 0x03);
}

byte PCD_GetAntennaGain(MFRC522 *mfrc522)
{
    return (byte)(PCD_ReadRegister(mfrc522, RFCfgReg) & (0x07 << 4));
}

void PCD_SetAntennaGain(MFRC522 *mfrc522, byte mask)
{
    if (PCD_GetAntennaGain(mfrc522) != mask) {
        PCD_ClearRegisterBitMask(mfrc522, RFCfgReg, (0x07 << 4));
        PCD_SetRegisterBitMask(mfrc522, RFCfgReg, (byte)(mask & (0x07 << 4)));
    }
}

StatusCode PCD_TransceiveData(MFRC522 *mfrc522,
                               byte *sendData,
                               byte sendLen,
                               byte *backData,
                               byte *backLen,
                               byte *validBits,
                               byte rxAlign,
                               bool checkCRC)
{
    byte waitIRq = 0x30;
    return PCD_CommunicateWithPICC(mfrc522, PCD_Transceive, waitIRq, sendData, sendLen, backData, backLen, validBits, rxAlign, checkCRC);
}

StatusCode __attribute__((section(".ram_text.mfrc522"))) PCD_CommunicateWithPICC(MFRC522 *mfrc522,
                                    byte command,
                                    byte waitIRq,
                                    byte *sendData,
                                    byte sendLen,
                                    byte *backData,
                                    byte *backLen,
                                    byte *validBits,
                                    byte rxAlign,
                                    bool checkCRC)
{
    byte txLastBits = validBits ? *validBits : 0;
    byte bitFraming = (byte)((rxAlign << 4) + txLastBits);
    const uint32_t deadline = HAL_Millis() + 36u;
    bool completed = false;
    byte errorRegValue;
    byte _validBits = 0;

    PCD_WriteRegister(mfrc522, CommandReg, PCD_Idle);
    PCD_WriteRegister(mfrc522, ComIrqReg, 0x7F);
    PCD_WriteRegister(mfrc522, FIFOLevelReg, 0x80);
    PCD_WriteRegisterMany(mfrc522, FIFODataReg, sendLen, sendData);
    PCD_WriteRegister(mfrc522, BitFramingReg, bitFraming);
    PCD_WriteRegister(mfrc522, CommandReg, command);
    if (command == PCD_Transceive) {
        PCD_SetRegisterBitMask(mfrc522, BitFramingReg, 0x80);
    }

    do {
        byte n = PCD_ReadRegister(mfrc522, ComIrqReg);
        if (n & waitIRq) {
            completed = true;
            break;
        }
        if (n & 0x01) {
            return STATUS_TIMEOUT;
        }
    } while ((uint32_t)HAL_Millis() < deadline);

    if (!completed) {
        return STATUS_TIMEOUT;
    }

    errorRegValue = PCD_ReadRegister(mfrc522, ErrorReg);
    if (errorRegValue & 0x13) {
        return STATUS_ERROR;
    }

    if (backData && backLen) {
        byte n = PCD_ReadRegister(mfrc522, FIFOLevelReg);
        if (n > *backLen) {
            return STATUS_NO_ROOM;
        }
        *backLen = n;
        PCD_ReadRegisterMany(mfrc522, FIFODataReg, n, backData, rxAlign);
        _validBits = (byte)(PCD_ReadRegister(mfrc522, ControlReg) & 0x07);
        if (validBits) {
            *validBits = _validBits;
        }
    }

    if (errorRegValue & 0x08) {
        return STATUS_COLLISION;
    }

    if (backData && backLen && checkCRC) {
        byte controlBuffer[2];
        StatusCode status;

        if (*backLen == 1 && _validBits == 4) {
            return STATUS_MIFARE_NACK;
        }
        if (*backLen < 2 || _validBits != 0) {
            return STATUS_CRC_WRONG;
        }
        status = PCD_CalculateCRC(mfrc522, &backData[0], (byte)(*backLen - 2), &controlBuffer[0]);
        if (status != STATUS_OK) {
            return status;
        }
        if ((backData[*backLen - 2] != controlBuffer[0]) || (backData[*backLen - 1] != controlBuffer[1])) {
            return STATUS_CRC_WRONG;
        }
    }

    return STATUS_OK;
}

StatusCode PICC_RequestA(MFRC522 *mfrc522, byte *bufferATQA, byte *bufferSize)
{
    return PICC_REQA_or_WUPA(mfrc522, PICC_CMD_REQA, bufferATQA, bufferSize);
}

StatusCode PICC_WakeupA(MFRC522 *mfrc522, byte *bufferATQA, byte *bufferSize)
{
    return PICC_REQA_or_WUPA(mfrc522, PICC_CMD_WUPA, bufferATQA, bufferSize);
}

StatusCode PICC_REQA_or_WUPA(MFRC522 *mfrc522, byte command, byte *bufferATQA, byte *bufferSize)
{
    byte validBits;
    StatusCode status;

    if (bufferATQA == NULL || *bufferSize < 2) {
        return STATUS_NO_ROOM;
    }
    PCD_ClearRegisterBitMask(mfrc522, CollReg, 0x80);
    validBits = 7;
    status = PCD_TransceiveData(mfrc522, &command, 1, bufferATQA, bufferSize, &validBits, 0, false);
    if (status != STATUS_OK) {
        return status;
    }
    if (*bufferSize != 2 || validBits != 0) {
        return STATUS_ERROR;
    }
    return STATUS_OK;
}

StatusCode PICC_Select(MFRC522 *mfrc522, Uid *uid, byte validBits)
{
    bool uidComplete;
    bool selectDone;
    bool useCascadeTag;
    byte cascadeLevel = 1;
    StatusCode result;
    byte count;
    byte checkBit;
    byte index;
    byte uidIndex;
    int8_t currentLevelKnownBits;
    byte buffer[9];
    byte bufferUsed;
    byte rxAlign;
    byte txLastBits;
    byte *responseBuffer;
    byte responseLength;
    byte bytesToCopy;

    if (validBits > 80) {
        return STATUS_INVALID;
    }

    PCD_ClearRegisterBitMask(mfrc522, CollReg, 0x80);

    uidComplete = false;
    while (!uidComplete) {
        switch (cascadeLevel) {
        case 1:
            buffer[0] = PICC_CMD_SEL_CL1;
            uidIndex = 0;
            useCascadeTag = validBits && uid->size > 4;
            break;
        case 2:
            buffer[0] = PICC_CMD_SEL_CL2;
            uidIndex = 3;
            useCascadeTag = validBits && uid->size > 7;
            break;
        case 3:
            buffer[0] = PICC_CMD_SEL_CL3;
            uidIndex = 6;
            useCascadeTag = false;
            break;
        default:
            return STATUS_INTERNAL_ERROR;
        }

        currentLevelKnownBits = (int8_t)(validBits - (8 * uidIndex));
        if (currentLevelKnownBits < 0) {
            currentLevelKnownBits = 0;
        }

        index = 2;
        if (useCascadeTag) {
            buffer[index++] = PICC_CMD_CT;
        }
        bytesToCopy = (byte)(currentLevelKnownBits / 8 + (currentLevelKnownBits % 8 ? 1 : 0));
        if (bytesToCopy) {
            byte maxBytes = useCascadeTag ? 3 : 4;
            if (bytesToCopy > maxBytes) {
                bytesToCopy = maxBytes;
            }
            for (count = 0; count < bytesToCopy; count++) {
                buffer[index++] = uid->uidByte[uidIndex + count];
            }
        }
        if (useCascadeTag) {
            currentLevelKnownBits += 8;
        }

        selectDone = false;
        while (!selectDone) {
            if (currentLevelKnownBits >= 32) {
                buffer[1] = 0x70;
                buffer[6] = (byte)(buffer[2] ^ buffer[3] ^ buffer[4] ^ buffer[5]);
                result = PCD_CalculateCRC(mfrc522, buffer, 7, &buffer[7]);
                if (result != STATUS_OK) {
                    return result;
                }
                txLastBits = 0;
                bufferUsed = 9;
                responseBuffer = &buffer[6];
                responseLength = 3;
            } else {
                txLastBits = (byte)(currentLevelKnownBits % 8);
                count = (byte)(currentLevelKnownBits / 8);
                index = (byte)(2 + count);
                buffer[1] = (byte)((index << 4) + txLastBits);
                bufferUsed = (byte)(index + (txLastBits ? 1 : 0));
                responseBuffer = &buffer[index];
                responseLength = (byte)(sizeof(buffer) - index);
            }

            rxAlign = txLastBits;
            PCD_WriteRegister(mfrc522, BitFramingReg, (byte)((rxAlign << 4) + txLastBits));
            result = PCD_TransceiveData(mfrc522, buffer, bufferUsed, responseBuffer, &responseLength, &txLastBits, rxAlign, false);
            if (result == STATUS_COLLISION) {
                byte valueOfCollReg = PCD_ReadRegister(mfrc522, CollReg);
                byte collisionPos;
                if (valueOfCollReg & 0x20) {
                    return STATUS_COLLISION;
                }
                collisionPos = (byte)(valueOfCollReg & 0x1F);
                if (collisionPos == 0) {
                    collisionPos = 32;
                }
                if (collisionPos <= currentLevelKnownBits) {
                    return STATUS_INTERNAL_ERROR;
                }
                currentLevelKnownBits = (int8_t)collisionPos;
                count = (byte)(currentLevelKnownBits % 8);
                checkBit = (byte)((currentLevelKnownBits - 1) % 8);
                index = (byte)(1 + (currentLevelKnownBits / 8) + (count ? 1 : 0));
                buffer[index] |= (byte)(1 << checkBit);
            } else if (result != STATUS_OK) {
                return result;
            } else {
                if (currentLevelKnownBits >= 32) {
                    selectDone = true;
                } else {
                    currentLevelKnownBits = 32;
                }
            }
        }

        index = (buffer[2] == PICC_CMD_CT) ? 3 : 2;
        bytesToCopy = (buffer[2] == PICC_CMD_CT) ? 3 : 4;
        for (count = 0; count < bytesToCopy; count++) {
            uid->uidByte[uidIndex + count] = buffer[index++];
        }

        if (responseLength != 3 || txLastBits != 0) {
            return STATUS_ERROR;
        }
        result = PCD_CalculateCRC(mfrc522, responseBuffer, 1, &buffer[2]);
        if (result != STATUS_OK) {
            return result;
        }
        if ((buffer[2] != responseBuffer[1]) || (buffer[3] != responseBuffer[2])) {
            return STATUS_CRC_WRONG;
        }
        if (responseBuffer[0] & 0x04) {
            cascadeLevel++;
        } else {
            uidComplete = true;
            uid->sak = responseBuffer[0];
        }
    }

    uid->size = (byte)(3 * cascadeLevel + 1);
    return STATUS_OK;
}

StatusCode PICC_HaltA(MFRC522 *mfrc522)
{
    StatusCode result;
    byte buffer[4];

    buffer[0] = PICC_CMD_HLTA;
    buffer[1] = 0;
    result = PCD_CalculateCRC(mfrc522, buffer, 2, &buffer[2]);
    if (result != STATUS_OK) {
        return result;
    }

    result = PCD_TransceiveData(mfrc522, buffer, (byte)sizeof(buffer), NULL, NULL, NULL, 0, false);
    if (result == STATUS_TIMEOUT) {
        return STATUS_OK;
    }
    if (result == STATUS_OK) {
        return STATUS_ERROR;
    }
    return result;
}

void PCD_StopCrypto1(MFRC522 *mfrc522)
{
    PCD_ClearRegisterBitMask(mfrc522, Status2Reg, 0x08);
}

bool PICC_IsNewCardPresent(MFRC522 *mfrc522)
{
    byte bufferATQA[2];
    byte bufferSize = sizeof(bufferATQA);
    StatusCode result;

    PCD_WriteRegister(mfrc522, TxModeReg, 0x00);
    PCD_WriteRegister(mfrc522, RxModeReg, 0x00);
    PCD_WriteRegister(mfrc522, ModWidthReg, 0x26);

    result = PICC_RequestA(mfrc522, bufferATQA, &bufferSize);
    return (result == STATUS_OK || result == STATUS_COLLISION);
}

bool PICC_ReadCardSerial(MFRC522 *mfrc522)
{
    StatusCode result = PICC_Select(mfrc522, &mfrc522->uid, 0);
    return (result == STATUS_OK);
}

const char *GetStatusCodeName(StatusCode code)
{
    switch (code) {
    case STATUS_OK:
        return "Success.";
    case STATUS_ERROR:
        return "Error in communication.";
    case STATUS_COLLISION:
        return "Collision detected.";
    case STATUS_TIMEOUT:
        return "Timeout in communication.";
    case STATUS_NO_ROOM:
        return "A buffer is not big enough.";
    case STATUS_INTERNAL_ERROR:
        return "Internal error in the code.";
    case STATUS_INVALID:
        return "Invalid argument.";
    case STATUS_CRC_WRONG:
        return "The CRC_A does not match.";
    case STATUS_MIFARE_NACK:
        return "A MIFARE PICC responded with NAK.";
    default:
        return "Unknown error.";
    }
}
