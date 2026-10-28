# Experimental PS5 Slippi platform testing

This is development work toward Slippi on Porpoise. **Online matches cannot
launch from the PS5 frontend yet.** The new Rust backend is cross-compiled for
the public payload SDK; it is no longer a Linux archive relabeled as PS5 code.
Console execution and actual game compatibility remain unverified.

## What your PS5 tester can run now

The `Build` workflow, job `Ishiiruka`, uploads `artifacts`. Inside its
`build/slippi-ps5/` directory, `slippi-platform-probe.elf` is an **ELF-loader
diagnostic**, not an `eboot.bin`, package, or emulator core. Run it through the
ELF loader already used for homebrew on the console. The SDK's sender can send
it from a development machine:

```sh
.deps/native/ps5-payload-sdk/bin/prospero-deploy -h PS5_IP -p 9021 \
  build/slippi-ps5/slippi-platform-probe.elf
```

Use the port configured for that console's ELF loader. The command sends the
diagnostic; it does not install a Porpoise title. The usual FTP `user.json`
upload is not needed for this test. No account, play key, ISO or matchmaking
ticket is used by the diagnostic.

Retrieve `/data/porpoise/slippi-platform-probe.txt` through FTP afterward. It
reports initialization and these stages:

| Stage | Check |
| --- | --- |
| 1 | C/Rust structure sizes, offsets, socket and error constants |
| 2 | Console random-number service |
| 3 | Rust threads, mutexes, thread-local destructors, hashing and monotonic time |
| 4 | Create, write, sync, stat, read and remove a temporary file |
| 5 | UDP loopback and bounded nonblocking reads |
| 6 | Resolve the reference development matchmaking service |
| 7 | HTTPS to `slippi.gg` with upstream certificate verification |
| 8 | Server-authenticated TLS WebSocket handshake to the reference development service; close without joining |

The log is flushed after each stage. If execution stops, return the partial log
and the console crash log. A payload's environment differs from a registered
native title, so even all passes do not establish that Porpoise can play a
match. The PC opponent test comes after the native title integration is ready.

## Build the platform backend and diagnostic

On Ubuntu 24.04, install Clang/LLD/LLVM 18, Rustup, Python 3, Git, Make, wget,
unzip and the tools listed in `BUILDING.md`. Then:

```sh
rustup toolchain install 1.88.0 --profile minimal
bash tools/test-slippi-ps5.sh
bash tools/build-slippi-ps5.sh
```

The SDK installer uses the same public SDK revision as the regular Porpoise
build. It exports a pinned tree and never resets an existing user checkout.
The backend uses the matched `project-slippi/dolphin` Rust submodule revision
from `tooling/slippi-core/pins.json`, with the changes recorded in
`patches/slippi/ps5-rust.patch`.

Rust's standard library is rebuilt from Rust 1.88.0 source with an experimental
custom target. The separate generated sysroot uses pthread TLS and pthread
synchronization/parking instead of native ELF TLS and FreeBSD futex operations.
`RUST_LIBC_UNSTABLE_FREEBSD_VERSION=10` selects structures matching the SDK;
36 compiled C/Rust ABI values are compared during the binary audit and again
on the console. The build explicitly opts into unstable `build-std` using the
pinned compiler; it does not alter the user's installed Rust sources.

The console bridge handles socket creation flags, `FIONBIO` plus `SO_NBIO`,
the SDK's older thread-name API, and cryptographic randomness through
`sceRandomGetRandomNumber`. RNG failure is propagated; std's void RNG interface
aborts on failure. It never falls back to the platform's timestamp-seeded
`arc4random` replacement. Rustls, webpki roots, HTTPS and WebSocket certificate
verification remain upstream's implementations. TLS is not disabled.

PS5 does not compile desktop ALSA/jukebox output or browser child processes.
Jukebox construction reports unsupported, account provisioning says
`Login (FTP)`, and updates stay in Porpoise's launcher. The game uses its
emulated audio. Panic behavior is immediate abort; desktop backtrace discovery
is excluded because the title lacks the required loader interface.

Outputs also include `libslippi_ps5.a`, an audited `slippi_ps5_backend.so`,
input/output hashes and notices. **The backend `.so` is not a libretro core.**
Do not replace `dolphin_libretro.so` with it. The diagnostic itself uses the
payload SDK's normal payload runtime; the backend audit checks the stricter
native-title rules, including no raw system calls.

## Experimental core build

```sh
bash tools/build-slippi-core-ps5.sh
```

This opts into the PS5 Slippi CMake path, links the actual matched backend into
Dolphin and stages matching game assets under `build/slippi-core-ps5/`.
The complete local build succeeds and passes the PS5 ELF audit, including all
25 required libretro exports, expected library dependencies and no raw system
calls. Actions also cross-builds this core and uploads it under that same path.
It keeps the normal staged core and title separate. The frontend still refuses
online launches. The ordinary PS5 build defaults to Slippi disabled. A successful
cross-build does not verify console loading or a game session.

Remaining work includes proving the runtime in a native title, handling online
session lifecycle/events in the frontend, testing a real Melee boot and rollback
determinism, and testing authenticated matchmaking, STUN/NAT, Direct connections
and disconnect/teardown on PS5. The retained upstream development version selects
development matchmaking; production PC interoperability is not established.
