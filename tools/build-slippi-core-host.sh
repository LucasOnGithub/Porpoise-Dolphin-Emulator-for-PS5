#!/usr/bin/env bash
# Experimental Linux core integration; not a PS5 package builder.
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
reference=${SLIPPI_REFERENCE_SOURCE:-"$root/.deps/slippi-core-reference"}
rust=${SLIPPI_RUST_SOURCE:-"$root/.deps/slippi-core-rust"}
dolphin=${SLIPPI_DOLPHIN_SOURCE:-"$root/.deps/slippi-core-dolphin"}
build=${SLIPPI_BUILD_DIR:-"$root/build/slippi-core-host"}
cmake=${CMAKE:-cmake}
python3 tools/prepare-slippi-core.py --reference "$reference" --rust "$rust" --dolphin "$dolphin"
RUSTUP_TOOLCHAIN=1.88.0 RUSTFLAGS="-C relocation-model=pic" cargo test --locked --release \
  --manifest-path "$rust/Cargo.toml" -p slippi-matchmaking --no-default-features --features mainline \
  --target-dir "$build/slippi-rust"
"$cmake" -S "$dolphin" -B "$build" -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_SLIPPI=ON -DSLIPPI_REFERENCE_SOURCE="$reference" -DSLIPPI_RUST_SOURCE="$rust" \
  -DSLIPPI_CORE_TEST_SOURCE="$root/tests/slippi_core_tests.cpp" \
  -DLIBRETRO=ON -DENABLE_QT=OFF -DENABLE_X11=OFF -DENABLE_EGL=OFF -DENABLE_SDL=OFF \
  -DENABLE_VULKAN=OFF -DENABLE_ALSA=OFF -DENABLE_HWDB=OFF -DENABLE_EVDEV=OFF \
  -DENABLE_PULSEAUDIO=OFF -DENABLE_CUBEB=OFF -DENABLE_LLVM=OFF -DUSE_MGBA=OFF \
  -DUSE_RETRO_ACHIEVEMENTS=OFF -DENABLE_ANALYTICS=OFF -DENABLE_AUTOUPDATE=OFF \
  -DENCODE_FRAMEDUMPS=OFF -DUSE_DISCORD_PRESENCE=OFF -DENABLE_CLI_TOOL=OFF
"$cmake" --build "$build" --target dolphin_libretro --parallel "${JOBS:-3}"
python3 tools/test-slippi-core.py "$build/dolphin_libretro.so"
python3 - "$dolphin" "$reference" "$build" "$rust" <<'PY'
import hashlib, json, os, pathlib, shutil, subprocess, sys
dolphin, reference, build, rust = map(pathlib.Path, sys.argv[1:])
dest = build / 'system/dolphin-emu/Sys'
shutil.copytree(dolphin / 'Data/Sys', dest, dirs_exist_ok=True)
shutil.copy2(reference / 'Data/Sys/bootloader.gct', dest / 'bootloader.gct')
shutil.copytree(reference / 'Data/Sys/GameFiles', dest / 'GameFiles', dirs_exist_ok=True)
(dest / 'Slippi').mkdir(exist_ok=True)
shutil.copy2(reference / 'Data/Sys/GameSettings/Netplay/GALE01r2.ini', dest / 'Slippi/GALE01r2.ini')
manifest = {'kind': 'experimental-linux-host-core', 'ps5': False, 'live_match_tested': False,
            'pins': json.loads(pathlib.Path('tooling/slippi-core/pins.json').read_text()),
            'port_patch_sha256': hashlib.sha256(pathlib.Path('patches/dolphin/ps5-port.patch').read_bytes()).hexdigest(),
            'rust_lock_sha256': hashlib.sha256((rust / 'Cargo.lock').read_bytes()).hexdigest(), 'assets_sha256': {}}
for p in [dest / 'bootloader.gct', dest / 'Slippi/GALE01r2.ini', *sorted((dest / 'GameFiles').rglob('*'))]:
    if p.is_file(): manifest['assets_sha256'][str(p.relative_to(dest))] = hashlib.sha256(p.read_bytes()).hexdigest()
(build / 'slippi-core-build.json').write_text(json.dumps(manifest, indent=2) + '\n')
licenses = build / 'licenses'
licenses.mkdir(exist_ok=True)
shutil.copy2('LICENSE', licenses / 'Porpoise-GPL-3.0.txt')
shutil.copy2(dolphin / 'COPYING', licenses / 'Dolphin-COPYING.txt')
shutil.copytree(dolphin / 'LICENSES', licenses / 'Dolphin', dirs_exist_ok=True)
shutil.copy2(dolphin / 'Externals/licenses.md', licenses / 'Dolphin-dependencies.md')
shutil.copy2(dolphin / 'Source/Core/Common/Base64.hpp', licenses / 'Slippi-Base64-MIT.hpp')
shutil.copy2(reference / 'Externals/semver/LICENSE', licenses / 'Semver-LICENSE.txt')
shutil.copy2(reference / 'Externals/open-vcdiff/COPYING', licenses / 'Open-vcdiff-COPYING.txt')
shutil.copy2(rust / 'LICENSE', licenses / 'Slippi-Rust-LICENSE.txt')
shutil.copy2('third_party/nlohmann/LICENSE', licenses / 'Nlohmann-MIT.txt')
for text in (dolphin / 'Externals').rglob('*'):
    if text.is_file() and text.name.lower().startswith(('license', 'copying', 'notice')):
        output = licenses / 'Dolphin-externals' / text.relative_to(dolphin / 'Externals')
        output.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(text, output)
metadata = json.loads(subprocess.check_output(['cargo', 'metadata', '--locked',
    '--manifest-path', str(rust / 'Cargo.toml'), '--format-version', '1',
    '--filter-platform', 'x86_64-unknown-linux-gnu'],
    env=dict(os.environ, RUSTUP_TOOLCHAIN='1.88.0')))
packages = []
for package in metadata['packages']:
    packages.append({key: package.get(key) for key in ('name', 'version', 'license', 'source', 'repository')})
    source = pathlib.Path(package['manifest_path']).parent
    texts = [p for p in source.iterdir() if p.is_file() and
             p.name.lower().startswith(('license', 'copying', 'notice'))]
    if package.get('license_file'):
        text = source / package['license_file']
        if text.is_file() and text not in texts: texts.append(text)
    for text in texts:
        output = licenses / 'Rust' / (package['name'] + '-' + package['version']) / text.name
        output.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(text, output)
(licenses / 'Rust-dependencies.json').write_text(json.dumps(packages, indent=2) + '\n')
PY
printf '%s\n' "Experimental Linux host core built and tested: $build/dolphin_libretro.so"
