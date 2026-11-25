/* ECHO LUDWIG - M-VAVE FM-1 adapter (fm1_open SDK).
 * Connects the platform-independent game to the FM-1 panel, LCD, LEDs and
 * user Flash. Not part of the PC test or the stand-alone browser canvas.
 *
 * Hook-up (ECHO_LUDWIG in echo_fm1.h):
 *   app/screen.c        groove_screen_init()    -> echo_fm1_init()
 *   app/screen.c        groove_screen_control() -> echo_fm1_control()
 *   app/audio_output.c  fm1_output_mix() starts with echo_audio_render(output, FM1_BLOCK_FRAMES)
 * The same hooks run on the FM-1 and in the fm1_open browser simulator. */
#include "fm1.h"
#include "fm1_engine_control.h"
#include "echo_game.h"
#include "echo_synth.h"
#include "echo_screen.h"
#include "echo_fx.h"
#include <string.h>

int groove_master_update(unsigned adc); /* engine: MASTER knob -> output level */

/* Save record lives in the Flash sector of FM-1 project slot 15 (index 14).
 * 3310 / factory projects in that slot are overwritten: back it up first. */
#define ECHO_SAVE_OFFSET (14u * FM1_FLASH_SECTOR_BYTES)

volatile uint8_t echo_ludwig_active = 1;
static uint16_t band[FM1_SCREEN_WIDTH * 8];
static unsigned shown_serial = ~0u;
static uint8_t cleared;

void echo_hal_led(unsigned index, int on) { fm1_led_set(index, on); }
int echo_hal_load(void *data, unsigned bytes)
{
    if (fm1_flash_size() < ECHO_SAVE_OFFSET + FM1_FLASH_SECTOR_BYTES) return -1;
    return fm1_flash_read(ECHO_SAVE_OFFSET, data, bytes) == FM1_FLASH_OK ? 0 : -1;
}
int echo_hal_save(const void *data, unsigned bytes)
{
    static uint8_t copy[256] __attribute__((aligned(4)));
    if (bytes > sizeof copy || fm1_flash_size() < ECHO_SAVE_OFFSET + FM1_FLASH_SECTOR_BYTES) return -1;
    memcpy(copy, data, bytes);                 /* input must not alias the mapped partition */
    if (fm1_flash_erase(ECHO_SAVE_OFFSET) != FM1_FLASH_OK) return -1;
    return fm1_flash_write(ECHO_SAVE_OFFSET, copy, bytes) == FM1_FLASH_OK ? 0 : -1;
}

static uint8_t was_flash;
static void redraw(void)
{
    unsigned serial = echo_screen_serial();
    if (serial == shown_serial && cleared) return;
    int flash = fx_flash();
    if (flash) {                               /* one fill: the whole screen white */
        fm1_screen_fill(0, 0, FM1_SCREEN_WIDTH, FM1_SCREEN_HEIGHT, 0xffffu);
        was_flash = 1; shown_serial = serial;
        return;
    }
    if (!cleared || was_flash) { fm1_screen_fill(0, 0, FM1_SCREEN_WIDTH, FM1_SCREEN_HEIGHT, 0); cleared = 1; was_flash = 0; }
    /* the message only ever occupies rows 80..159 */
    for (unsigned y = 80; y < 160; y += 8) {
        echo_screen_band(y, 8, band);
        fm1_screen_blit(0, y, FM1_SCREEN_WIDTH, 8, band, FM1_SCREEN_WIDTH * 8);
    }
    shown_serial = serial;
}

void echo_fm1_init(void)
{
    fm1_controls_init();
    fm1_screen_init();
    fm1_engine_pause(); fm1_engine_stop(); fm1_engine_resume(); /* the instrument stays silent */
    cleared = 0; shown_serial = ~0u;
    echo_game_init(fm1_millis());
    redraw();
}

void echo_fm1_control(void)
{
    struct fm1_controls c;
    fm1_controls_read(&c);
    groove_master_update(c.knob);              /* MASTER knob keeps working as volume */
    echo_input in;
    in.keys = c.keys & 0x07ffffffu;
    in.buttons = c.buttons & 0x3fffu;
    for (unsigned i = 0; i < 7; ++i) in.enc[i] = c.encoders[i];
    echo_game_tick(fm1_millis(), &in);
    redraw();
}
