/* ECHO LUDWIG - groove synth (see echo_synth.h). */
#include "echo_synth.h"
#include "echo_speech.h"
#include <string.h>

#define FS 44100.0f
#define TICKS_PER_BAR 48

/* -------------------------------------------------- command queue (SPSC) */
enum { C_STYLE, C_NOTE_ON, C_NOTE_OFF, C_PLAY, C_STOP, C_EAR, C_LEVELS };
typedef struct { uint8_t type, a, b, c; uint16_t d, e; } cmd_t;
#define QN 32
static cmd_t queue[QN];
static volatile uint32_t q_head, q_tail;
static void post(uint8_t type, uint8_t a, uint8_t b, uint8_t c, uint16_t d, uint16_t e)
{
    uint32_t h = q_head;
    if (h - q_tail >= QN) return;               /* full: drop (never block) */
    cmd_t *m = &queue[h & (QN - 1)];
    m->type = type; m->a = a; m->b = b; m->c = c; m->d = d; m->e = e;
    q_head = h + 1;
}
/* Two sequence buffers; the control task fills one while the other plays. */
static uint8_t seq_buffer[2][2 * 64];
static uint8_t seq_buffer_count[2];
static unsigned seq_write;

/* ------------------------------------------------------------- tables */
static float note_hz[128];
/* Lead voices: soft two-operator FM (sine carrier, sine modulator), the
 * gentle sound of an electric piano, music box or mellow pad - no raw
 * square or saw waves, no resonance, no distortion. */
typedef struct { float ratio, index, index_decay_s, detune, attack_s, decay_s, sustain, release_s, tone_hz, bass_cutoff; } style_t;
static const style_t styles[STYLE_COUNT] FM1_AUDIO_DATA = {
    /* ratio index idx_dec detune  att    dec   sus   rel   tone  bassc */
    {3.0f, 0.9f, 0.18f, 0.000f, 0.004f, 0.55f, 0.25f, 0.30f, 5200, 700},  /* chiptune: music box     */
    {1.0f, 1.6f, 0.45f, 0.002f, 0.005f, 0.90f, 0.35f, 0.35f, 4200, 450},  /* house: electric piano   */
    {1.0f, 0.7f, 1.20f, 0.005f, 0.018f, 1.50f, 0.70f, 0.55f, 3600, 600},  /* trance: soft pad lead   */
    {0.5f, 1.3f, 0.25f, 0.003f, 0.006f, 0.60f, 0.35f, 0.25f, 3800, 520},  /* electro: woody pluck    */
    {1.0f, 2.0f, 0.20f, 0.004f, 0.005f, 0.70f, 0.45f, 0.25f, 4500, 420},  /* hard techno: bright keys */
};
/* 16-step patterns: kick, hat, clap, bass (bit n = 16th step n). */
static const uint16_t patterns[STYLE_COUNT][4] FM1_AUDIO_DATA = {
    {0x1111, 0x4444, 0x1010, 0x5555},
    {0x1111, 0x4444, 0x1010, 0x4444},
    {0x1111, 0xffff, 0x1010, 0xeeee},
    {0x0441, 0x5555, 0x1010, 0x2489},
    {0x1111, 0x4444, 0x0000, 0x6666},
};
typedef struct { uint8_t midi; uint16_t ms; uint8_t wave; } ear_step;
static const ear_step earcons[EAR_COUNT][6] FM1_AUDIO_DATA = {
    [EAR_TICK]      = {{72, 22, 0}},
    [EAR_WRAP]      = {{88, 30, 0}, {0, 25, 0}, {88, 30, 0}},
    [EAR_ENTER]     = {{72, 45, 0}, {79, 80, 0}},
    [EAR_BACK]      = {{79, 45, 0}, {72, 80, 0}},
    [EAR_LOCKED]    = {{40, 140, 1}},
    [EAR_ERROR]     = {{46, 90, 1}, {45, 120, 1}},
    [EAR_YOUR_TURN] = {{84, 40, 2}, {0, 30, 0}, {88, 70, 2}},
    [EAR_ROUND_OK]  = {{72, 55, 0}, {76, 55, 0}, {79, 55, 0}, {84, 130, 0}},
    [EAR_WRONG]     = {{58, 90, 1}, {53, 170, 1}},
    [EAR_LIFE]      = {{64, 80, 2}, {60, 80, 2}, {55, 220, 2}},
    [EAR_UNLOCK]    = {{72, 70, 0}, {76, 70, 0}, {79, 70, 0}, {84, 70, 0}, {88, 70, 0}, {91, 320, 0}},
    [EAR_GAME_OVER] = {{67, 160, 1}, {63, 160, 1}, {60, 160, 1}, {55, 420, 1}},
    [EAR_STAR]      = {{96, 50, 2}, {91, 40, 2}, {100, 140, 2}},
    [EAR_COMBO]     = {{79, 40, 0}, {86, 80, 0}},
    [EAR_HIGHER]    = {{67, 60, 2}, {74, 110, 2}},
    [EAR_LOWER]     = {{74, 60, 2}, {67, 110, 2}},
};

