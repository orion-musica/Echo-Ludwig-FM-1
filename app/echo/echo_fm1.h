/* ECHO LUDWIG - FM-1 adapter entry points. Set ECHO_LUDWIG to 0 to
 * build the plain 3310 firmware from the same tree. */
#pragma once
#ifndef ECHO_LUDWIG
#define ECHO_LUDWIG 1
#endif
void echo_fm1_init(void);
void echo_fm1_control(void);
