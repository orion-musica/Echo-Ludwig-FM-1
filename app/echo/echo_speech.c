/* LUDWIG VOICE - tiny formant speech synthesizer for ECHO LUDWIG.
 * Cascade formant synthesis (after Klatt 1980, heavily simplified):
 * a glottal pulse + aspiration noise -> four resonators in series for
 * vowels and voiced sounds, plus one parallel noise band for S, SH, F...
 * Targets come from echo_data.h (generated from data/phonemes.txt). */
#include "echo_speech.h"
#include "echo_data.h"
#include <string.h>

#define SR 22050.0f
#define FRAME 110            /* parameter update every 5 ms */
#define MAX_ITEMS 256
#define RING 4096            /* 185 ms of 22.05 kHz audio */

typedef struct { uint8_t ph, stress, flags; uint8_t pitch; uint16_t ms; } item_t;
enum { IT_FINAL = 1 };
typedef struct { float f[3], b[3], av, ah, af, fc, fb; } params_t;
typedef struct { params_t p, end; float f0mul; uint16_t samples; uint8_t glide, instant; } seg_t;
typedef struct { float a, b, c, y1, y2; } res_t;

static item_t items[MAX_ITEMS];
static unsigned n_items, cur_item;
static seg_t segs[3];
static unsigned n_segs, cur_seg, seg_left;
static uint8_t active;
static params_t cur, target;
static float f0, f0_target, phase, base_f0 = 112.0f, formant_scale = 1.0f, rate = 1.0f;
static unsigned frame_count;
static res_t r1, r2, r3, r4, rf;
static float tilt, warm, dc_x, dc_y;
static uint32_t noise_state = 0x1234567u;
static float glottal[257];

static int16_t ring[RING];
static volatile uint32_t ring_head, ring_tail;
static volatile uint8_t flush_request;
static int16_t isr_last;
static uint8_t isr_odd;

/* ---------------------------------------------------------------- text */
static int lookup(const char *word, unsigned len, const uint8_t **code)
{
    const uint8_t *p = echo_lexicon;
    while (*p) {
        const char *w = (const char *)p;
        size_t wl = strlen(w);
        const uint8_t *c = p + wl + 1;
        if (wl == len && !memcmp(w, word, len)) { *code = c + 1; return c[0]; }
        p = c + 1 + c[0];
    }
    return -1;
}
static void push(uint8_t ph, uint8_t stress, uint16_t ms)
{
    if (n_items >= MAX_ITEMS) return;
    item_t *it = &items[n_items++];
    it->ph = ph; it->stress = stress; it->flags = 0; it->pitch = 100; it->ms = ms;
}
static void push_word(const char *w, unsigned len)
{
    char low[24];
    if (len == 0 || len > 20) return;
    if (len == 1 && ((w[0] >= 'A' && w[0] <= 'G') || w[0] == 'I')) {
        memcpy(low, "letter_", 7); low[7] = (char)(w[0] + 32); len = 8;
    } else {
        for (unsigned i = 0; i < len; ++i) low[i] = (char)(w[i] >= 'A' && w[i] <= 'Z' ? w[i] + 32 : w[i]);
    }
    const uint8_t *code;
    int n = lookup(low, len, &code);
    if (n < 0) { /* unknown word: short gap */
#ifdef ECHO_HOST_DEBUG
        extern void echo_host_unknown_word(const char *w, unsigned len);
        echo_host_unknown_word(low, len);
#endif
        push(ECHO_PAUSE_PHONEME, 0, 60); return;
    }
    for (int i = 0; i < n; ++i) push(code[i] & 63, code[i] >> 6, 0);
}
static const char *const small_numbers[20] = {
    "zero","one","two","three","four","five","six","seven","eight","nine","ten",
    "eleven","twelve","thirteen","fourteen","fifteen","sixteen","seventeen","eighteen","nineteen"};
static const char *const tens[10] = {"","","twenty","thirty","forty","fifty","sixty","seventy","eighty","ninety"};
static void push_text_word(const char *w) { push_word(w, (unsigned)strlen(w)); }
static void push_number(unsigned v)
{
    if (v >= 10000) v = 9999;
    if (v >= 1000) { push_number(v / 1000); push_text_word("thousand"); v %= 1000; if (!v) return; }
    if (v >= 100) { push_number(v / 100); push_text_word("hundred"); v %= 100; if (!v) return; }
    if (v < 20) { push_text_word(small_numbers[v]); return; }
    push_text_word(tens[v / 10]);
    if (v % 10) push_text_word(small_numbers[v % 10]);
}
/* Shape the pitch of one phrase: gentle declination, accents on stressed
 * vowels, a fall and lengthening on the last stressed vowel. */
