#!/usr/bin/env python3
"""Run real core memory/EXI/Rust/loopback tests without a game or account."""
# SPDX-License-Identifier: GPL-3.0-or-later
import ctypes
from pathlib import Path
import sys
import tempfile

if len(sys.argv) != 2:
    raise SystemExit('usage: test-slippi-core.py dolphin_libretro.so')
with tempfile.TemporaryDirectory(prefix='porpoise-slippi-core-') as directory:
    core = ctypes.CDLL(str(Path(sys.argv[1]).resolve()))
    run = core.porpoise_slippi_core_tests
    run.argtypes = [ctypes.c_char_p]
    run.restype = ctypes.c_int
    raise SystemExit(run(directory.encode()))
