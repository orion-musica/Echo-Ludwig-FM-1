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
t = open(os.path.join(ECHO, 'site', 'landing.template.html'), encoding='utf-8').read()
t = t.replace('%%VOICE_WASM%%', voice).replace('%%INTRO%%', json.dumps(intro))
os.makedirs(os.path.join(DOCS, 'play'), exist_ok=True)
open(os.path.join(DOCS, 'index.html'), 'w', encoding='utf-8').write(t)
shutil.copyfile(os.path.join(ECHO, 'prototype', 'index.html'), os.path.join(DOCS, 'play', 'index.html'))
print('docs/index.html and docs/play/index.html written')
