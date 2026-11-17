#!/usr/bin/env python3
"""Render real firmware drawing functions/fonts on the host, with mocked capture snapshots."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
from PIL import Image, ImageDraw

root = Path(__file__).resolve().parents[2]
tests = root / 'tests/native'
clib = Path(os.environ.get('U8G2_CLIB', Path.home() / 'Documents/Arduino/libraries/U8g2/src/clib'))
out = root / 'verification/raw-record'
out.mkdir(parents=True, exist_ok=True)

def function(source, klass, method):
    match = re.search(r'^.*\b' + klass + r'::' + method + r'\(', source, re.M)
    if not match:
        raise ValueError(f'Missing production function: {klass}::{method}')
    begin = source.index('{', match.start())
    depth = 1
    end = begin + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end] + '\n'

with tempfile.TemporaryDirectory(prefix='mynova-raw-preview-') as temp:
    temp = Path(temp)
    config_source = (root / 'src/Pages/Rf/RawConfigPage.cpp').read_text()
    draws = [re.search(r'static const int16_t RAW_THRESHOLDS\[\] = \{.*?\};', config_source, re.S).group(0)]
    for relative, klass, methods in [
        ('src/GUI/Widget/UINavBar.cpp', 'UINavBar', ['UINavBar', 'render']),
        ('src/Pages/Rf/RawConfigPage.cpp', 'RawConfigPage', ['freqText','presetText','thresholdText','render']),
        ('src/Pages/Rf/RawRecordPage.cpp', 'RawRecordPage', ['topInfo','updateButtons','drawGraph','render']),
        ('src/Lib/RawCaptureSession.cpp', 'RawCaptureSession', ['rssiToBar']),
    ]:
        source = (root / relative).read_text()
        draws.extend(function(source, klass, method) for method in methods)
    (temp / 'drawing_functions.inc').write_text('\n'.join(draws))
    fonts = (clib / 'u8g2_fonts.c').read_text(encoding='latin1')
    selected = ['#include "u8g2.h"\n']
    for name in ['u8g2_font_5x8_tf', 'u8g2_font_wqy12_t_gb2312']:
        start = fonts.index('const uint8_t ' + name + '[')
        end = start
        quote = escape = False
        while True:
            ch = fonts[end]
            if quote:
                if escape: escape = False
                elif ch == '\\': escape = True
                elif ch == '"': quote = False
            elif ch == '"': quote = True
            elif ch == ';': break
            end += 1
        selected.append(fonts[start:end+1] + '\n')
    (temp / 'fonts.c').write_text(''.join(selected), encoding='latin1')
    names = ['u8g2_bitmap','u8g2_box','u8g2_buffer','u8g2_circle','u8g2_d_memory',
             'u8g2_d_setup','u8g2_font','u8g2_hvline','u8g2_intersection','u8g2_line',
             'u8g2_ll_hvline','u8g2_setup','u8x8_8x8','u8x8_byte','u8x8_cad',
             'u8x8_display','u8x8_gpio','u8x8_setup','u8x8_string',
             'u8x8_d_ssd1306_128x64_noname']
    objects = []
    for source in [clib / (n+'.c') for n in names] + [temp / 'fonts.c']:
        obj = temp / (source.stem + '.o')
        subprocess.run(['clang','-O1','-ffunction-sections','-fdata-sections',
                        '-I',str(clib),'-c',str(source),'-o',str(obj)], check=True)
        objects.append(str(obj))
    binary = temp / 'preview'
    subprocess.run(['clang++','-std=c++17','-Wl,-dead_strip','-I',str(temp),
                    '-I',str(clib),'-I',str(tests/'stubs'),'-I',str(root/'src/Lib'),
                    str(tests/'raw_preview.cpp'),*objects,'-o',str(binary)],check=True)
    subprocess.run([str(binary),str(out)],check=True)

scenes = [('config_0','Frequency'),('config_1','Modulation'),('config_2','Threshold'),
          ('waiting','Waiting'),('recording','Recording'),('stopped','Stopped'),
          ('full','Full'),('error','Storage error')]
canvas = Image.new('RGB',(1120,652),'#111827')
draw = ImageDraw.Draw(canvas)
for i,(name,label) in enumerate(scenes):
    x,y = 28+(i%4)*276,28+(i//4)*314
    draw.text((x,y),label,fill='#dbeafe')
    img = Image.open(out/(name+'.pgm')).convert('RGB')
    img.save(out/(name+'.png'))
    canvas.paste(img.resize((256,128),Image.Resampling.NEAREST),(x,y+26))
    canvas.paste(img,(x,y+170))
    (out/(name+'.pgm')).unlink()
canvas.save(out/'raw-ui-preview.png')
print(out/'raw-ui-preview.png')
