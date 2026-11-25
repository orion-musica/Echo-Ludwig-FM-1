#!/bin/sh
# Build the browser canvas engine (prototype/echo.wasm) from the firmware sources.
set -e
cd "$(dirname "$0")/.."
python3 tools/gen_data.py
E=../app/echo
clang --target=wasm32 -O2 -ffreestanding -nostdlib -fno-builtin-memset -DFM1_HOST_TEST -DECHO_HOST_DEBUG -DECHO_WASM \
  -Iprototype/wasm -I$E -Wall -Wno-unused-function \
  -Wl,--no-entry -Wl,--allow-undefined -Wl,--export-memory -Wl,--strip-all \
  prototype/wasm/echo_wasm.c $E/echo_game.c $E/echo_speech.c $E/echo_synth.c $E/echo_screen.c $E/echo_fx.c \
  -o prototype/echo.wasm
ls -l prototype/echo.wasm
python3 - <<'PY'
import base64
t = open('prototype/canvas.template.html', encoding='utf-8').read()
w = base64.b64encode(open('prototype/echo.wasm', 'rb').read()).decode()
open('prototype/index.html', 'w', encoding='utf-8').write(t.replace('%%WASM%%', w))
print('prototype/index.html written')
PY
# LUDWIG VOICE alone, for the website
clang --target=wasm32 -O2 -ffreestanding -nostdlib -fno-builtin-memset -DFM1_HOST_TEST \
  -Iprototype/wasm -I$E -Wall -Wno-unused-function \
  -Wl,--no-entry -Wl,--allow-undefined -Wl,--export-memory -Wl,--strip-all \
  prototype/wasm/voice_wasm.c $E/echo_speech.c -o prototype/ludwig_voice.wasm
ls -l prototype/ludwig_voice.wasm
python3 tools/build_site.py
