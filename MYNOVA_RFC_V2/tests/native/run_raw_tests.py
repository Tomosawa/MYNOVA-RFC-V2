#!/usr/bin/env python3
"""Compile production RAW code against host GPIO/RTOS/FS substitutes; no board required."""
import os
import re
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
lib = root / 'src/Lib'
tests = root / 'tests/native'
# A numeric transcription must never write MDMCFG0 values into FREQ1 again.
registers = {name: int(value, 16) for name, value in re.findall(
    r'#define (CC1101_\w+)\s+(0x[0-9A-Fa-f]+)', (lib / 'CC1101Driver.h').read_text())}
presets = (lib / 'RFSubGhzPresets.h').read_text()
for name, body in re.findall(r'static const uint8_t (RF_PRESET_REGS_\w+)\[\] = \{(.*?)\};', presets, re.S):
    body = re.sub(r'//[^\n]*', '', body)
    values = [int(v, 16) for v in re.findall(r'0x[0-9A-Fa-f]+', body)]
    pairs = {}
    for addr, value in zip(values[::2], values[1::2]):
        if addr == value == 0:
            break
        pairs[addr] = value
    assert pairs[registers['CC1101_MDMCFG0']] == 0, name
    assert all(registers[f'CC1101_FREQ{n}'] not in pairs for n in range(3)), name
print('PASS: all four preset register addresses', flush=True)

with tempfile.TemporaryDirectory(prefix='mynova-raw-tests-') as temp:
    temp = Path(temp)
    flags = [os.environ.get('CXX', 'clang++'), '-std=c++17', '-pthread',
             '-Wno-macro-redefined', '-DLOG_COMPILE_MAX=0',
             '-I', str(tests / 'stubs'), '-I', str(lib), '-I', str(root / 'src/Util')]
    decoder = lib / 'RFSignalDecoder.cpp'
    for name in ('decoder', 'session'):
        sources = [str(decoder)]
        if name == 'session':
            # Only platform includes change; all session methods remain production source.
            source = (lib / 'RawCaptureSession.cpp').read_text()
            source = source.replace('#include "../Util/Log.h"', '#define LOG_I(...)\n#define LOG_E(...)\n#define LOG_W(...)')
            source = source.replace('#include "../Util/TaskConfig.h"', '#include "TaskConfig.h"')
            source = source.replace('#include "../Drivers/RadioHelper.h"', '#include "RadioHelperStub.h"')
            copied = temp / 'RawCaptureSession.cpp'
            copied.write_text(source)
            sources.append(str(copied))
        binary = temp / f'raw_{name}_test'
        subprocess.run(flags + sources + [str(tests / f'raw_{name}_test.cpp'), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=15)