/* --------------------------------------------------------- ISR state */
static unsigned style = STYLE_CHIP;
static float music_gain = 0.55f, voice_gain = 0.9f;
static int pitch_pan = 1;
static uint32_t rng = 0x9e3779b9u;

typedef struct { float pc, pc2, pm, hz, env, index_env, tone, pan; int gate, attack; uint8_t midi; } lead_t;
static lead_t lead;
typedef struct { float phase, hz, amp, env, low; int gate; } bass_t;
static bass_t bass;
static float kick_phase, kick_env, kick_hz, hat_env, hat_low, hat_soft, clap_env, clap_time, clap_low, clap_band;
static float ear_phase, ear_hz, ear_env;
static int ear_type = -1, ear_step_i, ear_param;
static unsigned ear_left;

static struct {
    int on, flags, loops, note, next_note_tick, gate_tick, playing_note;
    const uint8_t *notes; unsigned count, root;
    float spt, acc; int tick, total;
} seq;

static inline __attribute__((always_inline)) float rnd(void)
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return (float)(int32_t)rng * (1.0f / 2147483648.0f);
}
static inline __attribute__((always_inline)) float sine01(float p) /* p in [0,1) -> parabolic sine */
{
    float x = p * 2.0f - 1.0f;
    float y = 4.0f * x * (1.0f - (x < 0 ? -x : x));
    y = 0.225f * (y * (y < 0 ? -y : y) - y) + y;   /* refine: close to a pure sine */
    return -y;
}
static inline __attribute__((always_inline)) float softclip(float x) { return x / (1.0f + (x < 0 ? -x : x)); }

