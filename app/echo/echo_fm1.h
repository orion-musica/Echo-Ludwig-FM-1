/* ECHO LUDWIG - FM-1 adapter entry points. Set ECHO_LUDWIG to 0 to
 * build the plain 3310 firmware from the same tree. */
#pragma once
#ifndef ECHO_LUDWIG
#define ECHO_LUDWIG 1
#endif
#include <stdint.h>
/* 1 = run Echo Ludwig. A runtime flag (not a compile-time return) keeps the
 * base firmware's code linked: the SDK build requires its runtime symbols. */
extern volatile uint8_t echo_ludwig_active;
void echo_fm1_init(void);
void echo_fm1_control(void);
