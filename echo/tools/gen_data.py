#!/usr/bin/env python3
"""Generate the shared data tables for ECHO LUDWIG.

One source (data/*.txt) -> two outputs, so the firmware and the browser
canvas always speak the same words and play the same melodies:
  app/echo/echo_data.h          (C tables, used by the firmware)
  echo/prototype/echo_data.js   (JavaScript object)
"""
import json, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))  # echo/
REPO = os.path.dirname(ROOT)
NOTE = {'C': 0, 'C#': 1, 'D': 2, 'D#': 3, 'E': 4, 'F': 5, 'F#': 6, 'G': 7,
        'G#': 8, 'A': 9, 'A#': 10, 'B': 11}
LOW, HIGH = 53, 79  # F3..G5, the FM-1 keybed
TYPES = 'VDSNFHPBCJX'


def rows(name):
    with open(os.path.join(ROOT, 'data', name), encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith('#'):
                yield line


def midi(tok):
    m = re.fullmatch(r'([A-G]#?)(\d)', tok)
    if not m:
        sys.exit('bad note ' + tok)
    return 12 * (int(m.group(2)) + 1) + NOTE[m.group(1)]


ICONS = {  # 11x11, order must match IC_* in echo_game.h (IC_NONE is blank)
'NONE': [''] * 11,
'NOTE': ['    #######', '    #######', '    #     #', '    #     #', '    #     #', '    #     #',
         '    #     #', ' ####  ####', '##### #####', '##### #####', ' ###   ### '],
'EAR':  ['     #     ', '    ##   # ', '   ###  #  ', '#####  #  #', '#####  # # ',
         '#####  # # ', '#####  # # ', '#####  #  #', '   ###  #  ', '    ##   # ', '     #     '],
'KEYS': ['###########', '# # # # # #', '# # # # # #', '# # # # # #', '# # # # # #', '# # # # # #',
         '#  #  #  # ', '#  #  #  # ', '#  #  #  # ', '#  #  #  # ', '###########'],
'CHECK':['           ', '          #', '         ##', '        ## ', '#      ##  ', '##    ##   ',
         ' ##  ##    ', '  ####     ', '   ##      ', '           ', '           '],
'CROSS':['           ', ' #       # ', '  #     #  ', '   #   #   ', '    # #    ', '     #     ',
         '    # #    ', '   #   #   ', '  #     #  ', ' #       # ', '           '],
'STAR': ['     #     ', '     #     ', '    ###    ', '    ###    ', '###########', ' ######### ',
         '  #######  ', '  #######  ', ' ###   ### ', ' ##     ## ', ' #       # '],
'HEART':['           ', ' ###   ### ', '##### #####', '###########', '###########', ' ######### ',
         '  #######  ', '   #####   ', '    ###    ', '     #     ', '           '],
'LOCK': ['   #####   ', '  ##   ##  ', '  #     #  ', '  #     #  ', '###########', '###########',
         '#####  ####', '####    ###', '#####  ####', '#####  ####', '###########'],
'GEAR': ['    ###    ', ' #  ###  # ', ' ######### ', '  ##   ##  ', '###     ###', '###     ###',
         '###     ###', '  ##   ##  ', ' ######### ', ' #  ###  # ', '    ###    '],
'HELP': ['   #####   ', '  ##   ##  ', '  ##   ##  ', '      ##   ', '     ##    ', '    ##     ',
         '    ##     ', '           ', '    ##     ', '    ##     ', '           '],
'DISC': ['   #####   ', ' ##     ## ', ' #       # ', '#    #    #', '#   # #   #', '#  #   #  #',
         '#   # #   #', '#    #    #', ' #       # ', ' ##     ## ', '   #####   '],
'CRATE':['###########', '#         #', '###########', '# #  #  # #', '# #  #  # #', '# #  #  # #',
         '# #  #  # #', '# #  #  # #', '# #  #  # #', '#         #', '###########'],
'UP':   ['     #     ', '    ###    ', '   #####   ', '  #######  ', ' ######### ', '###########',
         '    ###    ', '    ###    ', '    ###    ', '    ###    ', '    ###    '],
'DOWN': ['    ###    ', '    ###    ', '    ###    ', '    ###    ', '    ###    ', '###########',
         ' ######### ', '  #######  ', '   #####   ', '    ###    ', '     #     '],
'PLAY': ['##         ', '####       ', '######     ', '########   ', '########## ', '###########',
         '########## ', '########   ', '######     ', '####       ', '##         '],
'BOLT': ['      #### ', '     ####  ', '    ####   ', '   ####    ', '  ######## ', ' ########  ',
         '     ###   ', '    ###    ', '   ###     ', '  ##       ', ' #         '],
}


def make_font():
    from PIL import Image, ImageDraw, ImageFont
    f = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf', 11)
    glyphs = []
    for code in range(32, 96):
        img = Image.new('L', (8, 14), 0)
        ImageDraw.Draw(img).text((0, 0), chr(code), fill=255, font=f)
        rows_ = []
        for y in range(1, 12):
            b = 0
            for x in range(7):
                if img.getpixel((x, y)) > 110:
                    b |= 1 << (6 - x)
            rows_.append(b)
        glyphs.append(rows_)
    icons = []
    for name, art in ICONS.items():
        g = []
        for y in range(11):
            row = (art[y] if art[y] else '').ljust(11)
            g.append(sum(1 << (10 - x) for x, ch in enumerate(row[:11]) if ch == '#'))
        icons.append(g)
    return glyphs, icons


phonemes = []
for line in rows('phonemes.txt'):
    p = line.split()
    phonemes.append({'name': p[0], 'type': TYPES.index(p[1]),
                     'v': [int(x) for x in p[2:]]})
pindex = {p['name']: i for i, p in enumerate(phonemes)}

lexicon = {}
for line in rows('lexicon.txt'):
    word, *phs = line.split()
    code = []
    for ph in phs:
        m = re.fullmatch(r'([A-Z_]+)(\d?)', ph)
        name, stress = m.group(1), int(m.group(2) or 0)
        if name not in pindex:
            sys.exit(f'unknown phoneme {ph} in {word}')
        code.append(pindex[name] | (stress << 6))
    if word in lexicon:
        sys.exit('duplicate word ' + word)
    lexicon[word] = code

crates, melodies = [], []
for line in rows('melodies.txt'):
    f = line.split('|')
    if f[0] == 'CRATE':
        crates.append({'index': int(f[1]), 'title': f[2], 'say': f[3],
                       'bpm': int(f[4]), 'style': f[5]})
    elif f[0] == 'MEL':
        notes = []
        for tok in f[10].split():
            n, t = tok.split(':')
            m = midi(n)
            if not LOW <= m <= HIGH:
                sys.exit(f'{f[1]}: {n} outside keybed F3..G5')
            notes.append([m, int(t)])
        melodies.append({'id': f[1], 'crate': int(f[2]), 'title': f[3], 'say': f[4],
                         'composer': f[5], 'saycomposer': f[6], 'year': int(f[7]),
                         'root': NOTE[f[8]], 'reveal': int(f[9]), 'notes': notes})


def check_say(text, where):
    for w in text.split():
        if re.fullmatch(r'\d+', w) or re.fullmatch(r'[A-GI]', w):
            continue
        if w.lower() not in lexicon:
            sys.exit(f'{where}: word "{w}" missing from lexicon')


phrases = []
for line in rows('phrases.txt'):
    pid, text = line.split('|', 1)
    check_say(re.sub(r'[,.!?;]', ' ', text), 'phrase ' + pid)
    phrases.append((pid, text))

for c in crates:
    check_say(c['say'], 'crate')
for m in melodies:
    check_say(m['say'], m['id'])
    check_say(m['saycomposer'], m['id'])
    if m['year'] >= 1929:
        sys.exit(m['id'] + ': not public domain by date')

# ---------- C ----------
out = []
o = out.append
o('/* GENERATED by tools/gen_data.py from the data folder - do not edit by hand. */')
o('#pragma once\n#include <stdint.h>')
o(f'enum {{ ECHO_PHONEMES = {len(phonemes)}, ECHO_MELODIES = {len(melodies)}, ECHO_CRATES = {len(crates)} }};')
o('enum { PH_V, PH_D, PH_S, PH_N, PH_F, PH_H, PH_P, PH_B, PH_C, PH_J, PH_X };')
o('typedef struct { char name[3]; uint8_t type; uint16_t f1, f2, f3, b1, b2, b3;'
  ' uint8_t av, ah, af; uint16_t fc, fb; uint8_t dur; uint16_t f1b, f2b, f3b; } echo_phoneme;')
o('static const echo_phoneme echo_phoneme_table[ECHO_PHONEMES] = {')
for p in phonemes:
    v = p['v']
    o('    {"%s",%d,%s},' % (p['name'], p['type'], ','.join(str(x) for x in v)))
o('};')
o(f'enum {{ ECHO_PAUSE_PHONEME = {pindex["_"]} }};')
blob = bytearray()
for w in sorted(lexicon):
    blob += w.encode() + b'\0' + bytes([len(lexicon[w])]) + bytes(lexicon[w])
blob += b'\0'
o(f'/* {len(lexicon)} words, {len(blob)} bytes: word NUL count phoneme... (phoneme | stress<<6) */')
o('static const uint8_t echo_lexicon[%d] = {' % len(blob))
for i in range(0, len(blob), 20):
    o('    ' + ','.join(str(b) for b in blob[i:i + 20]) + ',')
o('};')
o('typedef struct { const char *title, *say; uint8_t bpm; const char *style; } echo_crate_info;')
o('static const echo_crate_info echo_crates[ECHO_CRATES] = {')
for c in crates:
    o(f'    {{"{c["title"]}", "{c["say"]}", {c["bpm"]}, "{c["style"]}"}},')
o('};')
o('typedef struct { const char *id, *title, *say, *composer, *saycomposer; uint16_t year;'
  ' uint8_t crate, root, reveal, count; const uint8_t *notes; } echo_melody;')
for i, m in enumerate(melodies):
    flat = ','.join(f'{n},{t}' for n, t in m['notes'])
    o(f'static const uint8_t echo_notes_{i}[] = {{{flat}}};')
o('static const echo_melody echo_melodies[ECHO_MELODIES] = {')
for i, m in enumerate(melodies):
    o(f'    {{"{m["id"]}", "{m["title"]}", "{m["say"]}", "{m["composer"]}", "{m["saycomposer"]}", '
      f'{m["year"]}, {m["crate"] - 1}, {m["root"]}, {m["reveal"]}, {len(m["notes"])}, echo_notes_{i}}},')
o('};')
o('enum {')
for pid, _ in phrases:
    o(f'    P_{pid},')
o('    P_COUNT };')
o('static const char *const echo_phrases[P_COUNT] = {')
for pid, text in phrases:
    o(f'    "{text}",')
o('};')
font, icons = make_font()
o(f'/* 7x11 pixel font for ASCII 32..95 (bit 6 = leftmost pixel), then {len(icons)} 11x11 icons (bit 10 = left). */')
o('static const uint8_t echo_font[64][11] = {')
for g in font:
    o('    {' + ','.join(str(b) for b in g) + '},')
o('};')
o('static const uint16_t echo_icons[%d][11] = {' % len(icons))
for g in icons:
    o('    {' + ','.join(str(b) for b in g) + '},')
o('};')
cpath = os.path.join(REPO, 'app', 'echo', 'echo_data.h')
os.makedirs(os.path.dirname(cpath), exist_ok=True)
with open(cpath, 'w', encoding='utf-8') as f:
    f.write('\n'.join(out) + '\n')

# ---------- JS ----------
font_, icons_ = make_font()
js = {'phonemes': phonemes, 'lexicon': lexicon, 'crates': crates, 'melodies': melodies,
      'phrases': dict(phrases), 'font': font_, 'icons': icons_}
jpath = os.path.join(ROOT, 'prototype', 'echo_data.js')
os.makedirs(os.path.dirname(jpath), exist_ok=True)
with open(jpath, 'w', encoding='utf-8') as f:
    f.write('/* GENERATED by tools/gen_data.py - do not edit by hand. */\n')
    f.write('const ECHO_DATA = ' + json.dumps(js, separators=(',', ':')) + ';\n')
print(f'{len(phonemes)} phonemes, {len(lexicon)} words ({len(blob)} B), '
      f'{len(crates)} crates, {len(melodies)} melodies')