static FM1_AUDIO_CODE void lead_start(unsigned midi)
{
    lead.midi = (uint8_t)midi;
    lead.hz = note_hz[midi & 127];
    lead.gate = 1; lead.attack = 1;
    lead.index_env = 1.0f;
    if (lead.env < 0.02f) { lead.pc = lead.pc2 = lead.pm = 0.0f; lead.env = 0.0f; }
    float pan = ((float)midi - 66.0f) / 13.0f * 0.6f;
    lead.pan = pitch_pan ? (pan > 0.7f ? 0.7f : pan < -0.7f ? -0.7f : pan) : 0.0f;
}
static FM1_AUDIO_CODE void ear_begin(int type, int param)
{
    ear_type = type; ear_param = param; ear_step_i = 0;
    const ear_step *s = &earcons[type][0];
    int m = s->midi ? s->midi + (type == EAR_TICK || type == EAR_COMBO ? param : 0) : 0;
    ear_hz = m ? note_hz[m > 127 ? 127 : m] : 0.0f;
    ear_left = (unsigned)(s->ms * (FS / 1000.0f));
    ear_env = 1.0f;
}
static FM1_AUDIO_CODE void seq_restart(void)
{
    seq.tick = 0; seq.acc = 0.0f; seq.note = 0; seq.next_note_tick = 0; seq.gate_tick = -1; seq.playing_note = -1;
}
static FM1_AUDIO_CODE void handle(const cmd_t *m)
{
    switch (m->type) {
    case C_STYLE: style = m->a < STYLE_COUNT ? m->a : 0; break;
    case C_NOTE_ON: lead_start(m->a); break;
    case C_NOTE_OFF: lead.gate = 0; break;
    case C_LEVELS: music_gain = m->a * 0.01f; voice_gain = m->b * 0.01f; pitch_pan = m->c; break;
    case C_PLAY: {
        unsigned b = m->c & 1U;
        seq.notes = seq_buffer[b]; seq.count = seq_buffer_count[b];
        seq.flags = m->a; seq.root = m->b; seq.loops = m->e ? m->e : 1;
        seq.spt = FS * 60.0f / ((float)m->d * 12.0f);
        int total = 0;
        for (unsigned i = 0; i < seq.count; ++i) total += seq.notes[i * 2 + 1];
        if (seq.flags & (SEQ_FULL | SEQ_HATS)) total = (total + TICKS_PER_BAR - 1) / TICKS_PER_BAR * TICKS_PER_BAR;
        else total += 4;
        if (total <= 0) total = TICKS_PER_BAR;
        seq.total = total;
        seq_restart();
        seq.on = 1;
        break;
    }
    case C_STOP: seq.on = 0; lead.gate = 0; bass.gate = 0; break;
    case C_EAR: ear_begin(m->a, m->b); break;
    }
}
static FM1_AUDIO_CODE void seq_tick(void)
{
    const style_t *st = &styles[style];
    if (seq.flags & SEQ_LEAD) {
        if (seq.gate_tick == seq.tick) { lead.gate = 0; seq.playing_note = -1; }
        if (seq.note < (int)seq.count && seq.tick == seq.next_note_tick) {
            unsigned midi = seq.notes[seq.note * 2], ticks = seq.notes[seq.note * 2 + 1];
            lead_start(midi);
            seq.playing_note = seq.note;
            seq.gate_tick = seq.tick + (int)(ticks * 0.85f + 0.5f);
            if (seq.gate_tick <= seq.tick) seq.gate_tick = seq.tick + 1;
            seq.next_note_tick += (int)ticks;
            ++seq.note;
        }
    }
    if ((seq.flags & (SEQ_FULL | SEQ_HATS)) && seq.tick % 3 == 0) {
        unsigned step = (unsigned)(seq.tick / 3) & 15U, bit = 1U << step;
        const uint16_t *p = patterns[style];
        if (p[1] & bit) hat_env = (style == STYLE_TRANCE && (step & 1)) ? 0.45f : 0.8f;
        if (seq.flags & SEQ_FULL) {
            if (p[0] & bit) { kick_env = 1.0f; kick_hz = 160.0f; kick_phase = 0.0f; }
            if (p[2] & bit) { clap_env = 1.0f; clap_time = 0.0f; }
            if (p[3] & bit) {
                bass.hz = note_hz[36 + seq.root]; bass.gate = 1; bass.env = 1.0f;
                bass.low = 0.0f;
            } else bass.gate = 0;
        }
    }
    (void)st;
    if (++seq.tick >= seq.total) {
        if (--seq.loops > 0 || (seq.flags & SEQ_LOOP)) seq_restart();
        else { seq.on = 0; lead.gate = 0; bass.gate = 0; seq.playing_note = -1; }
    }
}
static FM1_AUDIO_CODE float lead_sample(void)
{
    const style_t *st = &styles[style];
    if (!lead.gate && lead.env < 0.0003f) return 0.0f;
    float inc = lead.hz / FS;
    lead.pm += inc * st->ratio; if (lead.pm >= 1.0f) lead.pm -= 1.0f;
    float mod = sine01(lead.pm) * st->index * lead.index_env * (0.35f + 0.65f * lead.env);
    lead.pc += inc; if (lead.pc >= 1.0f) lead.pc -= 1.0f;
    float p = lead.pc + mod * 0.159f; p -= (float)(int)p; if (p < 0.0f) p += 1.0f;
    float x = sine01(p);
    if (st->detune > 0.0f) {                   /* a second, slightly detuned voice: width */
        lead.pc2 += inc * (1.0f + st->detune); if (lead.pc2 >= 1.0f) lead.pc2 -= 1.0f;
        float p2 = lead.pc2 + mod * 0.159f; p2 -= (float)(int)p2; if (p2 < 0.0f) p2 += 1.0f;
        x = (x + sine01(p2)) * 0.5f;
    }
    /* envelope: soft attack, exponential decay to sustain, gentle release */
    if (lead.gate) {
        if (lead.attack) { lead.env += 1.0f / (st->attack_s * FS); if (lead.env >= 1.0f) { lead.env = 1.0f; lead.attack = 0; } }
        else lead.env += (st->sustain - lead.env) * (1.0f / (st->decay_s * FS));
    } else lead.env -= lead.env * (3.0f / (st->release_s * FS));
    lead.index_env -= lead.index_env * (1.0f / (st->index_decay_s * FS));
    /* one-pole low-pass rounds off whatever is left above the tone frequency */
    lead.tone += (x - lead.tone) * (st->tone_hz * 6.2831853f / FS);
    return lead.tone * lead.env * 0.5f;
}
static FM1_AUDIO_CODE float drums_bass_sample(void)
{
    float out = 0.0f;
    if (kick_env > 0.0005f) {
        kick_phase += kick_hz / FS; if (kick_phase >= 1.0f) kick_phase -= 1.0f;
        kick_hz = 45.0f + (kick_hz - 45.0f) * 0.9993f;
        float k = sine01(kick_phase) * kick_env;
        if (style == STYLE_HARD) k = softclip(k * 2.0f) * 1.1f;
        out += k * 0.9f;
        kick_env *= style == STYLE_HARD ? 0.99987f : 0.99982f;
    }
    if (hat_env > 0.0005f) {
        float n = rnd();
        hat_low += (n - hat_low) * 0.3f;
        hat_soft += ((n - hat_low) - hat_soft) * 0.45f;   /* tame the top: no hiss */
        out += hat_soft * hat_env * 0.18f;
        hat_env *= 0.9985f;
    }
    if (clap_env > 0.0005f) {
        clap_time += 1.0f / FS;
        float burst = (clap_time < 0.01f || (clap_time > 0.015f && clap_time < 0.022f) ||
                       (clap_time > 0.028f)) ? 1.0f : 0.2f;
        float n = rnd() * burst;
        clap_low += 0.25f * clap_band; float hp = n - clap_low - 0.6f * clap_band; clap_band += 0.25f * hp;
        out += clap_band * clap_env * 0.5f;
        clap_env *= 0.9993f;
    }
    if (bass.env > 0.0005f) {
        bass.phase += bass.hz / FS; if (bass.phase >= 1.0f) bass.phase -= 1.0f;
        float x = bass.phase * 2.0f - 1.0f;
        float a = styles[style].bass_cutoff * 2.0f * 3.14159265f / FS;  /* round, soft bass */
        bass.low += (x - bass.low) * a * (1.0f + bass.env);
        out += bass.low * bass.env * 0.5f;
        bass.env *= bass.gate ? 0.99993f : 0.9993f;
    }
    return out;
}
static FM1_AUDIO_CODE float earcon_sample(void)
{
    if (ear_type < 0) return 0.0f;
    if (ear_left == 0) {
        ++ear_step_i;
        if (ear_step_i >= 6 || earcons[ear_type][ear_step_i].ms == 0) { ear_type = -1; return 0.0f; }
        const ear_step *s = &earcons[ear_type][ear_step_i];
        int m = s->midi ? s->midi + (ear_type == EAR_COMBO ? ear_param : 0) : 0;
        ear_hz = m ? note_hz[m > 127 ? 127 : m] : 0.0f;
        ear_left = (unsigned)(s->ms * (FS / 1000.0f));
        ear_env = 1.0f;
    }
    --ear_left;
    if (ear_hz <= 0.0f) return 0.0f;
    const ear_step *s = &earcons[ear_type][ear_step_i];
    ear_phase += ear_hz / FS; if (ear_phase >= 1.0f) ear_phase -= 1.0f;
    float v = s->wave == 1 ? sine01(ear_phase) * 0.9f + sine01(ear_phase * 2.0f - (float)(int)(ear_phase * 2.0f)) * 0.15f
            : s->wave == 2 ? sine01(ear_phase)
            : (ear_phase < 0.5f ? ear_phase * 4.0f - 1.0f : 3.0f - ear_phase * 4.0f);
    float fade = ear_left < 220 ? (float)ear_left / 220.0f : 1.0f; /* no clicks */
    ear_env *= 0.99997f;
    return v * ear_env * fade * 0.32f;
}

