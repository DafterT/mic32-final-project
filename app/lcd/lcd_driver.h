#ifndef LCD_DRIVER_H_
#define LCD_DRIVER_H_

#include <stdint.h>

#define LCD_CUSTOM_CHAR_COUNT 8u
#define LCD_CUSTOM_CHAR_ROWS  8u

void lcd_init(void);   // initialize lcd
void lcd_send_cmd(char cmd);  // send command to the lcd
void lcd_send_data(char data);  // send data to the lcd
void lcd_send_data_buffer(const char *data, uint8_t length);
void lcd_send_string(const char *str, int row, int col);  // send string to the lcd
void lcd_put_cur(int row, int col);  // put cursor at the entered position row (0 or 1), col (0-15);
void lcd_clear(void);
void lcd_send_int(int value, int row, int col);
void lcd_send_double(double value, int row, int col);
// bitmap has 8 rows; only lower 5 bits of each row are used.
void lcd_create_char(uint8_t slot, const uint8_t bitmap[LCD_CUSTOM_CHAR_ROWS]);
void lcd_send_custom_char(uint8_t slot, int row, int col);

#endif /* LCD_DRIVER_H_ */
