#!/usr/bin/env python3
"""Run production driver, scanner, decoder and RX configuration against a CC1101 SPI model."""
import os
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[2]
lib=root/'src/Lib'
tests=root/'tests/native'
source=(root/'src/Drivers/RadioHelper.cpp').read_text()
def method(name):
    start=source.index(name)
    end=source.index('{',start)+1
    depth=1
    while depth:
        if source[end]=='{': depth+=1
        elif source[end]=='}': depth-=1
        end+=1
    return source[start:end]
with tempfile.TemporaryDirectory(prefix='mynova-radio-tests-') as directory:
    temp=Path(directory)
    (temp/'receive_configuration.inc').write_text('\n'.join(method(name) for name in (
        'bool RadioHelper::configureReceive(', 'bool RadioHelper::beginOokReceive(',
        'void RadioHelper::applyRxSensitivity(',
        'void RadioHelper::setAntennaBand(')))
    binary=temp/'radio_receive_test'
    flags=[os.environ.get('CXX','clang++'),'-std=c++17','-pthread','-Wno-macro-redefined',
           '-DLOG_COMPILE_MAX=0','-DLOG_I(...)=','-DLOG_E(...)=','-DLOG_W(...)=',
           '-I',str(tests/'radio_stubs'),'-I',str(lib),'-I',str(root/'src/Bsp'),'-I',str(temp)]
    subprocess.run(flags+[str(lib/name) for name in ('CC1101Driver.cpp','RawGpioStream.cpp','RFSignalDecoder.cpp','FrequencyScanner.cpp')]+
                   [str(tests/'radio_receive_test.cpp'),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True,timeout=15)
    logBinary=temp/'rf_diagnostics_test'
    logFlags=[os.environ.get('CXX','clang++'),'-std=c++17','-pthread','-Wno-macro-redefined',
              '-DLOG_COMPILE_MAX=4','-I',str(tests/'radio_stubs'),'-I',str(lib)]
    subprocess.run(logFlags+[str(lib/name) for name in ('CC1101Driver.cpp','RawGpioStream.cpp','RFSignalDecoder.cpp','FrequencyScanner.cpp')]+
                   [str(tests/'rf_diagnostics_test.cpp'),'-o',str(logBinary)],check=True)
    subprocess.run([str(logBinary)],check=True,timeout=15)
    ookBinary=temp/'ook_frame_test'
    subprocess.run(flags+[str(lib/'OokFrameDecoder.cpp'),str(tests/'ook_frame_test.cpp'),
                          '-o',str(ookBinary)],check=True)
    subprocess.run([str(ookBinary)],check=True,timeout=15)
