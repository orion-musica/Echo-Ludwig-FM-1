#!/usr/bin/env python3
"""Put the browser builds into the GitHub Pages site (docs/).
  echo/site/landing.template.html + ludwig_voice.wasm -> docs/index.html
  echo/prototype/index.html                          -> docs/play/index.html"""
import base64, json, os, shutil
ECHO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCS = os.path.join(os.path.dirname(ECHO), 'docs')
voice = base64.b64encode(open(os.path.join(ECHO, 'prototype', 'ludwig_voice.wasm'), 'rb').read()).decode()
phrases = {}
for line in open(os.path.join(ECHO, 'data', 'phrases.txt'), encoding='utf-8'):
    if line.startswith('SITE_'):
        k, v = line.strip().split('|', 1)
        phrases[k] = v
intro = [phrases[k] for k in sorted(phrases)]
def hero():
    """The FM-1 drawn as an SVG: black screen with LISTEN, knobs, 14 buttons, 27 keys,
    and a wave of light that runs out from middle C (CSS animation, twice, off for reduced motion)."""
    o = ['<svg class="device" viewBox="0 0 640 300" role="img" aria-labelledby="hero-t">',
         '<title id="hero-t">Drawing of the M-VAVE FM-1 running Echo Ludwig. Its black screen shows LISTEN, and a wave of light runs across the buttons and keys from middle C.</title>',
         '<rect x="12" y="12" width="616" height="276" rx="30" fill="#161616" stroke="#4a4a4a" stroke-width="3"/>',
         '<rect x="44" y="40" width="160" height="112" rx="10" fill="#000" stroke="#3a3a3a" stroke-width="3"/>',
         '<g fill="#fff"><path d="M110 76h7l8-7v24l-8-7h-7z"/></g><g fill="none" stroke="#fff" stroke-width="2.4" stroke-linecap="round"><path d="M130 74q5 7 0 14"/><path d="M135 69q10 12 0 24"/></g>',
         '<text x="124" y="120" text-anchor="middle" fill="#fff" font-family="ui-monospace,Menlo,monospace" font-size="12" font-weight="700" letter-spacing="1.5">LISTEN</text>',
         '<text x="124" y="137" text-anchor="middle" fill="#9a9a9a" font-family="ui-monospace,Menlo,monospace" font-size="8.5" letter-spacing="1">ROUND 1   NOTES 3</text>']
    for i, x in enumerate([250, 310, 370, 430]):
        o.append(f'<circle cx="{x}" cy="72" r="19" fill="#262626" stroke="#555" stroke-width="2"/><path d="M{x} 72l{[-9,6,10,-4][i]} {[-11,-12,4,-13][i]}" stroke="#ddd" stroke-width="3" stroke-linecap="round"/>')
    o.append('<circle cx="560" cy="80" r="30" fill="#262626" stroke="#555" stroke-width="2"/><path d="M560 80l14-20" stroke="#ddd" stroke-width="3.5" stroke-linecap="round"/>')
    keys_x0, white_w, ky, kh = 44, 34.5, 182, 92
    origin_x = keys_x0 + 4 * white_w + white_w / 2          # middle C, the 5th white key from F3
    def delay(x): return abs(x - origin_x) / white_w * 0.07
    for i in range(14):
        x = 230 + i * 27
        o.append(f'<rect x="{x}" y="124" width="20" height="13" rx="3" fill="#2a2a2a" stroke="#555" stroke-width="1.5"/>')
        o.append(f'<rect class="glow" style="animation-delay:{delay(x + 10) + 0.15:.2f}s" x="{x}" y="124" width="20" height="13" rx="3" fill="#fff"/>')
    names = 'F G A B C D E F G A B C D E F G'.split()
    for i in range(16):
        x = keys_x0 + i * white_w
        o.append(f'<rect x="{x:.1f}" y="{ky}" width="{white_w - 2:.1f}" height="{kh}" rx="4" fill="#eee"/>')
        o.append(f'<rect class="glow" style="animation-delay:{delay(x + white_w / 2):.2f}s" x="{x:.1f}" y="{ky}" width="{white_w - 2:.1f}" height="{kh}" rx="4" fill="#ffd400"/>')
    # black keys after F, G, A, C, D (pattern from F)
    for i in range(15):
        if names[i] in ('F', 'G', 'A', 'C', 'D'):
            x = keys_x0 + (i + 1) * white_w - 11
            o.append(f'<rect x="{x:.1f}" y="{ky}" width="20" height="56" rx="3" fill="#111"/>')
            o.append(f'<rect class="glow" style="animation-delay:{delay(x + 10):.2f}s" x="{x:.1f}" y="{ky}" width="20" height="56" rx="3" fill="#b38f00"/>')
    o.append(f'<circle class="still" cx="{origin_x:.1f}" cy="{ky + kh - 14}" r="7" fill="#ffd400" opacity="0"/>')
    o.append('</svg>')
    return ''.join(o)


t = open(os.path.join(ECHO, 'site', 'landing.template.html'), encoding='utf-8').read()
t = t.replace('%%HERO%%', hero()).replace('%%VOICE_WASM%%', voice).replace('%%INTRO%%', json.dumps(intro))
os.makedirs(os.path.join(DOCS, 'play'), exist_ok=True)
open(os.path.join(DOCS, 'index.html'), 'w', encoding='utf-8').write(t)
shutil.copyfile(os.path.join(ECHO, 'prototype', 'index.html'), os.path.join(DOCS, 'play', 'index.html'))
shots = os.path.join(DOCS, 'assets', 'screens')
os.makedirs(shots, exist_ok=True)
for name in os.listdir(os.path.join(ECHO, 'site', 'screens')):
    if name.endswith('.png'):
        shutil.copyfile(os.path.join(ECHO, 'site', 'screens', name), os.path.join(shots, name))
print('docs/index.html and docs/play/index.html written')
