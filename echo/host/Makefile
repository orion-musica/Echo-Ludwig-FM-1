# Host build of the real game code (Linux/macOS, gcc or clang).
CC ?= cc
SRC = ../../app/echo/echo_game.c ../../app/echo/echo_speech.c ../../app/echo/echo_synth.c ../../app/echo/echo_screen.c ../../app/echo/echo_fx.c echo_host.c
echo_host: $(SRC) ../../app/echo/*.h
	$(CC) -O2 -Wall -Wextra -DFM1_HOST_TEST -DECHO_HOST_DEBUG -I../../app/echo $(SRC) -o $@
clean:
	rm -f echo_host echo_save.bin *.ppm *.wav
