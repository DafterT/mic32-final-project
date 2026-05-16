
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

static void send_raw_data(const char* str)
{
	while (*str) lcd_send_data (*str++);
}

void lcd_send_cmd (char cmd)
{
  char data_u, data_l;
	uint8_t data_t[4];
	data_u = (cmd&0xf0);
	data_l = ((cmd<<4)&0xf0);
	data_t[0] = data_u|0x0C;  //en=1, rs=0 -> bxxxx1100
	data_t[1] = data_u|0x08;  //en=0, rs=0 -> bxxxx1000
	data_t[2] = data_l|0x0C;  //en=1, rs=0 -> bxxxx1100
	data_t[3] = data_l|0x08;  //en=0, rs=0 -> bxxxx1000

	if(HAL_OK != HAL_I2C_Master_Transmit (&hi2c, SLAVE_ADDRESS_LCD, data_t, 4, I2C_TIMEOUT_DEFAULT)) {
		xprintf("No Cmd sent\r\n");
	}
}

void lcd_send_data (char data)
{
	char data_u, data_l;
	uint8_t data_t[4];
	data_u = (data&0xf0);
	data_l = ((data<<4)&0xf0);
	data_t[0] = data_u|0x0D;  //en=1, rs=0 -> bxxxx1101
	data_t[1] = data_u|0x09;  //en=0, rs=0 -> bxxxx1001
	data_t[2] = data_l|0x0D;  //en=1, rs=0 -> bxxxx1101
	data_t[3] = data_l|0x09;  //en=0, rs=0 -> bxxxx1001
	
	if(HAL_OK != HAL_I2C_Master_Transmit (&hi2c, SLAVE_ADDRESS_LCD, data_t, 4, I2C_TIMEOUT_DEFAULT)) {
		xprintf("No Data sent\r\n");
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
	lcd_send_cmd (0x30);
	HAL_DelayMs(5);  // wait for >4.1ms
	lcd_send_cmd (0x30);
	HAL_DelayMs(1);  // wait for >100us
	lcd_send_cmd (0x30);
	HAL_DelayMs(10);
	lcd_send_cmd (0x20);  // 4bit mode
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
