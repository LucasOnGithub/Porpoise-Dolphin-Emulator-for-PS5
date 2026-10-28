# Experimental Slippi core port

This integrates the real upstream Slippi EXI, rollback and ENet peer code into
Porpoise's pinned libretro Dolphin tree. **It is a Linux host development port;
Slippi online is not playable in the PS5 application yet.** The regular PS5
build defaults to Slippi off. A separate opt-in PS5 build path now links an
experimental matched Rust target with pthread TLS, console sockets and secure
entropy. See [PS5 platform testing](SLIPPI_PS5_TESTING.md). Console execution
and actual online play remain unverified.

## Core implementation

- Actual Slot B EXI device, C ABI and matched Rust shadow-device ownership.
  The selected ISO path and `User/Slippi/` directory reach upstream Rust.
  In-game login requests reload the account file and show `Login (FTP)`
  instructions when it is missing.
- Upstream seven-frame rollback pool, capture/restore of game memory and the
  dynamic heap, with preservation of input/audio regions. Invalid heap bounds
  and preservation ranges are rejected before allocation or restoration.
- Upstream ENet Slippi input, acknowledgement, selection and peer protocols,
  STUN from the peer socket, and Rust matchmaking. Packet IDs retain their
  upstream values. Unranked and Direct are the only enabled search modes.
- Heap Gecko bootstrap, code-list responses and disc reads for linked assets.
  A separate CurrentRun profile supplies matching codes, Slot B and timing
  without merging ordinary local cheats or overwriting offline preferences.
- Explicit experimental libretro launch option, disabled by default, accepting
  NTSC-U Melee 1.02 (`GALE01`, revision 2). Ordinary libretro state/reset/cheat
  operations, fast-forward detection and overclock changes are guarded.
- Synchronous matchmaking/peer cancellation before freeing Rust ownership;
  atomic status/cancellation fields and protected matchmaking error text.
  EXI framing checks reject truncated commands and protect the fixed online ABI
  from recording-table overrides.

Ranked, teams, replay files/seeking, jukebox and spectator output are disabled
in this first session profile. The existing PS5 launcher remains gated.

## Reproduce the Linux build

Requires a C++23 compiler, CMake 3.25+, Python 3, Git, pkg-config, Rustup and
ALSA development headers. On Ubuntu 24.04:

```sh
sudo apt-get install cmake pkg-config libasound2-dev
rustup toolchain install 1.88.0 --profile minimal
bash tools/build-slippi-core-host.sh
```

The script fetches exact revisions from
[`tooling/slippi-core/pins.json`](../tooling/slippi-core/pins.json), applies
`patches/dolphin/ps5-port.patch` to a separate checkout, builds the complete
libretro module, and runs its compiled core tests. Existing worktrees are never
reset. The only Rust source change is FFI packaging from `cdylib` to `staticlib`;
the upstream lockfile is retained.

`tests/slippi_core_tests.cpp` exercises actual guest-memory restoration,
preserved/excluded regions, EXI factory/DMA/Rust ownership, state guards and
controller-input delivery between real Slippi clients over loopback UDP. It
uses synthetic memory and a temporary directory without an account or game.
These tests are not an emulated match, real matchmaking or a PS5 hardware test.
The local Linux build passes 20 core runtime checks, nine upstream
protocol/STUN/queue tests and 172 launcher checks under ASan/UBSan. Fifteen
modified translation units also pass compilation checks with Slippi disabled.

Outputs under `build/slippi-core-host/` include the experimental Linux
`dolphin_libretro.so` (with its test entry point), separate
`system/dolphin-emu/Sys/` assets, license texts and a source/asset hash manifest.
The host CI build disables Vulkan. Do not install this Linux module on a PS5.
No game image or account is included.

For manual host research, use that separate system directory and enable
`Experimental Slippi core` before loading a legally obtained Melee image.
Prepared PS5 launcher profiles and this experimental matched core profile are
separate inputs.

## Compatibility and remaining work

The C++ reference is `project-slippi/dolphin` at
`5a1d426ffb3440a4d38a18b687ae59971dff0163`, paired with its Rust submodule
`ddcee36ba7b2d3280e46df3197c8a900ea7267d0`. Codes, bootloader and linked game
assets come from that same reference checkout. Its advertised version remains
`4.0.0-mainline-beta.20-dev.1`. Upstream Rust selects development matchmaking
for development versions; this build does not impersonate a production release.
The earlier standalone services adapter uses a different pin and is not linked
into this core.

The new PS5 Rust/SDK bridge cross-builds and passes binary and host boundary
checks. It needs console and native-title validation of entropy, filesystem/time,
threads, UDP/DNS/STUN and the retained rustls HTTPS/WebSocket transports,
followed by frontend session/event integration. Real game compatibility, rollback determinism and
performance, NAT/disconnect behavior and offline GameCube/Wii/Vulkan/audio
regressions require game and hardware testing. A host build cannot prove them.

Source notices are retained in the tracked patch. Slippi/Dolphin C++ carries
GPL-2.0-or-later notices; Base64 retains its MIT notice. Reference semver,
open-vcdiff, Rust and dependency licenses remain upstream's original texts.
