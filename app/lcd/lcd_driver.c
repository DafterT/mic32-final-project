
/** Put this in the src folder **/
#include "mik32_hal.h"
#include "lcd_driver.h"
#include "mik32_hal_i2c.h"
#include "xprintf.h"
#include "stdlib.h"
#include "math.h"

extern I2C_HandleTypeDef hi2c;  // change your handler here accordingly

#define SLAVE_ADDRESS_LCD 0x27 // change this according to ur setup
#define RADIX_DEC 10
#define ROUND_MULTIP 100
#define LCD_CGRAM_ADDRESS 0x40
#define LCD_DATA_BATCH_MAX_CHARS 16u

static void lcd_send_raw_cmd_nibble(uint8_t nibble)
{
	uint8_t data_t[2];
	uint8_t data_u = (uint8_t)(nibble & 0xf0u);

	data_t[0] = data_u | 0x0Cu;
	data_t[1] = data_u | 0x08u;

	if(HAL_OK != HAL_I2C_Master_Transmit (&hi2c, SLAVE_ADDRESS_LCD, data_t, 2, I2C_TIMEOUT_DEFAULT)) {
		xprintf("No Cmd sent\r\n");
	}
}

static void send_raw_data(const char* str)
{
	while (*str) lcd_send_data (*str++);
}

void lcd_send_cmd (uint8_t cmd)
{
	uint8_t data_u, data_l;
	uint8_t data_t[4];
	data_u = (uint8_t)(cmd & 0xf0u);
	data_l = (uint8_t)((cmd << 4u) & 0xf0u);
	data_t[0] = data_u|0x0C;  //en=1, rs=0 -> bxxxx1100
	data_t[1] = data_u|0x08;  //en=0, rs=0 -> bxxxx1000
	data_t[2] = data_l|0x0C;  //en=1, rs=0 -> bxxxx1100
	data_t[3] = data_l|0x08;  //en=0, rs=0 -> bxxxx1000

	if(HAL_OK != HAL_I2C_Master_Transmit (&hi2c, SLAVE_ADDRESS_LCD, data_t, 4, I2C_TIMEOUT_DEFAULT)) {
		xprintf("No Cmd sent\r\n");
	}
}

void lcd_send_data (uint8_t data)
{
	uint8_t data_u, data_l;
	uint8_t data_t[4];
	data_u = (uint8_t)(data & 0xf0u);
	data_l = (uint8_t)((data << 4u) & 0xf0u);
	data_t[0] = data_u|0x0D;  //en=1, rs=0 -> bxxxx1101
	data_t[1] = data_u|0x09;  //en=0, rs=0 -> bxxxx1001
	data_t[2] = data_l|0x0D;  //en=1, rs=0 -> bxxxx1101
	data_t[3] = data_l|0x09;  //en=0, rs=0 -> bxxxx1001
	
	if(HAL_OK != HAL_I2C_Master_Transmit (&hi2c, SLAVE_ADDRESS_LCD, data_t, 4, I2C_TIMEOUT_DEFAULT)) {
		xprintf("No Data sent\r\n");
	}
}

void lcd_send_data_buffer(const char *data, uint8_t length)
{
	uint8_t offset = 0u;

	if ((data == NULL) || (length == 0u)) {
		return;
	}

	while (offset < length) {
		uint8_t chunk = (uint8_t)(length - offset);
		uint8_t data_t[LCD_DATA_BATCH_MAX_CHARS * 4u];
		uint8_t index;

		if (chunk > LCD_DATA_BATCH_MAX_CHARS) {
			chunk = LCD_DATA_BATCH_MAX_CHARS;
		}

		for (index = 0u; index < chunk; ++index) {
			uint8_t value = (uint8_t)data[offset + index];
			uint8_t data_u = (uint8_t)(value & 0xf0u);
			uint8_t data_l = (uint8_t)((value << 4u) & 0xf0u);
			uint8_t out = (uint8_t)(index * 4u);

			data_t[out] = data_u | 0x0Du;
			data_t[out + 1u] = data_u | 0x09u;
			data_t[out + 2u] = data_l | 0x0Du;
			data_t[out + 3u] = data_l | 0x09u;
		}

		if(HAL_OK != HAL_I2C_Master_Transmit (&hi2c, SLAVE_ADDRESS_LCD, data_t, (uint16_t)(chunk * 4u), I2C_TIMEOUT_DEFAULT)) {
			xprintf("No Data sent\r\n");
		}

		offset = (uint8_t)(offset + chunk);
	}
}