static float mono_block[256];
FM1_AUDIO_CODE void echo_audio_render(int32_t *out, unsigned frames)
{
    while (q_tail != q_head) { handle(&queue[q_tail & (QN - 1)]); ++q_tail; }
    if (frames > 256) frames = 256;
    for (unsigned i = 0; i < frames; ++i) mono_block[i] = 0.0f;
    sp_render_block(mono_block, frames, voice_gain * 1.7f);
    for (unsigned i = 0; i < frames; ++i) {
        if (seq.on) { seq.acc += 1.0f; while (seq.acc >= seq.spt && seq.on) { seq.acc -= seq.spt; seq_tick(); } }
        float l = lead_sample();
        float c = (drums_bass_sample() + earcon_sample()) * music_gain + mono_block[i];
        float lm = l * music_gain;
        float left = c + lm * (1.0f - lead.pan), right = c + lm * (1.0f + lead.pan);
        left = softclip(left * 1.2f); right = softclip(right * 1.2f);
        int32_t L = out[2 * i] + (int32_t)(left * 7000000.0f), R = out[2 * i + 1] + (int32_t)(right * 7000000.0f);
        out[2 * i] = L > 8388607 ? 8388607 : L < -8388608 ? -8388608 : L;
        out[2 * i + 1] = R > 8388607 ? 8388607 : R < -8388608 ? -8388608 : R;
    }
}

