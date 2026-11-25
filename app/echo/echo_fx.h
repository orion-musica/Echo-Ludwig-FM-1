/* ECHO LUDWIG - light wave over the LEDs and a white screen flash on presses. */
#pragma once
#include "echo_port.h"
enum { FX_OFF, FX_WAVE, FX_WAVE_FLASH };
void fx_init(void);
void fx_mode(unsigned mode);                  /* off / wave / wave and flash */
void fx_base(unsigned led_index, int on);     /* the game's own LED state */
void fx_press(unsigned led_index, uint32_t now); /* 0..13 buttons, 14..40 keys */
void fx_tick(uint32_t now);                   /* call every control tick */
int fx_flash(void);                           /* 1 while the screen should be white */
