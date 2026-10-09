#include "lcd.h"
#include <string.h>
static uint32_t color(const Lcd *l, uint16_t value)
{
    unsigned r, g, b;
    if (!l->enabled || l->standby || l->pattern == 2) return 0;
    if (l->pattern == 3) return 0xffffff;
    if (l->pattern == 1) value ^= 0xffff;
    r = (value >> 11) & 31; g = (value >> 5) & 63; b = value & 31;
    return ((r * 255 / 31) << 16) | ((g * 255 / 63) << 8) | (b * 255 / 31);
}
static void repaint(Lcd *l)
{
    unsigned x, y;
    if (!l->pixels) return;
    for (y = 0; y < l->height; ++y) for (x = 0; x < l->width; ++x)
        l->pixels[(size_t)y * l->width + x] = color(l, l->ram[y * 256 + x + l->origin_y]);
}
static void advance(Lcd *l)
{
    if (l->entry & 2) {
        if (++l->x > l->x_end) { l->x = l->x_start; if (++l->y > l->y_end) l->y = l->y_start; }
    } else {
        if (++l->y > l->y_end) { l->y = l->y_start; if (++l->x > l->x_end) l->x = l->x_start; }
    }
}
static unsigned parameters(unsigned command)
{
    switch (command) {
    case 0x42: case 0x43: case 0xfc: return 2;
    case 0x59: return 4;
    case 0x02: case 0x10: case 0x20: case 0x22: case 0x24: case 0x26:
    case 0x28: case 0x2a: case 0x2b: case 0x30: case 0x32: case 0x34:
    case 0x36: case 0x40: case 0x45: case 0x53: case 0x55: case 0x56:
    case 0x57: case 0x5a: case 0xed: return 1;
    default: return 0;
    }
}
static void parameter_done(Lcd *l)
{
    switch (l->command) {
    case 0x42: l->x_start = l->x = l->args[0]; l->x_end = l->args[1]; break;
    case 0x43: l->y_start = l->y = l->args[0]; l->y_end = l->args[1]; break;
    case 0x40: l->entry = l->args[0]; break;
    case 0x53: l->pattern = l->args[0] & 3; repaint(l); break;
    default: break; /* Analog/power parameters retained but not simulated. */
    }
}
static void command_write(void *ctx, uint32_t offset, unsigned width, uint32_t value)
{
    Lcd *l = ctx;
    (void)offset;
    if (width != 2) { bus_fault_access(l->bus, 1, l->command_port, width, value, "LCD requires x16 bus"); return; }
    value &= 255;
    if (l->remaining) {
        l->args[l->parameter++] = value;
        if (!--l->remaining) parameter_done(l);
        return;
    }
    l->command = value; ++l->commands; l->parameter = 0; l->remaining = parameters(value);
    switch (value) {
    case 0x2c: l->standby = 0; repaint(l); break;
    case 0x2d: l->standby = 1; repaint(l); break;
    case 0x2e: break; /* Normal x16 interface; burst mode is not used by C100. */
    case 0x50: l->enabled = 0; repaint(l); break;
    case 0x51: if (!l->standby) l->enabled = 1; repaint(l); break;
    default: if (!l->remaining && value != 0) bus_fault_access(l->bus, 1, l->command_port, width, value, "unimplemented LCD command"); break;
    }
}
static uint32_t status_read(void *ctx, uint32_t offset, unsigned width)
{
    Lcd *l = ctx;
    (void)offset; (void)width;
    return (l->enabled ? 1u : 0) | (l->standby ? 4u : 0) |
        (l->pattern == 1 ? 2u : 0) | ((l->entry & 2) ? 0x40u : 0); /* No busy delay. */
}
static void data_write(void *ctx, uint32_t offset, unsigned width, uint32_t value)
{
    Lcd *l = ctx;
    (void)offset;
    if (width != 2) { bus_fault_access(l->bus, 1, l->data_port, width, value, "LCD requires x16 pixels"); return; }
    if (l->x < 256 && l->y < 256) {
        uint16_t raw = (uint16_t)value;
        if (l->entry & 4) raw ^= 0xffff;
        l->ram[l->x * 256 + l->y] = raw;
        if (l->pixels && l->x < l->height && l->y >= l->origin_y && l->y - l->origin_y < l->width)
            l->pixels[(size_t)l->x * l->width + l->y - l->origin_y] = color(l, raw);
    }
    if (!l->writes) l->first_write_tick = l->bus->ticks;
    ++l->writes; advance(l);
}
static uint32_t data_read(void *ctx, uint32_t offset, unsigned width)
{
    Lcd *l = ctx;
    uint32_t result = 0;
    (void)offset; (void)width;
    if (l->x < 256 && l->y < 256) result = l->ram[l->x * 256 + l->y];
    if (!(l->entry & 1)) advance(l);
    return result;
}
int lcd_attach(Lcd *l, Bus *bus, uint32_t command_port, uint32_t data_port, unsigned origin_y)
{
    if (origin_y > 255 || (command_port & 1) || (data_port & 1)) return 0;
    memset(l, 0, sizeof(*l)); l->bus = bus; l->command_port = command_port; l->data_port = data_port;
    l->origin_y = origin_y; l->standby = 1; l->x_end = l->y_end = 255;
    if (!bus_device(bus, command_port, 2, status_read, command_write, l) ||
        !bus_device(bus, data_port, 2, data_read, data_write, l)) return 0;
    l->attached = 1; return 1;
}
void lcd_connect(Lcd *l, uint32_t *pixels, unsigned width, unsigned height)
{
    l->pixels = pixels; l->width = width; l->height = height; repaint(l);
}
static void put32(uint8_t *p, uint32_t v)
{
    unsigned i;
    for (i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8 * i));
}
int lcd_save_bmp(const Lcd *l, const char *path)
{
    uint8_t header[54] = {0}, row[256 * 3 + 4] = {0};
    unsigned stride = (l->width * 3 + 3) & ~3u, x, y;
    FILE *f;
    int ok = 1;
    if (!l->pixels || !l->width || l->width > 256 || !l->height || l->height > 256) return 0;
    f = fopen(path, "wb"); if (!f) return 0;
    header[0] = 'B'; header[1] = 'M'; put32(header + 2, 54 + stride * l->height);
    put32(header + 10, 54); put32(header + 14, 40);
    put32(header + 18, l->width); put32(header + 22, l->height);
    header[26] = 1; header[28] = 24; put32(header + 34, stride * l->height);
    if (fwrite(header, 1, sizeof(header), f) != sizeof(header)) ok = 0;
    for (y = l->height; ok && y; --y) {
        for (x = 0; x < l->width; ++x) {
            uint32_t v = l->pixels[(size_t)(y - 1) * l->width + x];
            row[x * 3] = (uint8_t)v; row[x * 3 + 1] = (uint8_t)(v >> 8); row[x * 3 + 2] = (uint8_t)(v >> 16);
        }
        if (fwrite(row, 1, stride, f) != stride) ok = 0;
    }
    if (fclose(f)) ok = 0;
    return ok;
}
