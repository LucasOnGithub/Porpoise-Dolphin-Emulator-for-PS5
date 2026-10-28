#!/usr/bin/env python3
"""Prepare the matched Rust backend's isolated PS5 worktree."""
# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location('slippi_inputs', ROOT / 'tools/prepare-slippi-core.py')
inputs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(inputs)


def main():
    rust = ROOT / '.deps/slippi-ps5-rust'
    inputs.checkout(rust, inputs.PINS['rust'])
    patch = ROOT / 'patches/slippi/ps5-rust.patch'
    diff = inputs.git(rust, 'diff', '--binary', 'HEAD', capture=True)
    if not diff:
        if inputs.git(rust, 'ls-files', '--others', '--exclude-standard', capture=True).strip():
            raise SystemExit('Untracked Rust files would be overwritten')
        inputs.git(rust, 'apply', '--intent-to-add', str(patch))
    elif diff != patch.read_text():
        raise SystemExit('PS5 Rust worktree differs from its tracked patch; refusing to reset it')
    print(rust)


if __name__ == '__main__':
    main()
