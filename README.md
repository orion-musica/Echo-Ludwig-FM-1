# Echo Ludwig

An audio-first echo game for blind players on the **M-VAVE FM-1**, by BULØKE.
Hear a melody, play it back, watch it grow. 25 public-domain classics remixed as chiptune, house,
trance, electro and hard techno. Spoken menus via a tiny built-in speech synth (LUDWIG VOICE),
sound icons and light waves.

[![build](https://github.com/orion-musica/Echo-Ludwig-FM-1/actions/workflows/build.yml/badge.svg)](https://github.com/orion-musica/Echo-Ludwig-FM-1/actions/workflows/build.yml)

[**Play in the browser**](https://orion-musica.github.io/Echo-Ludwig-FM-1/play/) ·
[**Install on the FM-1**](https://orion-musica.github.io/Echo-Ludwig-FM-1/install/) ·
[Player notes](ECHO_README.md)

> **Experimental.** First builds for device testing. Back up your FM-1 projects before installing:
> the game keeps its progress in the place of project slot 15.

## Where things are

| Path | What |
| --- | --- |
| `app/echo/` | the game: logic, speech synth, groove synth, light effects, screen, FM-1 adapter |
| `app/screen.c`, `app/audio_output.c` | two small hooks (`ECHO_LUDWIG` in `app/echo/echo_fm1.h`) |
| `echo/data/` | melodies, vocabulary, phonemes, spoken phrases — plain text |
| `echo/tools/gen_data.py` | turns `echo/data/` into `app/echo/echo_data.h` (run after any data change) |
| `echo/tools/build_wasm.sh` | builds the stand-alone browser canvas `echo/prototype/index.html` |
| `echo/host/` | PC test: a scripted player runs a full record and writes a WAV |
| `docs/` | GitHub Pages: start page, `play/` canvas, `install/` web installer |

The rest of the tree is the FM1 Open SDK (V14) and the firmware base it ships with
(see `README.FM1-OPEN.md`, `THIRD_PARTY.md`).

## Build

Every push to `main` builds the firmware, the web installer and the site in GitHub Actions
(Windows runner, JieLi toolchain 2.5.3). The download is the `Echo-Ludwig` artifact of the run;
with GitHub Pages set to "GitHub Actions" the site and installer go live too.

Locally on Windows: `INSTALL-ECHO.bat`.

## License

GPL-3.0-only with the FM1 Open linking exception (`LICENSE.md`).
