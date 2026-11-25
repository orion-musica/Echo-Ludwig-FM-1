#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "echo_screen.h"
#include "echo_game.h"
static unsigned I; static const char *A, *B;
void echo_game_message(unsigned *icon, const char **a, const char **b, unsigned *serial){*icon=I;*a=A;*b=B;*serial=1;}
int fx_flash(void){return 0;}
int main(int c,char**v){I=atoi(v[1]);A=v[2];B=v[3];static uint16_t band[240*16];FILE*f=fopen(v[4],"wb");fprintf(f,"P6 240 240 255\n");
for(unsigned y=0;y<240;y+=16){echo_screen_band(y,16,band);for(int i=0;i<240*16;i++){uint16_t p=band[i];unsigned char rgb[3]={(p>>11)<<3,((p>>5)&63)<<2,(p&31)<<3};fwrite(rgb,1,3,f);}}fclose(f);}
