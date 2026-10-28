#!/usr/bin/env bash
# Opt-in PS5 core experiment. It never replaces the normal staged core/title.
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
export LLVM_CONFIG="${LLVM_CONFIG:-$(command -v llvm-config-18 || command -v llvm-config)}"
bash tools/build-slippi-ps5.sh
sdk="$root/.deps/native/ps5-payload-sdk"
export PS5_PAYLOAD_SDK="$sdk"
dolphin=${SLIPPI_PS5_DOLPHIN_SOURCE:-"$root/.deps/slippi-ps5-dolphin"}
reference="$root/.deps/slippi-core-reference"
rust="$root/.deps/slippi-ps5-rust"
build="$root/build/slippi-core-ps5"
cmake=${CMAKE:-cmake}
# The host preparation checkout remains separate from the PS5 Rust patch.
python3 tools/prepare-slippi-core.py --reference "$reference" --dolphin "$dolphin"
git -C "$dolphin" submodule update --init --recursive --jobs 4 -- \
    Externals/rcheevos Externals/VulkanMemoryAllocator Externals/Vulkan-Headers
mkdir -p "$build/empty-libm"
"$sdk/bin/prospero-clang++" -std=c++11 -fPIC -fno-exceptions -fno-rtti \
    -c tooling/native/core_cxx_runtime.cpp -o "$build/core_cxx_runtime.o"
"$sdk/bin/prospero-clang++" -std=c++17 -O2 -fPIC \
    -c tooling/dolphin/ps5-libc-shims.cpp -o "$build/ps5-libc-shims.o"
[[ -f $build/empty-libm/libm.a ]] || "$sdk/bin/prospero-ar" rc "$build/empty-libm/libm.a"
"$cmake" -S "$dolphin" -B "$build/cmake" -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_TOOLCHAIN_FILE="$root/tooling/dolphin/ps5-toolchain.cmake" -DCMAKE_BUILD_TYPE=Release \
    -DPS5_EMPTY_LIBM="$build/empty-libm" \
    -DPS5_CORE_LINK_INPUTS="$build/core_cxx_runtime.o $build/ps5-libc-shims.o" \
    -DENABLE_SLIPPI=ON -DSLIPPI_PS5_EXPERIMENTAL=ON \
    -DSLIPPI_REFERENCE_SOURCE="$reference" -DSLIPPI_RUST_SOURCE="$rust" \
    -DSLIPPI_PS5_BACKEND_DIR="$root/build/slippi-ps5" \
    -DLIBRETRO=ON -DENABLE_X11=OFF -DENABLE_EGL=OFF -DUSE_SYSTEM_LIBS=OFF \
    -DUSE_MGBA=OFF -DENABLE_LLVM=OFF -DENCODE_FRAMEDUMPS=OFF -DUSE_UPNP=OFF \
    -DENABLE_SDL=OFF -DENABLE_CLI_TOOL=OFF -DENABLE_TESTS=OFF -DENABLE_QT=OFF \
    -DENABLE_AUTOUPDATE=OFF -DENABLE_ANALYTICS=OFF -DUSE_DISCORD_PRESENCE=OFF \
    -DUSE_RETRO_ACHIEVEMENTS=ON
"$cmake" --build "$build/cmake" --target dolphin_libretro --parallel "${JOBS:-3}"
cp "$build/cmake/dolphin_libretro.so" "$build/dolphin_slippi_libretro.so"
python3 tools/check-core.py "$build/dolphin_slippi_libretro.so" --report "$build/abi.json"
python3 - "$dolphin" "$reference" "$build" <<'PY'
import hashlib, json, pathlib, shutil, sys
dolphin, reference, build = map(pathlib.Path, sys.argv[1:])
dest = build / 'system/dolphin-emu/Sys'
shutil.copytree(dolphin / 'Data/Sys', dest, dirs_exist_ok=True)
shutil.copy2(reference / 'Data/Sys/bootloader.gct', dest / 'bootloader.gct')
shutil.copytree(reference / 'Data/Sys/GameFiles', dest / 'GameFiles', dirs_exist_ok=True)
(dest / 'Slippi').mkdir(exist_ok=True)
shutil.copy2(reference / 'Data/Sys/GameSettings/Netplay/GALE01r2.ini', dest / 'Slippi/GALE01r2.ini')
report = json.loads((build / 'abi.json').read_text())
blocked = {'_umtx_op', 'arc4random_buf', 'getrandom', '__tls_get_addr',
           'dl_iterate_phdr', 'pthread_setname_np', 'posix_spawnp', 'fork', 'execvp'}
unsupported = [name for name in report['undefined_symbols'] if '@' in name or name in blocked]
if unsupported:
    raise SystemExit('Unsupported Rust/core imports: ' + ', '.join(unsupported))
report.update(kind='experimental-ps5-core', console_tested=False, live_match_tested=False,
              frontend_online_launch_enabled=False, no_unsupported_rust_imports=True,
              pins=json.loads(pathlib.Path('tooling/slippi-core/pins.json').read_text()),
              port_patch_sha256=hashlib.sha256(pathlib.Path('patches/dolphin/ps5-port.patch').read_bytes()).hexdigest(),
              rust_archive_sha256=hashlib.sha256(pathlib.Path('build/slippi-ps5/libslippi_ps5.a').read_bytes()).hexdigest())
(build / 'build.json').write_text(json.dumps(report, indent=2) + '\n')
PY
echo "Experimental PS5 core: $build/dolphin_slippi_libretro.so; frontend online launch remains gated."
