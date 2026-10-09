#ifndef SGH_LCD_H
#define SGH_LCD_H
#include "bus.h"
typedef struct {
    Bus *bus;
    uint32_t command_port, data_port, *pixels;
    uint16_t ram[256 * 256];
    unsigned width, height, origin_y, x, y, x_start, x_end, y_start, y_end;
    unsigned command, remaining, parameter, args[4], entry, pattern;
    int enabled, standby, attached;
    uint64_t commands, writes, first_write_tick;
} Lcd;
int lcd_attach(Lcd *lcd, Bus *bus, uint32_t command_port, uint32_t data_port, unsigned origin_y);
void lcd_connect(Lcd *lcd, uint32_t *pixels, unsigned width, unsigned height);
int lcd_save_bmp(const Lcd *lcd, const char *path);
#endif
