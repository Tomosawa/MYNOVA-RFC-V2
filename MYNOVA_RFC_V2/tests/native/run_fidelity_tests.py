#!/usr/bin/env python3
"""Compile real decoder, file IO and transmitter and compare their full waveform round trip."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[2]
tests=root/'tests/native'
with tempfile.TemporaryDirectory(prefix='mynova-fidelity-') as directory:
    binary=Path(directory)/'raw_fidelity_test'
    command=['clang++','-std=c++17','-pthread','-Wno-macro-redefined','-DLOG_COMPILE_MAX=0', '-DRF_RAW_TX_USE_RMT=1',
             '-fsanitize=address,undefined','-fno-omit-frame-pointer',
             '-I',str(tests/'radio_stubs'),'-I',str(root/'src/Lib')]
    command += [str(root/'src/Lib'/name) for name in ('CC1101Driver.cpp','RawGpioStream.cpp','RFSignalDecoder.cpp','FlipperSignalFile.cpp')]
    subprocess.run(command+[str(tests/'raw_fidelity_test.cpp'),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True,timeout=30)
