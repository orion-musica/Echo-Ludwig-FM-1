/* ECHO LUDWIG - game logic. Platform independent: the same file runs on
 * the M-VAVE FM-1 (echo_fm1.c) and in the browser canvas (WebAssembly).
 *
 * The platform calls echo_game_init once and echo_game_tick from its control
 * loop (every few ms) and provides the echo_hal_* functions below. */
#pragma once
#include "echo_port.h"

/* FM-1 panel buttons, in the order fm1_controls.buttons reports them. */
enum { B_OCT_DOWN, B_OCT_UP, B_FX, B_SCL, B_ENV, B_LFO, B_EDIT, B_GLO,
       B_HOME, B_SAVE, B_ARP, B_SEQ, B_PLAY, B_REC, B_COUNT };
enum { KEY_LOWEST_MIDI = 53, KEY_COUNT = 27 };   /* F3 .. G5 */

typedef struct {
    uint32_t keys;      /* bit n = key n held (F3 + n) */
    uint32_t buttons;   /* bit n = button n held */
    int16_t enc[7];     /* detents turned since the previous tick */
} echo_input;

/* Screen: one small centred message on black. */
enum { IC_NONE, IC_NOTE, IC_EAR, IC_KEYS, IC_CHECK, IC_CROSS, IC_STAR, IC_HEART,
       IC_LOCK, IC_GEAR, IC_HELP, IC_DISC, IC_CRATE, IC_UP, IC_DOWN, IC_PLAY, IC_BOLT, IC_COUNT };

void echo_game_init(uint32_t now_ms);
void echo_game_tick(uint32_t now_ms, const echo_input *in);
/* Current message, for the screen renderer. */
void echo_game_message(unsigned *icon, const char **line1, const char **line2, unsigned *serial);

/* ---- provided by the platform ---- */
void echo_hal_led(unsigned index, int on);              /* 0..13 buttons, 14..40 keys */
int echo_hal_save(const void *data, unsigned bytes);   /* 0 = ok */
int echo_hal_load(void *data, unsigned bytes);         /* 0 = ok */