static void close_phrase(unsigned start, uint16_t pause_ms)
{
    unsigned n = n_items - start;
    if (n) {
        int last_vowel = -1, last_stressed = -1;
        for (unsigned i = start; i < n_items; ++i) {
            const echo_phoneme *p = &echo_phoneme_table[items[i].ph];
            if (p->type <= PH_D) { last_vowel = (int)i; if (items[i].stress == 1) last_stressed = (int)i; }
            float decl = 1.06f - 0.16f * (float)(i - start) / (float)n;
            float accent = items[i].stress == 1 ? 1.12f : items[i].stress == 2 ? 1.05f : 1.0f;
            items[i].pitch = (uint8_t)(decl * accent * 100.0f);
        }
        if (last_stressed < 0) last_stressed = last_vowel;
        if (last_stressed >= 0) {
            items[last_stressed].flags |= IT_FINAL;
            for (unsigned i = (unsigned)last_stressed; i < n_items; ++i)
                items[i].pitch = (uint8_t)(items[i].pitch * 0.88f);
        }
    }
    push(ECHO_PAUSE_PHONEME, 0, pause_ms);
}
static void parse(const char *text)
{
    unsigned phrase = n_items;
    const char *s = text;
    while (*s) {
        if (*s == ',' || *s == '.' || *s == '!' || *s == '?' || *s == ';') {
            close_phrase(phrase, *s == ',' ? 170 : 300); phrase = n_items; ++s; continue;
        }
        if (*s == ' ') { ++s; continue; }
        const char *w = s;
        while (*s && *s != ' ' && *s != ',' && *s != '.' && *s != '!' && *s != '?' && *s != ';') ++s;
        unsigned len = (unsigned)(s - w);
        if (w[0] >= '0' && w[0] <= '9') {
            unsigned v = 0;
            for (unsigned i = 0; i < len && w[i] >= '0' && w[i] <= '9'; ++i) v = v * 10 + (unsigned)(w[i] - '0');
            push_number(v);
        } else push_word(w, len);
    }
    if (n_items > phrase) close_phrase(phrase, 140);
}

/* ------------------------------------------------------------ synthesis */
static float noise(void)
{
    noise_state ^= noise_state << 13; noise_state ^= noise_state >> 17; noise_state ^= noise_state << 5;
    return (float)(int32_t)noise_state * (1.0f / 2147483648.0f);
}
static void res_set(res_t *r, float f, float bw, int peak_normalized)
{
    const float pi = 3.14159265f;
    if (f > SR * 0.47f) f = SR * 0.47f;
    float rr = echo_expn(pi * bw / SR);
    float th = 2.0f * pi * f / SR;
    float cth = echo_cos(th);
    r->c = -rr * rr;
    r->b = 2.0f * rr * cth;
    if (peak_normalized) {
        float c2 = 2.0f * cth * cth - 1.0f;
        r->a = (1.0f - rr) * echo_sqrt(1.0f - 2.0f * rr * c2 + rr * rr);
    } else r->a = 1.0f - r->b - r->c;
}
static inline float res_run(res_t *r, float x)
{
    float y = r->a * x + r->b * r->y1 + r->c * r->y2;
    r->y2 = r->y1; r->y1 = y;
    return y;
}
static void load_params(params_t *p, const echo_phoneme *ph, int second)
{
    float fs = formant_scale;
    p->f[0] = (second ? ph->f1b : ph->f1) * fs;
    p->f[1] = (second ? ph->f2b : ph->f2) * fs;
    p->f[2] = (second ? ph->f3b : ph->f3) * fs;
    p->b[0] = ph->b1; p->b[1] = ph->b2; p->b[2] = ph->b3;
    p->av = ph->av * 0.01f; p->ah = ph->ah * 0.01f; p->af = ph->af * 0.01f;
    p->fc = ph->fc ? ph->fc : 3000.0f; p->fb = ph->fb ? ph->fb : 1000.0f;
}
static void silent(params_t *p) { p->av = p->ah = p->af = 0.0f; }
static uint16_t ms_to_samples(float ms) { float s = ms * (SR / 1000.0f); return (uint16_t)(s > 60000.0f ? 60000.0f : s); }

