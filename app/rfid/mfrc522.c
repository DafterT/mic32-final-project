#include "mfrc522.h"

#include "mik32_hal.h"
#include "mik32_hal_pcc.h"

#include <stddef.h>

#define MFRC522_RAM_FUNC __attribute__((section(".ram_text.mfrc522")))

#define MFRC522_RESET_PULSE_US      2u
#define MFRC522_RESET_SETTLE_MS     50u
#define MFRC522_SOFT_RESET_POLLS    3u
#define MFRC522_CRC_TIMEOUT_MS      89u
#define MFRC522_TRANSCEIVE_TIMEOUT_MS 36u

#define MFRC522_IRQ_TIMER           0x01u
#define MFRC522_IRQ_CRC             0x04u
#define MFRC522_IRQ_ALL             0x7Fu
#define MFRC522_TRANSCEIVE_WAIT_IRQ 0x30u
#define MFRC522_ERROR_COLLISION     0x08u
#define MFRC522_ERROR_PROTOCOL_MASK 0x13u

#define MFRC522_START_SEND          0x80u
#define MFRC522_FLUSH_FIFO          0x80u
#define MFRC522_READ_ADDRESS        0x80u
#define MFRC522_VALUES_AFTER_COLL   0x80u
#define MFRC522_ANTENNA_ON_MASK     0x03u
#define MFRC522_RX_GAIN_MASK        (0x07u << 4)
#define MFRC522_RX_LAST_BITS_MASK   0x07u
#define MFRC522_MIFARE_NACK_BITS    4u
#define MFRC522_CRYPTO1_ON          0x08u

#define MFRC522_REQA_BITS           7u
#define MFRC522_ATQA_SIZE           2u
#define MFRC522_UID_MAX_BITS        80u
#define MFRC522_CASCADE_LEVEL_BITS  32u
#define MFRC522_SELECT_NVB          0x70u
#define MFRC522_SAK_SIZE            3u
#define MFRC522_SAK_CASCADE_BIT     0x04u
#define MFRC522_COLL_POS_INVALID    0x20u
#define MFRC522_COLL_POS_MASK       0x1Fu

/**
 * Включает clock для GPIO-порта
 */
static void mfrc522_enable_gpio_clock(GPIO_TypeDef *port)
{
    if (port == GPIO_0) {
        __HAL_PCC_GPIO_0_CLK_ENABLE();
    } else if (port == GPIO_1) {
        __HAL_PCC_GPIO_1_CLK_ENABLE();
    } else if (port == GPIO_2) {
        __HAL_PCC_GPIO_2_CLK_ENABLE();
    }
}

/**
 * Начинает SPI transaction с MFRC522.
 * SPI включается до CS low: так требует HAL для ручного GPIO CS.
 */
static void MFRC522_RAM_FUNC mfrc522_select(MFRC522 *mfrc522)
{
    HAL_SPI_Enable(mfrc522->spi);
    HAL_GPIO_WritePin(mfrc522->_chipSelectPort, mfrc522->_chipSelectPin, GPIO_PIN_LOW);
}

/**
 * Завершает SPI transaction.
 * Сначала CS high, потом disable SPI, чтобы ведомый увидел корректный конец frame.
 */
static void MFRC522_RAM_FUNC mfrc522_unselect(MFRC522 *mfrc522)
{
    HAL_GPIO_WritePin(mfrc522->_chipSelectPort, mfrc522->_chipSelectPin, GPIO_PIN_HIGH);
    HAL_SPI_Disable(mfrc522->spi);
}

/**
 * Обменивает один байт по SPI.
 * Это ожидание byte exchange с чипом MFRC522.
 */
static byte MFRC522_RAM_FUNC mfrc522_transfer(MFRC522 *mfrc522, byte value)
{
    byte rx = 0;
    byte tx = value;

    if (HAL_SPI_Exchange(mfrc522->spi, &tx, &rx, 1u, SPI_TIMEOUT_DEFAULT) != HAL_OK) {
        HAL_SPI_ClearError(mfrc522->spi);
    }
    return rx;
}

/**
 * Привязывает C-объект драйвера к SPI, CS и RST, затем готовит GPIO.
 */
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

    mfrc522_enable_gpio_clock(chipSelectPort);
    mfrc522_enable_gpio_clock(resetPowerDownPort);

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = chipSelectPin;
    gpio.Mode = HAL_GPIO_MODE_GPIO_OUTPUT;
    gpio.Pull = HAL_GPIO_PULL_NONE;
    HAL_GPIO_Init(chipSelectPort, &gpio);
    HAL_GPIO_WritePin(chipSelectPort, chipSelectPin, GPIO_PIN_HIGH);

    gpio.Pin = resetPowerDownPin;
    gpio.Mode = HAL_GPIO_MODE_GPIO_OUTPUT;
    gpio.Pull = HAL_GPIO_PULL_NONE;
    HAL_GPIO_Init(resetPowerDownPort, &gpio);
    HAL_GPIO_WritePin(resetPowerDownPort, resetPowerDownPin, GPIO_PIN_HIGH);
}

