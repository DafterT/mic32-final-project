#ifndef MFRC522_H
#define MFRC522_H

#include "mik32_hal_gpio.h"
#include "mik32_hal_spi.h"

#include <stdbool.h>
#include <stdint.h>

typedef uint8_t byte;

#ifndef MFRC522_SPICLOCK
#define MFRC522_SPICLOCK (4000000u)
#endif

#define MFRC522_FIFO_SIZE 64u
#define MFRC522_UNUSED_PIN UINT8_MAX

typedef enum {
    CommandReg              = 0x01 << 1,
    ComIEnReg               = 0x02 << 1,
    DivIEnReg               = 0x03 << 1,
    ComIrqReg               = 0x04 << 1,
    DivIrqReg               = 0x05 << 1,
    ErrorReg                = 0x06 << 1,
    Status1Reg              = 0x07 << 1,
    Status2Reg              = 0x08 << 1,
    FIFODataReg             = 0x09 << 1,
    FIFOLevelReg            = 0x0A << 1,
    WaterLevelReg           = 0x0B << 1,
    ControlReg              = 0x0C << 1,
    BitFramingReg           = 0x0D << 1,
    CollReg                 = 0x0E << 1,

    ModeReg                 = 0x11 << 1,
    TxModeReg               = 0x12 << 1,
    RxModeReg               = 0x13 << 1,
    TxControlReg            = 0x14 << 1,
    TxASKReg                = 0x15 << 1,
    TxSelReg                = 0x16 << 1,
    RxSelReg                = 0x17 << 1,
    RxThresholdReg          = 0x18 << 1,
    DemodReg                = 0x19 << 1,
    MfTxReg                 = 0x1C << 1,
    MfRxReg                 = 0x1D << 1,
    SerialSpeedReg          = 0x1F << 1,

    CRCResultRegH           = 0x21 << 1,
    CRCResultRegL           = 0x22 << 1,
    ModWidthReg             = 0x24 << 1,
    RFCfgReg                = 0x26 << 1,
    GsNReg                  = 0x27 << 1,
    CWGsPReg                = 0x28 << 1,
    ModGsPReg               = 0x29 << 1,
    TModeReg                = 0x2A << 1,
    TPrescalerReg           = 0x2B << 1,
    TReloadRegH             = 0x2C << 1,
    TReloadRegL             = 0x2D << 1,
    TCounterValueRegH       = 0x2E << 1,
    TCounterValueRegL       = 0x2F << 1,

    TestSel1Reg             = 0x31 << 1,
    TestSel2Reg             = 0x32 << 1,
    TestPinEnReg            = 0x33 << 1,
    TestPinValueReg         = 0x34 << 1,
    TestBusReg              = 0x35 << 1,
    AutoTestReg             = 0x36 << 1,
    VersionReg              = 0x37 << 1,
    AnalogTestReg           = 0x38 << 1,
    TestDAC1Reg             = 0x39 << 1,
    TestDAC2Reg             = 0x3A << 1,
    TestADCReg              = 0x3B << 1
} PCD_Register;

typedef enum {
    PCD_Idle                = 0x00,
    PCD_Mem                 = 0x01,
    PCD_GenerateRandomID    = 0x02,
    PCD_CalcCRC             = 0x03,
    PCD_Transmit            = 0x04,
    PCD_NoCmdChange         = 0x07,
    PCD_Receive             = 0x08,
    PCD_Transceive          = 0x0C,
    PCD_MFAuthent           = 0x0E,
    PCD_SoftReset           = 0x0F
} PCD_Command;

typedef enum {
    PICC_CMD_REQA           = 0x26,
    PICC_CMD_WUPA           = 0x52,
    PICC_CMD_CT             = 0x88,
    PICC_CMD_SEL_CL1        = 0x93,
    PICC_CMD_SEL_CL2        = 0x95,
    PICC_CMD_SEL_CL3        = 0x97,
    PICC_CMD_HLTA           = 0x50,
    PICC_CMD_RATS           = 0xE0,
    PICC_CMD_MF_AUTH_KEY_A  = 0x60,
    PICC_CMD_MF_AUTH_KEY_B  = 0x61,
    PICC_CMD_MF_READ        = 0x30,
    PICC_CMD_MF_WRITE       = 0xA0,
    PICC_CMD_MF_DECREMENT   = 0xC0,
    PICC_CMD_MF_INCREMENT   = 0xC1,
    PICC_CMD_MF_RESTORE     = 0xC2,
    PICC_CMD_MF_TRANSFER    = 0xB0,
    PICC_CMD_UL_WRITE       = 0xA2
} PICC_Command;

