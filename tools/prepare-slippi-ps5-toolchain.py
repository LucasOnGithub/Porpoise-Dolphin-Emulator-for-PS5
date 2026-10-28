#!/usr/bin/env python3
"""Make an isolated Rust 1.88 sysroot with PS5-compatible synchronization."""
# SPDX-License-Identifier: GPL-3.0-or-later
from pathlib import Path
import hashlib
import json
import shutil
import shlex
import subprocess

ROOT = Path(__file__).resolve().parent.parent
DEST = ROOT / '.deps/slippi-ps5-toolchain'


def main():
    original = Path(subprocess.check_output(
        ['rustup', 'run', '1.88.0', 'rustc', '--print', 'sysroot'], text=True).strip())
    library = original / 'lib/rustlib/src/rust/library'
    if not library.is_dir():
        raise SystemExit('Install rustup component add rust-src --toolchain 1.88.0')
    identity = subprocess.check_output(['rustup', 'run', '1.88.0', 'rustc', '-Vv'], text=True)
    if 'commit-hash: 6b00bc3880198600130e1cf62b8f8a93494488cc' not in identity:
        raise SystemExit('Unexpected Rust compiler; the std port is pinned to Rust 1.88.0')
    key = hashlib.sha256(identity.encode() + Path(__file__).read_bytes()).hexdigest()
    stamp = DEST / 'porpoise-sysroot.json'
    if stamp.exists() and json.loads(stamp.read_text()).get('key') == key:
        print(DEST)
        return
    if DEST.exists():
        # This is a tool-owned generated sysroot, never a user checkout.
        shutil.rmtree(DEST)
    (DEST / 'bin').mkdir(parents=True)
    for binary in ('rustc', 'rustdoc'):
        wrapper = DEST / 'bin' / binary
        wrapper.write_text('#!/bin/sh\nexec ' + shlex.quote(str(original / 'bin' / binary)) +
                           ' --sysroot ' + shlex.quote(str(DEST)) + ' "$@"\n')
        wrapper.chmod(0o755)
    (DEST / 'lib/rustlib').mkdir(parents=True)
    for entry in (original / 'lib').iterdir():
        if entry.name != 'rustlib':
            (DEST / 'lib' / entry.name).symlink_to(entry, target_is_directory=entry.is_dir())
    for entry in (original / 'lib/rustlib').iterdir():
        if entry.name != 'src':
            (DEST / 'lib/rustlib' / entry.name).symlink_to(entry, target_is_directory=entry.is_dir())
    source = DEST / 'lib/rustlib/src/rust/library'
    shutil.copytree(library, source)
    modified = []
    paths = ['std/src/sys/sync/' + name + '/mod.rs' for name in
             ('mutex', 'condvar', 'rwlock', 'once', 'thread_parking')]
    paths.append('std/src/sys/pal/unix/sync/mod.rs')
    for relative in paths:
        path = source / relative
        text = path.read_text()
        old = 'target_os = "freebsd",'
        if text.count(old) != 1:
            raise SystemExit(f'Unexpected std source in {path}')
        path.write_text(text.replace(old,
            'all(target_os = "freebsd", not(target_vendor = "porpoise")), '))
        modified.append(str(path.relative_to(source)))
    stamp.write_text(json.dumps({'key': key, 'compiler': identity, 'std_changes': modified,
                                'console_verified': False}, indent=2) + '\n')
    print(DEST)


if __name__ == '__main__':
    main()
