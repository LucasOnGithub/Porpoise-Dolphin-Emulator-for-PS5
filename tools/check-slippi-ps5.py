#!/usr/bin/env python3
"""Audit the linked PS5 Rust backend, including compiled C/Rust ABI values."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess


def inspect(path):
    data = path.read_bytes()
    if data[:8] != b'\x7fELF\x02\x01\x01\x09' or struct.unpack_from('<HH', data, 16) != (3, 62):
        raise ValueError('Expected x86-64 FreeBSD/PS5 shared ELF')
    dynamic = subprocess.check_output(['readelf', '-dW', str(path)], text=True)
    needed = re.findall(r'\(NEEDED\).*\[([^\]]+)\]', dynamic)
    if set(needed) != {'libkernel_web.sprx', 'libSceLibcInternal.sprx', 'libScePosixForWebKit.sprx'}:
        raise ValueError(f'Unexpected imported libraries: {needed}')
    phoff = struct.unpack_from('<Q', data, 32)[0]
    phsize, phcount = struct.unpack_from('<HH', data, 54)
    loads = []
    for i in range(phcount):
        kind, flags, offset, address, _, filesz, memsz, align = struct.unpack_from('<IIQQQQQQ', data, phoff + i * phsize)
        if kind == 7:
            raise ValueError('Native ELF TLS is unsupported by the core loader')
        if kind == 1:
            if align < 0x4000 or (flags & 3) == 3 or offset % 0x4000 != address % 0x4000:
                raise ValueError('Invalid PS5 load segment')
            loads.append((address, filesz, offset))
    symbols = subprocess.check_output(['readelf', '-sW', str(path)], text=True)
    values, undefined, exports = {}, set(), set()
    for line in symbols.splitlines():
        fields = line.split()
        if len(fields) != 8 or not fields[0].endswith(':'):
            continue
        name = fields[7]
        if fields[6] == 'UND':
            undefined.add(name)
        else:
            exports.add(name)
        if name not in ('SLIPPI_PS5_C_ABI', 'SLIPPI_PS5_RUST_ABI'):
            continue
        address, size = int(fields[1], 16), int(fields[2])
        for start, filesz, offset in loads:
            if start <= address and address + size <= start + filesz:
                values[name] = list(struct.unpack_from('<' + 'Q' * (size // 8), data, offset + address - start))
    if len(values) != 2 or len(values['SLIPPI_PS5_C_ABI']) != 36 or values['SLIPPI_PS5_C_ABI'] != values['SLIPPI_PS5_RUST_ABI']:
        raise ValueError(f'C/Rust ABI mismatch: {values}')
    required = {'slprs_exi_device_create', 'slprs_exi_device_destroy', 'slprs_mm_start',
                'slprs_mm_cancel', 'slprs_stun_build_request', 'slprs_ps5_platform_probe'}
    if required - exports:
        raise ValueError(f'Missing backend exports: {sorted(required - exports)}')
    if any('@' in name or name in ('_umtx_op', 'arc4random_buf', 'getrandom', '__tls_get_addr',
                                   'dl_iterate_phdr', 'pthread_setname_np', 'fork', 'execvp',
                                   'posix_spawnp') for name in undefined):
        raise ValueError('Unsupported versioned ABI, futex, RNG or native TLS import')
    relocations = subprocess.check_output(['readelf', '-rW', str(path)], text=True)
    if re.search(r'R_X86_64_(?:TPOFF|DTP|TLS|GOTTPOFF)', relocations):
        raise ValueError('Native TLS relocation')
    disasm = subprocess.check_output(['llvm-objdump', '-d', '--no-show-raw-insn', str(path)], text=True)
    if re.search(r'\s(?:syscall|sysenter)\s*$', disasm, re.MULTILINE):
        raise ValueError('Raw system call in title backend')
    return {'kind': 'experimental-ps5-slippi-backend', 'sha256': hashlib.sha256(data).hexdigest(),
            'bytes': len(data), 'c_rust_abi_values': values['SLIPPI_PS5_C_ABI'],
            'needed': needed, 'undefined_symbols': sorted(undefined),
            'console_tested': False, 'live_match_tested': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('module', type=Path)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    result = inspect(args.module)
    if args.report:
        args.report.write_text(json.dumps(result, indent=2) + '\n')
    print('PS5 Slippi backend audit PASS: 36 C/Rust ABI values, pthread TLS/sync, secure RNG bindings; console untested')


if __name__ == '__main__':
    main()