typedef enum {
    STATUS_OK,
    STATUS_ERROR,
    STATUS_COLLISION,
    STATUS_TIMEOUT,
    STATUS_NO_ROOM,
    STATUS_INTERNAL_ERROR,
    STATUS_INVALID,
    STATUS_CRC_WRONG,
    STATUS_MIFARE_NACK = 0xff
} StatusCode;

typedef struct {
    byte size;
    byte uidByte[10];
    byte sak;
} Uid;

typedef struct {
    Uid uid;
    SPI_HandleTypeDef *spi;
    GPIO_TypeDef *_chipSelectPort;
    HAL_PinsTypeDef _chipSelectPin;
    GPIO_TypeDef *_resetPowerDownPort;
    HAL_PinsTypeDef _resetPowerDownPin;
} MFRC522;

void MFRC522_Init(MFRC522 *mfrc522,
                  SPI_HandleTypeDef *spi,
                  GPIO_TypeDef *chipSelectPort,
                  HAL_PinsTypeDef chipSelectPin,
                  GPIO_TypeDef *resetPowerDownPort,
                  HAL_PinsTypeDef resetPowerDownPin);

void PCD_WriteRegister(MFRC522 *mfrc522, PCD_Register reg, byte value);
void PCD_WriteRegisterMany(MFRC522 *mfrc522, PCD_Register reg, byte count, byte *values);
byte PCD_ReadRegister(MFRC522 *mfrc522, PCD_Register reg);
void PCD_ReadRegisterMany(MFRC522 *mfrc522, PCD_Register reg, byte count, byte *values, byte rxAlign);
void PCD_SetRegisterBitMask(MFRC522 *mfrc522, PCD_Register reg, byte mask);
void PCD_ClearRegisterBitMask(MFRC522 *mfrc522, PCD_Register reg, byte mask);
StatusCode PCD_CalculateCRC(MFRC522 *mfrc522, byte *data, byte length, byte *result);

void PCD_Init(MFRC522 *mfrc522);
void PCD_Reset(MFRC522 *mfrc522);
void PCD_AntennaOn(MFRC522 *mfrc522);
void PCD_AntennaOff(MFRC522 *mfrc522);
byte PCD_GetAntennaGain(MFRC522 *mfrc522);
void PCD_SetAntennaGain(MFRC522 *mfrc522, byte mask);

StatusCode PCD_TransceiveData(MFRC522 *mfrc522,
                               byte *sendData,
                               byte sendLen,
                               byte *backData,
                               byte *backLen,
                               byte *validBits,
                               byte rxAlign,
                               bool checkCRC);
StatusCode PCD_CommunicateWithPICC(MFRC522 *mfrc522,
                                    byte command,
                                    byte waitIRq,
                                    byte *sendData,
                                    byte sendLen,
                                    byte *backData,
                                    byte *backLen,
                                    byte *validBits,
                                    byte rxAlign,
                                    bool checkCRC);
StatusCode PICC_RequestA(MFRC522 *mfrc522, byte *bufferATQA, byte *bufferSize);
StatusCode PICC_WakeupA(MFRC522 *mfrc522, byte *bufferATQA, byte *bufferSize);
StatusCode PICC_REQA_or_WUPA(MFRC522 *mfrc522, byte command, byte *bufferATQA, byte *bufferSize);
StatusCode PICC_Select(MFRC522 *mfrc522, Uid *uid, byte validBits);
StatusCode PICC_HaltA(MFRC522 *mfrc522);
void PCD_StopCrypto1(MFRC522 *mfrc522);
bool PICC_IsNewCardPresent(MFRC522 *mfrc522);
bool PICC_ReadCardSerial(MFRC522 *mfrc522);

const char *GetStatusCodeName(StatusCode code);

#endif
