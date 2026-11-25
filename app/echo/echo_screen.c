/* ECHO LUDWIG - the whole screen is black; one small white message
 * (icon + two short lines) sits in the centre, for sighted helpers.
 * Rendered in horizontal bands so it needs no full frame buffer. */
#include "echo_screen.h"
#include "echo_game.h"
#include "echo_data.h"
#include "echo_fx.h"
#include <string.h>

enum { W = 240, H = 240, ICON_Y = 90, LINE1_Y = 122, LINE2_Y = 140, CELL = 8 };
#define WHITE 0xffffu
#define GREY  0x9cf3u

static unsigned text_width(const char *s) { unsigned n = (unsigned)strlen(s); return n ? n * CELL - 1 : 0; }

static void draw_text_row(uint16_t *row, const char *s, int glyph_row, uint16_t colour)
{
    unsigned width = text_width(s), x0 = width < W ? (W - width) / 2 : 0;
    for (unsigned i = 0; s[i]; ++i) {
        unsigned c = (unsigned char)s[i];
        if (c < 32 || c > 95) c = '?';
        uint8_t bits = echo_font[c - 32][glyph_row];
        for (unsigned x = 0; x < 7; ++x) {
            unsigned px = x0 + i * CELL + x;
            if (px < W && (bits & (0x40u >> x))) row[px] = colour;
        }
    }
}

void echo_screen_band(unsigned y0, unsigned rows, uint16_t *pixels)
{
    unsigned icon, serial; const char *a, *b;
    echo_game_message(&icon, &a, &b, &serial);
    if (fx_flash()) { /* the whole screen white for one short flash */
        for (size_t i = 0; i < (size_t)W * rows; ++i) pixels[i] = WHITE;
        return;
    }
    memset(pixels, 0, (size_t)W * rows * sizeof *pixels);
    for (unsigned r = 0; r < rows; ++r) {
        int y = (int)(y0 + r);
        uint16_t *row = pixels + (size_t)r * W;
        if (icon != IC_NONE && icon < IC_COUNT && y >= ICON_Y && y < ICON_Y + 22) {
            uint16_t bits = echo_icons[icon][(y - ICON_Y) / 2];
            for (unsigned x = 0; x < 22; ++x)
                if (bits & (0x400u >> (x / 2))) row[W / 2 - 11 + x] = WHITE;
        }
        if (y >= LINE1_Y && y < LINE1_Y + 11) draw_text_row(row, a, y - LINE1_Y, WHITE);
        if (y >= LINE2_Y && y < LINE2_Y + 11) draw_text_row(row, b, y - LINE2_Y, GREY);
    }
}

unsigned echo_screen_serial(void)
{
    unsigned icon, serial; const char *a, *b;
    echo_game_message(&icon, &a, &b, &serial);
    return serial * 2u + (unsigned)fx_flash();
}
