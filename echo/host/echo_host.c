/* ECHO LUDWIG - host test: runs the real game code on a PC with a
 * scripted "player", writes the audio to a WAV file and logs the screen.
 *   make -C firmware/host && ./firmware/host/echo_host out.wav            */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "echo_game.h"
#include "echo_synth.h"
#include "echo_speech.h"
#include "echo_screen.h"

int echo_debug_expected(void);
int echo_debug_screen(void);
static unsigned unknown_words, leds_set;
void echo_host_unknown_word(const char *w, unsigned len) { printf("  !! unknown word: %.*s\n", (int)len, w); ++unknown_words; }
void echo_hal_led(unsigned i, int on) { (void)i; leds_set += on != 0; }
int echo_hal_save(const void *d, unsigned n) { FILE *f = fopen("echo_save.bin", "wb"); if (!f) return -1; fwrite(d, 1, n, f); fclose(f); return 0; }
int echo_hal_load(void *d, unsigned n) { FILE *f = fopen("echo_save.bin", "rb"); if (!f) return -1; size_t r = fread(d, 1, n, f); fclose(f); return r == n ? 0 : -1; }

static FILE *wav; static unsigned frames_written;
static void wav_open(const char *fn)
{
    wav = fopen(fn, "wb"); if (!wav) return;
    uint8_t h[44] = {0}; fwrite(h, 1, 44, wav);
}
static void wav_close(void)
{
    if (!wav) return;
    uint32_t data = frames_written * 4, v; uint16_t s;
    fseek(wav, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, wav); v = 36 + data; fwrite(&v, 4, 1, wav); fwrite("WAVEfmt ", 1, 8, wav);
    v = 16; fwrite(&v, 4, 1, wav); s = 1; fwrite(&s, 2, 1, wav); s = 2; fwrite(&s, 2, 1, wav);
    v = 44100; fwrite(&v, 4, 1, wav); v = 44100 * 4; fwrite(&v, 4, 1, wav); s = 4; fwrite(&s, 2, 1, wav);
    s = 16; fwrite(&s, 2, 1, wav); fwrite("data", 1, 4, wav); fwrite(&data, 4, 1, wav); fclose(wav);
}
static void dump_screen(const char *fn)
{
    static uint16_t band[240 * 16];
    FILE *f = fopen(fn, "wb"); if (!f) return;
    fprintf(f, "P6 240 240 255\n");
    for (unsigned y = 0; y < 240; y += 16) {
        echo_screen_band(y, 16, band);
        for (unsigned i = 0; i < 240 * 16; ++i) {
            uint16_t p = band[i];
            uint8_t rgb[3] = {(uint8_t)((p >> 11) << 3), (uint8_t)(((p >> 5) & 63) << 2), (uint8_t)((p & 31) << 3)};
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    remove("echo_save.bin");
    wav_open(argc > 1 ? argv[1] : "echo_host.wav");
    unsigned max_ms = argc > 2 ? (unsigned)atoi(argv[2]) : 200000;
    echo_input in; memset(&in, 0, sizeof in);
    static int32_t block[256];
    double audio_due = 0;
    unsigned last_serial = ~0u, shots = 0, step = 0, wrong_once = 0;
    uint32_t release_at = 0, next_action = 0;
    int holding_key = -1;
    echo_game_init(0);
    for (uint32_t t = 0; t < max_ms; t += 5) {
        /* ---- scripted player ---- */
        if (release_at && t >= release_at) { in.buttons = 0; if (holding_key >= 0) in.keys = 0; holding_key = -1; release_at = 0; }
        int scr = echo_debug_screen(), want = echo_debug_expected();
        if (!release_at && t >= next_action) {
            if (scr == 1 && step == 0) { in.buttons = 1u << B_OCT_UP; release_at = t + 60; next_action = t + 2500; step = 1; }
            else if (scr == 1 && step == 1) { in.buttons = 1u << B_OCT_DOWN; release_at = t + 60; next_action = t + 2500; step = 2; }
            else if (scr == 1 && step == 2) { in.buttons = 1u << B_PLAY; release_at = t + 60; next_action = t + 4000; step = 3; }
            else if (scr == 2 && step == 3) { in.buttons = 1u << B_PLAY; release_at = t + 60; next_action = t + 3500; step = 4; }
            else if (scr == 3 && step == 4) { in.buttons = 1u << B_OCT_UP; release_at = t + 60; next_action = t + 3500; step = 5; }
            else if (scr == 3 && step == 5) { in.buttons = 1u << B_PLAY; release_at = t + 60; next_action = t + 500; step = 6; }
            else if (scr == 4 && want >= 0) {
                int key = want - KEY_LOWEST_MIDI;
                if (!wrong_once && step == 6) { key = key > 0 ? key - 1 : key + 1; wrong_once = 1; }
                in.keys = 1u << key; holding_key = key; release_at = t + 140; next_action = t + 260;
            } else if (scr == 3 && step == 6) { printf("[%6.2fs] back in the record list - game finished\n", t / 1000.0); step = 7;
                in.buttons = 1u << B_HOME; release_at = t + 60; next_action = t + 3000; }
            else if (step == 7 && scr == 2) { in.buttons = 1u << B_HOME; release_at = t + 60; next_action = t + 3000; step = 8; }
            else if (step == 8 && scr == 1) { break; }
        }
        echo_game_tick(t, &in);
        memset(in.enc, 0, sizeof in.enc);
        unsigned icon; const char *a, *b; unsigned serial;
        echo_game_message(&icon, &a, &b, &serial);
        if (serial != last_serial) {
            last_serial = serial;
            printf("[%6.2fs] screen: icon %2u | %-22s | %s\n", t / 1000.0, icon, a, b);
            if (shots < 12) { char fn[64]; snprintf(fn, sizeof fn, "screen_%02u.ppm", shots++); dump_screen(fn); }
        }
        /* ---- audio: render what 5 ms needs ---- */
        audio_due += 5 * 44.1;
        while (audio_due >= 128) {
            memset(block, 0, sizeof block);
            echo_audio_render(block, 128);
            if (wav) { for (int i = 0; i < 256; ++i) { int16_t s = (int16_t)(block[i] >> 8); fwrite(&s, 2, 1, wav); } frames_written += 128; }
            audio_due -= 128;
        }
    }
    wav_close();
    printf("unknown words: %u, led on events: %u, audio %.1f s\n", unknown_words, leds_set, frames_written / 44100.0);
    return unknown_words ? 1 : 0;
}
