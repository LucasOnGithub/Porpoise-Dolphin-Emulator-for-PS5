#!/usr/bin/env python3
"""Prepare exact matched Slippi inputs without resetting existing worktrees."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parent.parent
PINS = json.loads((ROOT / 'tooling/slippi-core/pins.json').read_text())

def git(path, *args, capture=False):
    return subprocess.run(['git', '-C', str(path), *args], check=True, text=True,
                          stdout=subprocess.PIPE if capture else None).stdout

def checkout(path, pin):
    if not path.exists():
        path.mkdir(parents=True)
        git(path, 'init', '--quiet')
        git(path, 'remote', 'add', 'origin', pin['url'])
        git(path, 'fetch', '--depth=1', 'origin', pin['revision'])
        git(path, 'checkout', '--detach', pin['revision'])
    if git(path, 'rev-parse', 'HEAD', capture=True).strip() != pin['revision']:
        raise SystemExit(f'Unexpected revision at {path}; refusing to reset it')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', type=Path, default=ROOT / '.deps/slippi-core-reference')
    parser.add_argument('--rust', type=Path, default=ROOT / '.deps/slippi-core-rust')
    parser.add_argument('--dolphin', type=Path)
    args = parser.parse_args()
    args.reference = args.reference.resolve()
    args.rust = args.rust.resolve()
    checkout(args.reference, PINS['reference'])
    checkout(args.rust, PINS['rust'])
    if PINS['rust']['revision'] not in git(args.reference, 'ls-tree', 'HEAD', 'Externals/SlippiRustExtensions', capture=True):
        raise SystemExit('C++/Rust submodule pins do not match')
    if git(args.reference, 'status', '--porcelain', '--untracked-files=no', capture=True).strip():
        raise SystemExit('Reference source has local edits')
    manifest = args.rust / 'ffi/Cargo.toml'
    original = git(args.rust, 'show', 'HEAD:ffi/Cargo.toml', capture=True)
    static = original.replace('crate-type = ["cdylib"]', 'crate-type = ["staticlib"]')
    if original == static:
        raise SystemExit('Unexpected Rust FFI crate type')
    dirty = git(args.rust, 'diff', '--name-only', 'HEAD', capture=True).splitlines()
    if any(name != 'ffi/Cargo.toml' for name in dirty) or manifest.read_text() not in (original, static):
        raise SystemExit('Rust source has unexpected edits')
    if manifest.read_text() != static:
        manifest.write_text(static)
    if args.dolphin:
        dolphin = args.dolphin.resolve()
        checkout(dolphin, PINS['dolphin'])
        patch = ROOT / 'patches/dolphin/ps5-port.patch'
        diff = git(dolphin, 'diff', '--binary', 'HEAD', capture=True)
        if not diff:
            if git(dolphin, 'ls-files', '--others', '--exclude-standard', capture=True).strip():
                raise SystemExit('Untracked Dolphin files would be overwritten')
            git(dolphin, 'apply', '--intent-to-add', str(patch))
        elif diff != patch.read_text():
            raise SystemExit('Dolphin worktree differs from the tracked patch; refusing to reset it')
        git(dolphin, 'submodule', 'update', '--init', '--recursive', '--jobs', '4', '--',
            'Externals/bzip2', 'Externals/cpp-ipc', 'Externals/cpp-optparse', 'Externals/curl',
            'Externals/enet', 'Externals/fmt', 'Externals/glslang', 'Externals/hidapi',
            'Externals/imgui', 'Externals/implot', 'Externals/libspng', 'Externals/libusb',
            'Externals/lz4', 'Externals/minizip-ng', 'Externals/pugixml', 'Externals/SFML',
            'Externals/tinygltf', 'Externals/watcher', 'Externals/xxhash', 'Externals/zlib-ng', 'Externals/zstd')
    print('Matched C++/Rust Slippi sources prepared; experimental Linux host integration only.')

if __name__ == '__main__':
    main()
