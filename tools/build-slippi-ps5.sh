#!/usr/bin/env bash
# Experimental PS5 Rust backend and ELF-loader platform diagnostic.
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
sdk=$(bash tools/setup-slippi-ps5-sdk.sh)
export PS5_PAYLOAD_SDK="$sdk"
export RUSTUP_TOOLCHAIN=1.88.0
rustup component add rust-src --toolchain 1.88.0
python3 tools/prepare-slippi-ps5.py
sysroot=$(python3 tools/prepare-slippi-ps5-toolchain.py)
export RUSTC="$sysroot/bin/rustc" RUSTDOC="$sysroot/bin/rustdoc"
export RUSTC_BOOTSTRAP=1 RUST_LIBC_UNSTABLE_FREEBSD_VERSION=10
export CC_porpoise_ps5="$root/tooling/slippi-ps5/cc"
export AR_porpoise_ps5="$sdk/bin/prospero-ar"
# A changed std port must not reuse rlibs from another std configuration.
std_key=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["key"][:16])' "$sysroot/porpoise-sysroot.json")
build="$root/build/slippi-ps5"
rust="$root/.deps/slippi-ps5-rust"
cargo_target="$build/rust-$std_key"
mkdir -p "$build"
cargo build -Zbuild-std=std,panic_abort -Zbuild-std-features=panic_immediate_abort \
    --locked --release --no-default-features --features mainline \
    --target "$root/tooling/slippi-ps5/porpoise-ps5.json" \
    --manifest-path "$rust/ffi/Cargo.toml" --target-dir "$cargo_target"
# Only this Rust archive is rewritten. The matched FFI/protocol is unchanged;
# its versioned FreeBSD 10 symbols bind to the SDK's matching unversioned ABI.
"$sdk/bin/prospero-objcopy" --redefine-syms="$root/tooling/slippi-ps5/symbols.map" \
    "$cargo_target/porpoise-ps5/release/libslippi_rust_extensions.a" "$build/libslippi_ps5.a"
"$sdk/bin/prospero-clang" -std=c11 -O2 -fPIC -Wall -Wextra -Werror \
    -c tooling/slippi-ps5/runtime.c -o "$build/runtime.o"
# Linking all FFI entry points catches issues hidden by merely producing an
# archive. This is a backend module for auditing, not a libretro emulator.
"$sdk/bin/prospero-clang" -shared -nostdlib -nodefaultlibs -Wl,-m,elf_x86_64_fbsd \
    -Wl,-T,tooling/native/ps5-core.ld -Wl,--gc-sections \
    -Wl,--version-script,tooling/slippi-ps5/exports.map \
    "$build/runtime.o" -Wl,--whole-archive "$build/libslippi_ps5.a" -Wl,--no-whole-archive \
    -lkernel_web -lSceLibcInternal -lScePosixForWebKit -o "$build/slippi_ps5_backend.so"
python3 tools/check-slippi-ps5.py "$build/slippi_ps5_backend.so" --report "$build/backend-audit.json"

# The diagnostic is an ELF-loader payload. Its environment differs from a
# registered native title, so passing it is necessary evidence, not proof that
# Porpoise can play an online match. Reuse the console DNS implementation.
"$sdk/bin/prospero-clang" -std=c11 -O2 -Wall -Wextra -Werror \
    -c tooling/slippi-ps5/probe.c -o "$build/probe.o"
"$sdk/bin/prospero-clang++" -std=c++17 -O2 -DPORPOISE_DNS_STANDALONE \
    -c src/porpoise_dns.cpp -o "$build/dns.o"
"$sdk/bin/prospero-clang++" "$build/probe.o" "$build/runtime.o" "$build/dns.o" \
    "$build/libslippi_ps5.a" -Wl,--gc-sections \
    -Wl,--wrap=getaddrinfo -Wl,--wrap=freeaddrinfo -Wl,--wrap=getnameinfo \
    -Wl,--defsym=__wrap_getaddrinfo=porpoise_core_getaddrinfo \
    -Wl,--defsym=__wrap_freeaddrinfo=porpoise_core_freeaddrinfo \
    -Wl,--defsym=__wrap_getnameinfo=porpoise_core_getnameinfo \
    -lps5platform -o "$build/slippi-platform-probe.elf"
"$sdk/bin/prospero-strip" --strip-debug "$build/slippi-platform-probe.elf"
cp "$sysroot/porpoise-sysroot.json" "$build/"
python3 - "$root" "$build" <<'PY'
import hashlib, json, pathlib, shutil, subprocess, sys
root, build = map(pathlib.Path, sys.argv[1:])
files = ['tooling/slippi-core/pins.json', 'patches/slippi/ps5-rust.patch',
         'tools/prepare-slippi-ps5-toolchain.py', 'tooling/slippi-ps5/porpoise-ps5.json',
         'tooling/slippi-ps5/runtime.c', 'tooling/slippi-ps5/symbols.map']
manifest = {'kind': 'experimental-ps5-backend-and-diagnostic', 'console_tested': False,
            'playable_online': False, 'sdk_revision': (root / '.deps/native/ps5-payload-sdk/.ps5-sdk-revision').read_text().strip(),
            'inputs_sha256': {p: hashlib.sha256((root / p).read_bytes()).hexdigest() for p in files},
            'outputs_sha256': {p: hashlib.sha256((build / p).read_bytes()).hexdigest() for p in
                ('libslippi_ps5.a', 'slippi_ps5_backend.so', 'slippi-platform-probe.elf')}}
(build / 'build.json').write_text(json.dumps(manifest, indent=2) + '\n')
licenses = build / 'licenses'
licenses.mkdir(exist_ok=True)
shutil.copy2(root / 'LICENSE', licenses / 'Porpoise-GPL-3.0.txt')
rust = root / '.deps/slippi-ps5-rust'
shutil.copy2(rust / 'LICENSE', licenses / 'Slippi-Rust-LICENSE.txt')
metadata = json.loads(subprocess.check_output(['cargo', 'metadata', '--locked', '--format-version', '1',
    '--manifest-path', str(rust / 'Cargo.toml'), '--filter-platform', str(root / 'tooling/slippi-ps5/porpoise-ps5.json')]))
packages = []
for package in metadata['packages']:
    packages.append({key: package.get(key) for key in ('name', 'version', 'license', 'source', 'repository')})
    source = pathlib.Path(package['manifest_path']).parent
    for text in source.iterdir():
        if text.is_file() and text.name.lower().startswith(('license', 'copying', 'notice')):
            output = licenses / 'Rust' / (package['name'] + '-' + package['version']) / text.name
            output.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(text, output)
(licenses / 'Rust-dependencies.json').write_text(json.dumps(packages, indent=2) + '\n')
PY
printf '%s\n' "PS5 backend and diagnostic built under $build; console untested, online play still gated."
