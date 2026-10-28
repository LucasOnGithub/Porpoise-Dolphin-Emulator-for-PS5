#!/usr/bin/env bash
# Install the same public SDK pin used by Porpoise, without RADV or zlib.
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
sdk="$root/.deps/native/ps5-payload-sdk"
revision=f674976f48a8c319abd5646079eca284fde790fd
if [[ -f $sdk/.ps5-sdk-revision && $(<"$sdk/.ps5-sdk-revision") == "$revision" ]]; then
    printf '%s\n' "$sdk"
    exit 0
fi
source_dir="${PS5_PAYLOAD_SDK_FORK:-$root/.deps/slippi-ps5-sdk-source}"
if [[ ! -d $source_dir/.git ]]; then
    [[ ! -e $source_dir ]] || { echo "SDK source destination already exists" >&2; exit 2; }
    git init --quiet "$source_dir" >&2
    git -C "$source_dir" remote add origin https://github.com/mihawk-99/PS5_PayloadSDK.git
fi
git -C "$source_dir" cat-file -e "$revision^{commit}" 2>/dev/null ||
    git -C "$source_dir" fetch --depth=1 origin "$revision" >&2
tree=$(mktemp -d)
trap 'rm -rf -- "$tree"' EXIT
git -C "$source_dir" archive "$revision" | tar -x -C "$tree"
bash "$tree/platform/tools/setup-sdk.sh" "$sdk" "$revision" "$root/.deps/native" >&2
printf '%s\n' "$sdk"