/**
 * Пишет один регистр MFRC522: address byte с MSB=0, затем data byte.
 */
void MFRC522_RAM_FUNC PCD_WriteRegister(MFRC522 *mfrc522, PCD_Register reg, byte value)
{
    mfrc522_select(mfrc522);
    mfrc522_transfer(mfrc522, (byte)reg);
    mfrc522_transfer(mfrc522, value);
    mfrc522_unselect(mfrc522);
}

/**
 * Пишет несколько байт в один регистр, обычно FIFODataReg.
 * CS держится low весь burst, чтобы MFRC522 видел одну SPI transaction.
 */
void MFRC522_RAM_FUNC PCD_WriteRegisterMany(MFRC522 *mfrc522, PCD_Register reg, byte count, byte *values)
{
    mfrc522_select(mfrc522);
    mfrc522_transfer(mfrc522, (byte)reg);
    for (byte index = 0; index < count; index++) {
        mfrc522_transfer(mfrc522, values[index]);
    }
    mfrc522_unselect(mfrc522);
}

/**
 * Читает один регистр MFRC522.
 * Первый байт задает адрес чтения, второй dummy byte выталкивает значение регистра на MISO.
 */
byte MFRC522_RAM_FUNC PCD_ReadRegister(MFRC522 *mfrc522, PCD_Register reg)
{
    mfrc522_select(mfrc522);
    mfrc522_transfer(mfrc522, (byte)(MFRC522_READ_ADDRESS | reg));
    byte value = mfrc522_transfer(mfrc522, 0);
    mfrc522_unselect(mfrc522);
    return value;
}

/**
 * Читает несколько байт из регистра/FIFO.
 * rxAlign нужен для ISO14443 anticollision, когда ответ начинается не с bit 0 первого байта.
 */
