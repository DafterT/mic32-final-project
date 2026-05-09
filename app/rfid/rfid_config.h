#ifndef RFID_CONFIG_H
#define RFID_CONFIG_H

#include "mik32_hal_gpio.h"
#include "mik32_hal_spi.h"

#define RFID_SPI_INSTANCE      SPI_0
#define RFID_SPI_BAUDRATE_DIV  SPI_BAUDRATE_DIV8
#define RFID_SPI_PHASE         SPI_PHASE_OFF
#define RFID_SPI_POLARITY      SPI_POLARITY_LOW

#define RFID_CS_PORT           GPIO_0
#define RFID_CS_PIN            GPIO_PIN_8
#define RFID_RST_PORT          GPIO_0
#define RFID_RST_PIN           GPIO_PIN_10

#define RFID_POLL_DELAY_MS              100u

#endif