static const echo_phoneme *neighbour(unsigned i)
{
    if (i < n_items && items[i].ph != ECHO_PAUSE_PHONEME) return &echo_phoneme_table[items[i].ph];
    return &echo_phoneme_table[ECHO_PAUSE_PHONEME];
}
static void build_segments(unsigned index)
{
    const item_t *it = &items[index];
    const echo_phoneme *ph = &echo_phoneme_table[it->ph];
    const echo_phoneme *next = neighbour(index + 1);
    float mul = (float)it->pitch * 0.01f;
    float dur = ph->dur;
    if (ph->type <= PH_D) dur *= it->stress == 1 ? 1.35f : it->stress == 2 ? 1.12f : 0.8f;
    if (it->flags & IT_FINAL) dur *= 1.4f;
    if (ph->type == PH_X && it->ms) dur = it->ms;
    dur *= rate;
    memset(segs, 0, sizeof segs);
    n_segs = 1;
    seg_t *s = segs;
    for (int i = 0; i < 3; ++i) s[i].f0mul = mul;
    load_params(&s[0].p, ph, 0);
    s[0].samples = ms_to_samples(dur);
    switch (ph->type) {
    case PH_D:
        s[0].samples = ms_to_samples(dur * 0.4f);
        load_params(&s[1].p, ph, 0); load_params(&s[1].end, ph, 1);
        s[1].glide = 1; s[1].samples = ms_to_samples(dur * 0.6f); n_segs = 2;
        break;
    case PH_H:
        load_params(&s[0].p, next, 0);
        s[0].p.av = 0.0f; s[0].p.af = 0.0f; s[0].p.ah = ph->ah * 0.01f;
        s[0].p.b[0] = 300; s[0].p.b[1] = 300; s[0].p.b[2] = 300;
        break;
    case PH_P: case PH_B: case PH_C: case PH_J: {
        int voiced = ph->type == PH_B || ph->type == PH_J;
        int affricate = ph->type == PH_C || ph->type == PH_J;
        silent(&s[0].p);
        if (voiced) { s[0].p.av = ph->av * 0.01f; s[0].p.f[0] = 200.0f * formant_scale; }
        s[0].samples = ms_to_samples(dur * (affricate ? 0.7f : 1.0f));
        load_params(&s[1].p, ph, 0);
        s[1].p.ah = 0.0f;
        s[1].p.av = voiced ? ph->av * 0.01f : 0.0f;
        s[1].instant = 1;
        s[1].samples = ms_to_samples(affricate ? 75.0f * rate : (voiced ? 8.0f : 12.0f));
        n_segs = 2;
        if (ph->type == PH_P && next->type <= PH_N) { /* aspiration into the next sound */
            load_params(&s[2].p, next, 0);
            s[2].p.av = 0.0f; s[2].p.af = 0.0f; s[2].p.ah = ph->ah * 0.01f;
            s[2].samples = ms_to_samples(38.0f * rate);
            n_segs = 3;
        }
        break;
    }
    case PH_X:
        silent(&s[0].p);
        break;
    default:
        break;
    }
    cur_seg = 0;
    seg_left = s[0].samples ? s[0].samples : 1;
    target = s[0].p;
    f0_target = base_f0 * s[0].f0mul;
    if (s[0].instant) { cur.av = target.av; cur.af = target.af; cur.ah = target.ah; }
}
static int advance(void)
{
    while (1) {
        if (seg_left) return 1;
        if (++cur_seg < n_segs) {
            seg_t *s = &segs[cur_seg];
            seg_left = s->samples ? s->samples : 1;
            target = s->p;
            f0_target = base_f0 * s->f0mul;
            if (s->instant) { cur.av = target.av; cur.af = target.af; cur.ah = target.ah; cur.fc = target.fc; cur.fb = target.fb; }
            return 1;
        }
        if (++cur_item >= n_items) { active = 0; n_items = cur_item = 0; return 0; }
        build_segments(cur_item);
    }
}
static void update_frame(void)
{
    seg_t *s = &segs[cur_seg];
    params_t t = target;
    if (s->glide && s->samples) {
        float k = 1.0f - (float)seg_left / (float)s->samples;
        for (int i = 0; i < 3; ++i) t.f[i] = s->p.f[i] + (s->end.f[i] - s->p.f[i]) * k;
    }
    for (int i = 0; i < 3; ++i) {
        cur.f[i] += (t.f[i] - cur.f[i]) * 0.38f;
        cur.b[i] += (t.b[i] - cur.b[i]) * 0.38f;
    }
    cur.av += (t.av - cur.av) * 0.45f;
    cur.ah += (t.ah - cur.ah) * 0.5f;
    cur.af += (t.af - cur.af) * 0.6f;
    cur.fc += (t.fc - cur.fc) * 0.6f;
    cur.fb += (t.fb - cur.fb) * 0.6f;
    f0 += (f0_target - f0) * 0.18f;
    res_set(&r1, cur.f[0], cur.b[0], 0);
    res_set(&r2, cur.f[1], cur.b[1], 0);
    res_set(&r3, cur.f[2], cur.b[2], 0);
    res_set(&rf, cur.fc, cur.fb, 1);
}
static int synth_sample(int16_t *out)
{
    if (!active || !advance()) return 0;
    if (frame_count == 0) update_frame();
    if (++frame_count >= FRAME) frame_count = 0;
    --seg_left;
    phase += f0 * (1.0f / SR);
    if (phase >= 1.0f) phase -= 1.0f;
    float idx = phase * 256.0f;
    int i0 = (int)idx;
    float g = glottal[i0] + (glottal[i0 + 1] - glottal[i0]) * (idx - (float)i0);
    tilt += (g - tilt) * 0.55f;                 /* soft spectral tilt: warmer voice */
    float n = noise();
    float x = tilt * cur.av + n * (cur.ah * 0.35f + cur.av * 0.015f);
    x = res_run(&r1, x); x = res_run(&r2, x); x = res_run(&r3, x); x = res_run(&r4, x);
    warm += (x - warm) * 0.62f;                 /* round off the buzz above ~3.5 kHz */
    float y = warm * 0.12f + res_run(&rf, n) * cur.af * 0.7f;
    float d = y - dc_x + 0.995f * dc_y; dc_x = y; dc_y = d;  /* DC blocker */
    y = d;
    y = y / (1.0f + (y < 0 ? -y : y) * 0.6f);   /* gentle soft clip */
    int v = (int)(y * 26000.0f);
    *out = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    return 1;
}