/* ------------------------------------------------------- control task */
void syn_init(void)
{
    float f = 440.0f;
    note_hz[69] = f;
    for (int i = 70; i < 128; ++i) note_hz[i] = note_hz[i - 1] * 1.0594630943592953f;
    for (int i = 68; i >= 0; --i) note_hz[i] = note_hz[i + 1] / 1.0594630943592953f;
    memset(&lead, 0, sizeof lead); memset(&bass, 0, sizeof bass); memset(&seq, 0, sizeof seq);
    seq.playing_note = -1; ear_type = -1;
    q_head = q_tail = 0;
}
void syn_style(unsigned s) { post(C_STYLE, (uint8_t)s, 0, 0, 0, 0); }
void syn_levels(float music, float voice, int pan)
{
    post(C_LEVELS, (uint8_t)(music * 100.0f), (uint8_t)(voice * 100.0f), (uint8_t)(pan != 0), 0, 0);
}
void syn_note_on(unsigned midi) { post(C_NOTE_ON, (uint8_t)midi, 0, 0, 0, 0); }
void syn_note_off(void) { post(C_NOTE_OFF, 0, 0, 0, 0, 0); }
void syn_play(const uint8_t *notes, unsigned count, unsigned bpm, unsigned root, unsigned flags, unsigned loops)
{
    if (count > 64) count = 64;
    unsigned b = seq_write ^= 1U;
    memcpy(seq_buffer[b], notes, count * 2);
    seq_buffer_count[b] = (uint8_t)count;
    post(C_PLAY, (uint8_t)flags, (uint8_t)root, (uint8_t)b, (uint16_t)bpm, (uint16_t)loops);
}
void syn_stop(void) { post(C_STOP, 0, 0, 0, 0, 0); }
void syn_earcon(unsigned type, unsigned param) { if (type < EAR_COUNT) post(C_EAR, (uint8_t)type, (uint8_t)param, 0, 0, 0); }
int syn_playing(void) { return seq.on || q_head != q_tail; }
int syn_note_index(void) { return seq.on ? seq.playing_note : -1; }
int syn_earcon_busy(void) { return ear_type >= 0 || q_head != q_tail; }
