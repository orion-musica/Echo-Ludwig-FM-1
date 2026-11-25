/* ECHO LUDWIG - small groove synth: one lead voice, bass, three drums,
 * an earcon (sound-icon) voice and a tiny step sequencer. Everything sounds
 * from the audio interrupt; the control task only posts commands.
 *   syn_*                -> control task
 *   echo_audio_render    -> audio interrupt (adds to the FM-1 output block) */
#pragma once
#include "echo_port.h"

enum { STYLE_CHIP, STYLE_HOUSE, STYLE_TRANCE, STYLE_ELECTRO, STYLE_HARD, STYLE_COUNT };
enum { SEQ_LEAD = 1, SEQ_HATS = 2, SEQ_FULL = 4, SEQ_LOOP = 8 };
enum {
    EAR_TICK, EAR_WRAP, EAR_ENTER, EAR_BACK, EAR_LOCKED, EAR_ERROR, EAR_YOUR_TURN,
    EAR_ROUND_OK, EAR_WRONG, EAR_LIFE, EAR_UNLOCK, EAR_GAME_OVER, EAR_STAR, EAR_COMBO,
    EAR_HIGHER, EAR_LOWER, EAR_COUNT
};

void syn_init(void);
void syn_style(unsigned style);
void syn_levels(float music, float voice, int pitch_pan);
void syn_note_on(unsigned midi);
void syn_note_off(void);
/* notes: pairs (midi, ticks), 12 ticks per quarter. root: 0..11 for the bass. */
void syn_play(const uint8_t *notes, unsigned count, unsigned bpm, unsigned root, unsigned flags, unsigned loops);
void syn_stop(void);
void syn_earcon(unsigned type, unsigned param);
int syn_playing(void);          /* 1 while a sequence plays */
int syn_note_index(void);       /* melody note now sounding, -1 if none */
int syn_earcon_busy(void);

FM1_AUDIO_CODE void echo_audio_render(int32_t *stereo_q23, unsigned frames);
