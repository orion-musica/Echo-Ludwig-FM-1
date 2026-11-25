/* ECHO LUDWIG - minimal screen: black, one centred white message. */
#pragma once
#include "echo_port.h"
enum { ECHO_SCREEN_W = 240, ECHO_SCREEN_H = 240 };
/* Render rows y0 .. y0+rows-1 as native RGB565 into pixels (240 * rows). */
void echo_screen_band(unsigned y0, unsigned rows, uint16_t *pixels);
/* Changes whenever the message changes: redraw only then. */
unsigned echo_screen_serial(void);