void MFRC522_RAM_FUNC PCD_ReadRegisterMany(MFRC522 *mfrc522, PCD_Register reg, byte count, byte *values, byte rxAlign)
{
    if (count == 0) {
        return;
    }

    byte address = (byte)(MFRC522_READ_ADDRESS | reg);
    byte index = 0;
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

/** Устанавливает bits в регистре MFRC522 без изменения остальных bits. */
void MFRC522_RAM_FUNC PCD_SetRegisterBitMask(MFRC522 *mfrc522, PCD_Register reg, byte mask)
{
    byte tmp = PCD_ReadRegister(mfrc522, reg);
    PCD_WriteRegister(mfrc522, reg, (byte)(tmp | mask));
}

/** Сбрасывает bits в регистре MFRC522 без изменения остальных bits. */
void MFRC522_RAM_FUNC PCD_ClearRegisterBitMask(MFRC522 *mfrc522, PCD_Register reg, byte mask)
{
    byte tmp = PCD_ReadRegister(mfrc522, reg);
    PCD_WriteRegister(mfrc522, reg, (byte)(tmp & (~mask)));
}

/**
 * Считает CRC_A аппаратным CRC coprocessor внутри MFRC522.
 * FIFO очищается перед расчетом, чтобы старые байты не попали в CRC.
 */
StatusCode MFRC522_RAM_FUNC PCD_CalculateCRC(MFRC522 *mfrc522, byte *data, byte length, byte *result)
{
    const uint32_t startMs = HAL_Millis();

    PCD_WriteRegister(mfrc522, CommandReg, PCD_Idle);
    PCD_WriteRegister(mfrc522, DivIrqReg, MFRC522_IRQ_CRC);
    PCD_WriteRegister(mfrc522, FIFOLevelReg, MFRC522_FLUSH_FIFO);
    PCD_WriteRegisterMany(mfrc522, FIFODataReg, length, data);
    PCD_WriteRegister(mfrc522, CommandReg, PCD_CalcCRC);

    do {
        byte n = PCD_ReadRegister(mfrc522, DivIrqReg);
        if (n & MFRC522_IRQ_CRC) {
            PCD_WriteRegister(mfrc522, CommandReg, PCD_Idle);
            result[0] = PCD_ReadRegister(mfrc522, CRCResultRegL);
            result[1] = PCD_ReadRegister(mfrc522, CRCResultRegH);
            return STATUS_OK;
        }
    } while ((uint32_t)(HAL_Millis() - startMs) < MFRC522_CRC_TIMEOUT_MS);

    return STATUS_TIMEOUT;
}

/**
 * Полная инициализация MFRC522 после готового SPI.
 * Текущая схема всегда имеет RST pin, поэтому используем hard reset без soft-reset fallback.
 */
void PCD_Init(MFRC522 *mfrc522)
{
    HAL_GPIO_WritePin(mfrc522->_chipSelectPort, mfrc522->_chipSelectPin, GPIO_PIN_HIGH);

    HAL_GPIO_WritePin(mfrc522->_resetPowerDownPort, mfrc522->_resetPowerDownPin, GPIO_PIN_LOW);
    HAL_DelayUs(MFRC522_RESET_PULSE_US);
    HAL_GPIO_WritePin(mfrc522->_resetPowerDownPort, mfrc522->_resetPowerDownPin, GPIO_PIN_HIGH);
    HAL_DelayMs(MFRC522_RESET_SETTLE_MS);

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

/**
 * Soft reset MFRC522 через CommandReg.
 * Не используется в штатном init, но оставлен как явная recovery/API операция.
 */
void PCD_Reset(MFRC522 *mfrc522)
{
    uint8_t count = 0;

    PCD_WriteRegister(mfrc522, CommandReg, PCD_SoftReset);
    do {
        HAL_DelayMs(MFRC522_RESET_SETTLE_MS);
    } while ((PCD_ReadRegister(mfrc522, CommandReg) & (1 << 4)) && (++count) < MFRC522_SOFT_RESET_POLLS);
}

/** Включает RF antenna drivers TX1/TX2, без них карта не получает поле. */
void PCD_AntennaOn(MFRC522 *mfrc522)
{
    byte value = PCD_ReadRegister(mfrc522, TxControlReg);
    if ((value & MFRC522_ANTENNA_ON_MASK) != MFRC522_ANTENNA_ON_MASK) {
        PCD_WriteRegister(mfrc522, TxControlReg, (byte)(value | MFRC522_ANTENNA_ON_MASK));
    }
}

/** Выключает RF antenna drivers TX1/TX2. */
void PCD_AntennaOff(MFRC522 *mfrc522)
{
    PCD_ClearRegisterBitMask(mfrc522, TxControlReg, MFRC522_ANTENNA_ON_MASK);
}

/** Возвращает текущий receiver gain из RFCfgReg. */
byte PCD_GetAntennaGain(MFRC522 *mfrc522)
{
    return (byte)(PCD_ReadRegister(mfrc522, RFCfgReg) & MFRC522_RX_GAIN_MASK);
}

/** Меняет только gain bits, не трогая остальные поля RFCfgReg. */
void PCD_SetAntennaGain(MFRC522 *mfrc522, byte mask)
{
    if (PCD_GetAntennaGain(mfrc522) != mask) {
        PCD_ClearRegisterBitMask(mfrc522, RFCfgReg, MFRC522_RX_GAIN_MASK);
        PCD_SetRegisterBitMask(mfrc522, RFCfgReg, (byte)(mask & MFRC522_RX_GAIN_MASK));
    }
}

/**
 * Удобная обертка для PCD_Transceive: отправить bytes в PICC и принять ответ.
 * waitIRq=0x30 означает: ждем RxIRq или IdleIRq от MFRC522.
 */
StatusCode PCD_TransceiveData(MFRC522 *mfrc522,
                               byte *sendData,
                               byte sendLen,
                               byte *backData,
                               byte *backLen,
                               byte *validBits,
                               byte rxAlign,
                               bool checkCRC)
{
    byte waitIRq = MFRC522_TRANSCEIVE_WAIT_IRQ;
    return PCD_CommunicateWithPICC(mfrc522, PCD_Transceive, waitIRq, sendData, sendLen, backData, backLen, validBits, rxAlign, checkCRC);
}

/**
 * Главный обмен с картой через MFRC522 FIFO.
 * Пишет команду в FIFO, запускает PCD command, ждет IRQ, затем читает FIFO и проверяет ошибки.
 */
StatusCode MFRC522_RAM_FUNC PCD_CommunicateWithPICC(MFRC522 *mfrc522,
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
    const uint32_t startMs = HAL_Millis();
    bool completed = false;
    byte errorRegValue;
    byte _validBits = 0;

    PCD_WriteRegister(mfrc522, CommandReg, PCD_Idle);
    PCD_WriteRegister(mfrc522, ComIrqReg, MFRC522_IRQ_ALL);
    PCD_WriteRegister(mfrc522, FIFOLevelReg, MFRC522_FLUSH_FIFO);
    /* FIFO содержит RF payload, который MFRC522 отправит в PICC. */
    PCD_WriteRegisterMany(mfrc522, FIFODataReg, sendLen, sendData);
    PCD_WriteRegister(mfrc522, BitFramingReg, bitFraming);
    PCD_WriteRegister(mfrc522, CommandReg, command);
    if (command == PCD_Transceive) {
        PCD_SetRegisterBitMask(mfrc522, BitFramingReg, MFRC522_START_SEND);
    }

    do {
        byte n = PCD_ReadRegister(mfrc522, ComIrqReg);
        if (n & waitIRq) {
            completed = true;
            break;
        }
        /* TimerIrq для REQA обычно значит: карты нет или она уже в HALT. */
        if (n & MFRC522_IRQ_TIMER) {
            return STATUS_TIMEOUT;
        }
    } while ((uint32_t)(HAL_Millis() - startMs) < MFRC522_TRANSCEIVE_TIMEOUT_MS);

    if (!completed) {
        return STATUS_TIMEOUT;
    }

    errorRegValue = PCD_ReadRegister(mfrc522, ErrorReg);
    if (errorRegValue & MFRC522_ERROR_PROTOCOL_MASK) {
        return STATUS_ERROR;
    }

    if (backData && backLen) {
        byte n = PCD_ReadRegister(mfrc522, FIFOLevelReg);
        if (n > *backLen) {
            return STATUS_NO_ROOM;
        }
        *backLen = n;
        /* Ответ PICC лежит в FIFO MFRC522 после успешного Transceive. */
        PCD_ReadRegisterMany(mfrc522, FIFODataReg, n, backData, rxAlign);
        _validBits = (byte)(PCD_ReadRegister(mfrc522, ControlReg) & MFRC522_RX_LAST_BITS_MASK);
        if (validBits) {
            *validBits = _validBits;
        }
    }

    if (errorRegValue & MFRC522_ERROR_COLLISION) {
        return STATUS_COLLISION;
    }

    if (backData && backLen && checkCRC) {
        byte controlBuffer[2];
        StatusCode status;

        /* MIFARE NACK - это 4 valid bits в одном байте, не CRC_A frame. */
        if (*backLen == 1 && _validBits == MFRC522_MIFARE_NACK_BITS) {
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

/** Отправляет REQA: 7-bit запрос карт в состоянии IDLE. Ответ - ATQA 2 байта. */
StatusCode PICC_RequestA(MFRC522 *mfrc522, byte *bufferATQA, byte *bufferSize)
{
    return PICC_REQA_or_WUPA(mfrc522, PICC_CMD_REQA, bufferATQA, bufferSize);
}

/** Отправляет WUPA: 7-bit запрос карт в IDLE и HALT. В main loop сейчас не используется. */
StatusCode PICC_WakeupA(MFRC522 *mfrc522, byte *bufferATQA, byte *bufferSize)
{
    return PICC_REQA_or_WUPA(mfrc522, PICC_CMD_WUPA, bufferATQA, bufferSize);
}

/**
 * Общая отправка REQA/WUPA как короткого 7-bit frame.
 * ATQA обязан быть ровно 16 bits, поэтому проверяем размер и отсутствие partial bits.
 */
StatusCode PICC_REQA_or_WUPA(MFRC522 *mfrc522, byte command, byte *bufferATQA, byte *bufferSize)
{
    byte validBits;
    StatusCode status;

    if (bufferATQA == NULL || *bufferSize < MFRC522_ATQA_SIZE) {
        return STATUS_NO_ROOM;
    }
    PCD_ClearRegisterBitMask(mfrc522, CollReg, MFRC522_VALUES_AFTER_COLL);
    validBits = MFRC522_REQA_BITS;
    status = PCD_TransceiveData(mfrc522, &command, 1, bufferATQA, bufferSize, &validBits, 0, false);
    if (status != STATUS_OK) {
        return status;
    }
    if (*bufferSize != MFRC522_ATQA_SIZE || validBits != 0) {
        return STATUS_ERROR;
    }
    return STATUS_OK;
}

/**
 * Выполняет ISO14443-A anticollision/select и заполняет uid.
 * Отправляет ANTICOLLISION кадры для UID bytes и SELECT кадры для получения SAK.
 */
StatusCode PICC_Select(MFRC522 *mfrc522, Uid *uid, byte validBits)
{
    // TODO: Переписать без переменных в начале
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

    if (validBits > MFRC522_UID_MAX_BITS) {
        return STATUS_INVALID;
    }

    PCD_ClearRegisterBitMask(mfrc522, CollReg, MFRC522_VALUES_AFTER_COLL);

    uidComplete = false;
    while (!uidComplete) {
        /* Каждый cascade level передает максимум 4 UID-related байта. */
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
            if (currentLevelKnownBits >= MFRC522_CASCADE_LEVEL_BITS) {
                /* Все 32 бита уровня известны: отправляем SELECT и ждем SAK. */
                buffer[1] = MFRC522_SELECT_NVB;
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
                /* Не все UID bits известны: отправляем ANTICOLLISION запрос. */
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
                /* Без валидной позиции collision нельзя выбрать следующую ветку UID. */
                if (valueOfCollReg & MFRC522_COLL_POS_INVALID) {
                    return STATUS_COLLISION;
                }
                collisionPos = (byte)(valueOfCollReg & MFRC522_COLL_POS_MASK);
                if (collisionPos == 0) {
                    collisionPos = MFRC522_CASCADE_LEVEL_BITS;
                }
                if (collisionPos <= currentLevelKnownBits) {
                    return STATUS_INTERNAL_ERROR;
                }
                currentLevelKnownBits = (int8_t)collisionPos;
                count = (byte)(currentLevelKnownBits % 8);
                checkBit = (byte)((currentLevelKnownBits - 1) % 8);
                index = (byte)(1 + (currentLevelKnownBits / 8) + (count ? 1 : 0));
                /* Простая политика anticollision: выбираем ветку, где collided bit равен 1. */
                buffer[index] |= (byte)(1 << checkBit);
            } else if (result != STATUS_OK) {
                return result;
            } else {
                if (currentLevelKnownBits >= MFRC522_CASCADE_LEVEL_BITS) {
                    selectDone = true;
                } else {
                    currentLevelKnownBits = MFRC522_CASCADE_LEVEL_BITS;
                }
            }
        }

        index = (buffer[2] == PICC_CMD_CT) ? 3 : 2;
        bytesToCopy = (buffer[2] == PICC_CMD_CT) ? 3 : 4;
        for (count = 0; count < bytesToCopy; count++) {
            uid->uidByte[uidIndex + count] = buffer[index++];
        }

        if (responseLength != MFRC522_SAK_SIZE || txLastBits != 0) {
            return STATUS_ERROR;
        }
        result = PCD_CalculateCRC(mfrc522, responseBuffer, 1, &buffer[2]);
        if (result != STATUS_OK) {
            return result;
        }
        if ((buffer[2] != responseBuffer[1]) || (buffer[3] != responseBuffer[2])) {
            return STATUS_CRC_WRONG;
        }
        if (responseBuffer[0] & MFRC522_SAK_CASCADE_BIT) {
            cascadeLevel++;
        } else {
            uidComplete = true;
            uid->sak = responseBuffer[0];
        }
    }

    uid->size = (byte)(3 * cascadeLevel + 1);
    return STATUS_OK;
}

/**
 * Отправляет HALTA: 0x50 0x00 + CRC_A.
 * Правильная карта после HALTA молчит, поэтому STATUS_TIMEOUT здесь означает успех.
 */
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

/** Сбрасывает Crypto1 active bit после MIFARE authentication. Для UID-only пути не нужен. */
void PCD_StopCrypto1(MFRC522 *mfrc522)
{
    PCD_ClearRegisterBitMask(mfrc522, Status2Reg, MFRC522_CRYPTO1_ON);
}

/**
 * Быстрая проверка карты через REQA.
 * Collision тоже считается признаком карты: значит, RF-ответ был, но несколько ответов наложились.
 */
bool PICC_IsNewCardPresent(MFRC522 *mfrc522)
{
    byte bufferATQA[MFRC522_ATQA_SIZE];
    byte bufferSize = sizeof(bufferATQA);
    StatusCode result;

    PCD_WriteRegister(mfrc522, TxModeReg, 0x00);
    PCD_WriteRegister(mfrc522, RxModeReg, 0x00);
    PCD_WriteRegister(mfrc522, ModWidthReg, 0x26);

    result = PICC_RequestA(mfrc522, bufferATQA, &bufferSize);
    return (result == STATUS_OK || result == STATUS_COLLISION);
}

/** Читает UID через anticollision/select. UID валиден только при STATUS_OK. */
bool PICC_ReadCardSerial(MFRC522 *mfrc522)
{
    StatusCode result = PICC_Select(mfrc522, &mfrc522->uid, 0);
    return (result == STATUS_OK);
}

/** Текстовое имя статуса для временной диагностики через UART. */
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
