/* ECHO LUDWIG - light effects for sighted and low-vision players:
 *  - a light wave that runs left and right from the pressed control across
 *    every button and key LED;
 *  - a short white flash of the whole screen on every press.
 * The game's own LEDs (the note being played) stay underneath the wave.
 *
 * Photosensitive safety: the screen flashes at most 3 times a second
 * (WCAG 2.3.1 "three flashes"), each flash lasts 60 ms, and the "lights"
 * setting turns the flash, or both effects, off. */
#include "echo_fx.h"
#include "echo_game.h"

enum { LEDS = 41, WAVE_STEP_MS = 14, WAVE_TAIL = 3, MAX_WAVES = 4, FLASH_MS = 60, FLASH_GAP_MS = 334 };

/* Left-to-right position of each LED on a 0..26 scale. Keys 14..40 are the
 * keybed F3..G5 in order. The 14 panel buttons are spread evenly over the
 * same width until their real layout is measured on the device. */
static uint8_t led_x(unsigned i)
{
    if (i >= 14) return (uint8_t)(i - 14);
    return (uint8_t)(i * 2);
}

static uint64_t base, shown;
static struct { uint32_t start; int8_t x; uint8_t on; } waves[MAX_WAVES];
static unsigned mode = FX_WAVE_FLASH, next_wave;
static uint32_t flash_start, last_flash;
static uint8_t flashing, flash_ever;

void fx_init(void)
{
    base = shown = 0;
    for (unsigned i = 0; i < MAX_WAVES; ++i) waves[i].on = 0;
    flashing = 0; flash_ever = 0;
    for (unsigned i = 0; i < LEDS; ++i) echo_hal_led(i, 0);
}
void fx_mode(unsigned m) { mode = m > FX_WAVE_FLASH ? FX_WAVE_FLASH : m; if (mode != FX_WAVE_FLASH) flashing = 0; }
void fx_base(unsigned index, int on)
{
    if (index >= LEDS) return;
    if (on) base |= 1ull << index; else base &= ~(1ull << index);
}
void fx_press(unsigned led_index, uint32_t now)
{
    if (mode == FX_OFF || led_index >= LEDS) return;
    waves[next_wave].start = now; waves[next_wave].x = (int8_t)led_x(led_index); waves[next_wave].on = 1;
    next_wave = (next_wave + 1) % MAX_WAVES;
    if (mode == FX_WAVE_FLASH && (!flash_ever || now - last_flash >= FLASH_GAP_MS)) {
        flash_start = last_flash = now; flashing = 1; flash_ever = 1;
    }
}
void fx_tick(uint32_t now)
{
    uint64_t want = base;
    for (unsigned w = 0; w < MAX_WAVES; ++w) {
        if (!waves[w].on) continue;
        int front = (int)((now - waves[w].start) / WAVE_STEP_MS), alive = 0;
        for (unsigned i = 0; i < LEDS; ++i) {
            int d = (int)led_x(i) - waves[w].x; if (d < 0) d = -d;
            if (d <= front && d > front - WAVE_TAIL) want |= 1ull << i;
        }
        if (front - WAVE_TAIL <= 26) alive = 1;
        waves[w].on = (uint8_t)alive;
    }
    uint64_t diff = want ^ shown;
    for (unsigned i = 0; diff && i < LEDS; ++i)
        if (diff & (1ull << i)) { echo_hal_led(i, (int)((want >> i) & 1u)); diff &= ~(1ull << i); }
    shown = want;
    if (flashing && now - flash_start >= FLASH_MS) flashing = 0;
}
int fx_flash(void) { return flashing; }
