#ifndef LCD_DIRVER_H_
#define LCD_DRIVER_H_

void lcd_init(void);   // initialize lcd
void lcd_send_cmd(char cmd);  // send command to the lcd
void lcd_send_data(char data);  // send data to the lcd
void lcd_send_string(const char *str, int row, int col);  // send string to the lcd
void lcd_put_cur(int row, int col);  // put cursor at the entered position row (0 or 1), col (0-15);
void lcd_clear(void);
void lcd_send_int(int value, int row, int col);
void lcd_send_double(double value, int row, int col);

#endif /* LCD_DRIVER_H_ */
