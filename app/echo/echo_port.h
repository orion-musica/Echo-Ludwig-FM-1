/* ECHO LUDWIG - portability shims shared by every echo_*.c file.
 * On the FM-1 the SDK header supplies FM1_AUDIO_CODE / FM1_AUDIO_DATA
 * (code and tables that must stay in RAM for the audio interrupt).
 * The host test build defines FM1_HOST_TEST and gets empty macros. */
#pragma once
#include <stdint.h>
#include <stddef.h>
#ifdef FM1_HOST_TEST
#ifndef FM1_AUDIO_CODE
#define FM1_AUDIO_CODE
#define FM1_AUDIO_DATA
#endif
#else
#include "fm1.h"
#endif

/* libm-free helpers: the FM-1 has a single-precision FPU (-mfprev1), but we
 * keep the firmware independent of libm so it links the same everywhere. */
static inline float echo_expn(float x) /* e^-x, 0 <= x <= ~2 */
{
    float t = 1.0f, s = 1.0f;
    for (int i = 1; i < 10; ++i) { t *= -x / (float)i; s += t; }
    return s;
}
static inline float echo_cos(float x) /* 0 <= x <= pi */
{
    const float pi = 3.14159265f;
    float sign = 1.0f;
    if (x > pi * 0.5f) { x = pi - x; sign = -1.0f; }
    float x2 = x * x;
    return sign * (1.0f - x2 / 2.0f * (1.0f - x2 / 12.0f * (1.0f - x2 / 30.0f * (1.0f - x2 / 56.0f))));
}
static inline float echo_sqrt(float x)
{
    if (x <= 0.0f) return 0.0f;
    float r = x > 1.0f ? x * 0.5f : 1.0f;
    for (int i = 0; i < 6; ++i) r = 0.5f * (r + x / r);
    return r;
}
