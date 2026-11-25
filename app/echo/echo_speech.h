/* LUDWIG VOICE - a tiny English formant speech synthesizer (about 6 KB code
 * + 3 KB vocabulary). Text is a sentence of lexicon words, digits (spoken as
 * numbers), single capitals A-G / I (letter names) and , . ! for pauses.
 *
 * Threading on the FM-1:
 *   sp_say / sp_queue / sp_stop / sp_pump  -> control task only
 *   sp_render_block                         -> audio interrupt only
 * sp_pump synthesizes at 22.05 kHz into a ring buffer; the interrupt reads
 * it and doubles the rate to 44.1 kHz. */
#pragma once
#include "echo_port.h"

enum { SP_VOICE_LUDWIG, SP_VOICE_CLARA };
enum { SP_SLOW, SP_NORMAL, SP_FAST, SP_VERY_FAST };

void sp_init(void);
void sp_set_voice(unsigned voice, unsigned speed);
void sp_say(const char *text);   /* interrupt current speech and say text */
void sp_queue(const char *text); /* append after current speech */
void sp_stop(void);
int sp_busy(void);               /* 1 while anything is still to be heard */
void sp_pump(void);              /* call often from the control loop */
/* Adds speech (mono, about -1..1) into a float block of `frames` samples. */
FM1_AUDIO_CODE void sp_render_block(float *mono, unsigned frames, float gain);

/* Host/test helper: synthesize the whole text into pcm (22.05 kHz). */
unsigned sp_render_offline(const char *text, int16_t *pcm, unsigned max_samples);