/* -------------------------------------------------------------- public */
void sp_init(void)
{
    const float pi = 3.14159265f;
    /* Derivative of a Rosenberg glottal flow pulse (open 40 %, closing 16 %). */
    float prev = 0.0f;
    for (int i = 0; i <= 256; ++i) {
        float t = (float)(i & 255) / 256.0f, u;
        if (t < 0.40f) u = 0.5f * (1.0f - echo_cos(pi * t / 0.40f));
        else if (t < 0.56f) u = echo_cos(pi * 0.5f * (t - 0.40f) / 0.16f);
        else u = 0.0f;
        glottal[i] = (u - prev) * 40.0f;
        prev = u;
    }
    glottal[0] = glottal[256] = glottal[1] * 0.5f;
    res_set(&r4, 3500.0f, 260.0f, 0);
    memset(&cur, 0, sizeof cur);
    cur.f[0] = 500; cur.f[1] = 1500; cur.f[2] = 2500; cur.b[0] = cur.b[1] = cur.b[2] = 100;
    cur.fc = 3000; cur.fb = 1000;
    f0 = base_f0;
    active = 0; n_items = cur_item = 0;
    ring_head = ring_tail = 0;
}
void sp_set_voice(unsigned voice, unsigned speed)
{
    static const float rates[4] = {1.3f, 1.0f, 0.8f, 0.64f};
    base_f0 = voice == SP_VOICE_CLARA ? 200.0f : 112.0f;
    formant_scale = voice == SP_VOICE_CLARA ? 1.16f : 1.0f;
    rate = rates[speed & 3];
}
void sp_queue(const char *text)
{
    if (!text) return;
    if (!active) { n_items = cur_item = 0; }
    unsigned start = n_items;
    parse(text);
    if (!active && n_items > start) {
        active = 1; frame_count = 0;
        build_segments(0);
    }
}
void sp_stop(void)
{
    active = 0; n_items = cur_item = 0; seg_left = 0;
    flush_request = 1;
}
void sp_say(const char *text) { sp_stop(); sp_queue(text); }
int sp_busy(void) { return active || ring_head != ring_tail || flush_request; }
void sp_pump(void)
{
    if (flush_request) return;   /* the interrupt has not emptied the ring yet */
    unsigned budget = 1024;      /* bound the work done per control-loop pass */
    while (budget-- && active) {
        uint32_t head = ring_head;
        if (head - ring_tail >= RING - 1) break;
        int16_t v;
        if (!synth_sample(&v)) break;
        ring[head & (RING - 1)] = v;
        ring_head = head + 1;
    }
}
FM1_AUDIO_CODE void sp_render_block(float *mono, unsigned frames, float gain)
{
    if (flush_request) { ring_tail = ring_head; isr_last = 0; flush_request = 0; }
    uint32_t tail = ring_tail;
    const float k = gain * (1.0f / 32768.0f);
    for (unsigned i = 0; i < frames; ++i) {
        int16_t s;
        if (!isr_odd) {
            if (tail == ring_head) { isr_last = 0; continue; }
            s = ring[tail & (RING - 1)];
            mono[i] += (float)(isr_last + s) * 0.5f * k;
            isr_last = s; isr_odd = 1;
        } else {
            mono[i] += (float)isr_last * k;
            ++tail; isr_odd = 0;
        }
    }
    ring_tail = tail;
}
unsigned sp_render_offline(const char *text, int16_t *pcm, unsigned max_samples)
{
    sp_say(text);
    flush_request = 0; ring_head = ring_tail = 0;
    unsigned n = 0;
    while (n < max_samples && synth_sample(&pcm[n])) ++n;
    return n;
}
