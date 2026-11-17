#!/usr/bin/env python3
"""Exercise production RMT sends and RAW worker entry points without a board."""
import os
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
tests = root / 'tests/native'
source = (root / 'src/Drivers/RadioHelper.cpp').read_text()
def method(name):
    start = source.index(name)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        if source[end] == '{': depth += 1
        elif source[end] == '}': depth -= 1
        end += 1
    return source[start:end]
with tempfile.TemporaryDirectory(prefix='mynova-tx-tests-') as directory:
    temp = Path(directory)
    flags = [os.environ.get('CXX','clang++'), '-std=c++17', '-pthread',
             '-Wno-macro-redefined', '-DLOG_COMPILE_MAX=0', '-DRF_RAW_TX_USE_RMT=1',
             '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
             '-I',str(tests/'radio_stubs'), '-I',str(root/'src/Lib'),
             '-I',str(root/'src/Util'), '-I',str(temp)]
    binary = temp / 'radio_transmit_test'
    subprocess.run(flags + [str(root/'src/Lib/CC1101Driver.cpp'), str(root/'src/Lib/RawGpioStream.cpp'),
                           str(tests/'radio_transmit_test.cpp'), '-o',str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=15)
    (temp/'raw_transmit_task.inc').write_text('\n'.join(method(name) for name in (
        'bool RadioHelper::StartRawTransmit(', 'void RadioHelper::rawTransmitTask(',
        'bool RadioHelper::sendSubGhzRecord(')))
    binary = temp / 'raw_transmit_task_test'
    subprocess.run(flags + [str(tests/'raw_transmit_task_test.cpp'), '-o',str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=15)

    source = (root / 'src/Pages/Rf/RawRecordPage.cpp').read_text()
    methods = [method('void RawRecordPage::' + name + '(') for name in (
        'update', 'updateButtons', 'sendRecord', 'onButtonBack', 'onButtonEnter',
        'onButtonMenu', 'onButton4', 'onButton6')]
    source = (root / 'src/Pages/Rf/SendDataPage.cpp').read_text()
    methods.append(method('void SendDataPage::update('))
    (temp/'raw_transmit_ui.inc').write_text('\n'.join(methods))
    binary = temp / 'raw_transmit_ui_test'
    subprocess.run(flags + [str(tests/'raw_transmit_ui_test.cpp'), '-o',str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=15)
