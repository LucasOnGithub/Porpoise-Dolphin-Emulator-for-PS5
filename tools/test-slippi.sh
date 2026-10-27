#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
mkdir -p build/tests
python3 - <<'PY'
import hashlib, json, pathlib
root = pathlib.Path('.')
meta = json.loads((root / 'assets/slippi/provenance.json').read_text())
expected = '/* Generated from assets/slippi/provenance.json; checked by tools/test-slippi.sh. */\n'
for name, digest in meta['files'].items():
    assert hashlib.sha256((root / 'assets/slippi' / name).read_bytes()).hexdigest() == digest, name
    expected += '{"' + name + '", "' + digest + '"},\n'
assert (root / 'src/slippi_codes.inc').read_text() == expected, 'compiled code digests differ'
print('Official Slippi asset digests match the compiled pins.')
PY
cxx=${CXX:-g++}
args=(-std=c++20 -O1 -g -Wall -Wextra -Werror -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections
      -Isrc -Ithird_party -Ithird_party/vulkan)
if [[ ${SANITIZE:-0} == 1 ]]; then args+=(-fsanitize=address,undefined -fno-omit-frame-pointer); fi
"$cxx" "${args[@]}" tests/slippi_tests.cpp src/porpoise_slippi.cpp src/ui_cheats.cpp \
    -Wl,--gc-sections -o build/tests/slippi_tests
build/tests/slippi_tests "$root/assets/slippi"
# Check the changed controller UI against the repository's actual headers.
# This does not substitute for the PS5 cross-build or a hardware UI test.
"$cxx" -std=c++20 -fno-exceptions -fno-rtti -Isrc -Ithird_party -Ithird_party/vulkan -fsyntax-only \
    src/ui_app_slippi.cpp src/ui_app_cheats_view.cpp src/ui_app.cpp src/ui_app_settings.cpp src/ui_app_controls.cpp src/ui_app_online.cpp