void lcd_clear (void)
{
	lcd_send_cmd (0x80);
	for (int i=0; i<70; i++)
	{
		lcd_send_data (' ');
	}
}

void lcd_put_cur(int row, int col)
{
    switch (row)
    {
        case 0:
            col |= 0x80;
            break;
        case 1:
            col |= 0xC0;
            break;
    }

    lcd_send_cmd (col);
}


void lcd_init (void)
{
	// 4 bit initialisation
	HAL_DelayMs(50);  // wait for >40ms
	lcd_send_raw_cmd_nibble(0x30u);
	HAL_DelayMs(5);  // wait for >4.1ms
	lcd_send_raw_cmd_nibble(0x30u);
	HAL_DelayMs(1);  // wait for >100us
	lcd_send_raw_cmd_nibble(0x30u);
	HAL_DelayMs(10);
	lcd_send_raw_cmd_nibble(0x20u);  // 4bit mode
	HAL_DelayMs(10);

  // dislay initialisation
	lcd_send_cmd (0x28); // Function set --> DL=0 (4 bit mode), N = 1 (2 line display) F = 0 (5x8 characters)
	HAL_DelayMs(1);
	lcd_send_cmd (0x08); //Display on/off control --> D=0,C=0, B=0  ---> display off
	HAL_DelayMs(1);
	lcd_send_cmd (0x01);  // clear display
	HAL_DelayMs(1);
	HAL_DelayMs(1);
	lcd_send_cmd (0x06); //Entry mode set --> I/D = 1 (increment cursor) & S = 0 (no shift)
	HAL_DelayMs(1);
	lcd_send_cmd (0x0C); //Display on/off control --> D = 1, C and B = 0. (Cursor and blink, last two bits)
}

void lcd_send_string (const char *str, int row, int col)
{
	lcd_put_cur(row, col);
	send_raw_data(str);
}

void lcd_send_int(int value, int row, int col) 
{
	char buffer[16] = { 0 };

	lcd_put_cur(row, col);
	itoa(value, buffer, RADIX_DEC);
	send_raw_data(buffer);
}

void lcd_send_double(double value, int row, int col)
{
	char buffer[16] = { 0 };
	int integer_part = 0;
	int fractional_part = 0;

	integer_part = (int) value;

	if(value < 0) {
		fractional_part = -value * ROUND_MULTIP - integer_part * ROUND_MULTIP;
	} else {
		fractional_part = value * ROUND_MULTIP - integer_part * ROUND_MULTIP;
	}
	
	lcd_put_cur(row, col);
	itoa(integer_part, buffer, RADIX_DEC);
	send_raw_data(buffer);
	lcd_send_data('.');
	itoa(fractional_part, buffer, RADIX_DEC);
    send_raw_data(buffer);
}

void lcd_create_char(uint8_t slot, const uint8_t bitmap[LCD_CUSTOM_CHAR_ROWS])
{
	if (bitmap == NULL) {
		return;
	}

	slot %= LCD_CUSTOM_CHAR_COUNT;
	lcd_send_cmd(LCD_CGRAM_ADDRESS | (slot << 3));

	for (int i = 0; i < LCD_CUSTOM_CHAR_ROWS; i++) {
		lcd_send_data(bitmap[i] & 0x1F);
	}
}

void lcd_send_custom_char(uint8_t slot, int row, int col)
{
	lcd_put_cur(row, col);
	lcd_send_data(slot % LCD_CUSTOM_CHAR_COUNT);
}
