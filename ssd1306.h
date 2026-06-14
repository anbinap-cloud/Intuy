#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "driver/i2c.h"

#define SSD1306_WIDTH   128
#define SSD1306_HEIGHT  64

bool ssd1306_init(i2c_port_t port, uint8_t addr);
void ssd1306_clear(void);
void ssd1306_display(void);
void ssd1306_set_cursor(uint8_t col, uint8_t row);
void ssd1306_puts(const char *str);
void ssd1306_printf(uint8_t col, uint8_t row, const char *fmt, ...);
