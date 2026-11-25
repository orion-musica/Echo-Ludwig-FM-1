/* ECHO LUDWIG - LUDWIG VOICE alone, for the website: text in, 22.05 kHz PCM out. */
#include <stddef.h>
#include <stdint.h>
#include "echo_speech.h"
#define EXPORT(name) __attribute__((export_name(name)))
void *memset(void *d, int c, size_t n) { unsigned char *p = d; while (n--) *p++ = (unsigned char)c; return d; }
void *memcpy(void *d, const void *s, size_t n) { unsigned char *p = d; const unsigned char *q = s; while (n--) *p++ = *q++; return d; }
int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *p = a, *q = b;
    for (; n; --n, ++p, ++q) if (*p != *q) return *p - *q;
    return 0;
}
size_t strlen(const char *s) { size_t n = 0; while (s[n]) ++n; return n; }

enum { MAX_SAMPLES = 22050 * 12 };
static char text[600];
static int16_t pcm[MAX_SAMPLES];
EXPORT("init") void v_init(void) { sp_init(); }
EXPORT("voice") void v_voice(unsigned voice, unsigned speed) { sp_set_voice(voice, speed); }
EXPORT("text") char *v_text(void) { return text; }
EXPORT("pcm") int16_t *v_pcm(void) { return pcm; }
/* Speak text[] (NUL-terminated); returns the number of samples in pcm[]. */
EXPORT("say") unsigned v_say(void) { return sp_render_offline(text, pcm, MAX_SAMPLES); }
