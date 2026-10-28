# Slippi on PS5: implementation status

Updated 2026-10-27. **This is an incomplete port, not a working Slippi release.**
The PS5 application cannot authenticate with matchmaking, find an opponent, or
play a rollback match yet. The online launch action refuses to boot the ordinary
Dolphin core with Slippi online codes. No real account or game image was used in
development; no PS5 or live matchmaking test has been performed.

## Experimental core integration

The tracked `patches/dolphin/ps5-port.patch` now includes upstream EXI/Rust
ownership, seven-frame game-memory rollback, ENet inputs/acks, STUN,
matchmaking, heap Gecko bootstrap and a separate host CurrentRun profile.
`tooling/slippi-core/pins.json` selects the exact C++/Rust pair and matching
reference assets. Normal PS5 builds keep Slippi disabled; attempting to enable
it on PS5 fails until a compatible Rust/socket/TLS ABI is implemented.

The complete Linux host module builds and links with the matched Rust backend.
Twenty core runtime checks pass, including actual guest-memory rollback,
EXI/DMA/Rust device ownership and input delivery over loopback UDP. Nine
upstream protocol/STUN/queue tests and 172 launcher checks under ASan/UBSan
also pass. Fifteen modified translation units pass compilation checks with
Slippi disabled. The synthetic fixture initializes the GameCube BAT mapping,
a temporary base config layer and libretro environment callback, and tears
logging down before configuration. No real account, game or PS5 was tested. See
[docs/SLIPPI_CORE.md](docs/SLIPPI_CORE.md) for reproduction and limitations.

## Implemented launcher preparation

- Controller-operated Slippi panel under Settings > Online, and a Melee
  offline/online choice when starting a detected GALE01/GALJ01 library game.
  Offline launch uses the existing startup/resume flow and settings.
- Official website sign-in instructions and required manual FTP upload to the
  backend's canonical account path, local account status, name/code, and sign-out.
  Login (FTP) displays the active path and prepares its folder. Reload FTP
  Account and online launch validate the uploaded file directly; the file-browser
  import step has been removed. Offline launch needs no account.
  The default FTP destination is `/data/porpoise/saves/User/Slippi/user.json`;
  moved data folders and sandbox fallback use the path displayed in the UI.
  Credentials are saved to `<saves>/User/Slippi/user.json`, the `User/Slippi`
  convention used by the inspected upstream File::GetSlippiUserConfigFolder().
- Bounded, strict JSON import: required string fields, UTF-8/escape validation,
  duplicate-key rejection, nesting/size limits, no raw parser errors, and no
  credential content in UI errors/logs. Unknown export fields are discarded.
  POSIX imports use private 0700 storage and unique 0600 temporary files, sync
  the file, and rename atomically. File symlinks/FIFOs and a symlinked Slippi
  directory are rejected. Existing offline User permissions are preserved.
- Account labels explicitly say **Loaded, unverified**. Neither file presence
  nor upstream `AttemptLogin()` is treated as server acceptance of `playKey`.
  Reauthenticate explains the missing backend instead of displaying success.
- Unmodified official NTSC-U/J revision 2 netplay INIs, pinned by commit and
  SHA-256. Three mandatory codes are locked on; nine recommended/optional codes
  use switches, official descriptions and defaults. Optional switches persist
  separately by region. Prepared profiles never append offline cheats.
- Existing cheat types/name normalization and switch renderer are reused. A
  Slippi profile entry is available from Melee's Cheats and Patches section.
- ISO/GCM header validation: actual six-byte ID, GameCube magic, revision byte,
  disc number and DOL/FST bounds. This is explicitly a **header check**, not full
  image validation. RVZ/CISO and full image checks require the integrated core.
- A Rust C-ABI services adapter uses pinned upstream `UserManager`, `APIClient`
  and `MatchmakingClient`; it calls real upstream account loading, Unranked and
  Direct discovery, state/cancellation and match-result retrieval. It disallows
  alternate matchmaking URLs and ranked/teams. This crate is **not linked into
  the PS5 app**, does not own the ENet socket, and is not a rollback backend.
  Search requires a caller-provided bound peer socket port; STUN candidates and
  LAN address plumbing still need integration before it can be used on PS5.
- `Build` Actions workflow for C++ host/sanitizer checks and Rust host checks.
  It does not produce or claim to produce an eboot.bin or PS5 package.

## Architecture decision and inspected sources

The intended full port is **A: integrate into the existing recent libretro
Dolphin core**, referencing the modern Slippi mainline implementation. This
preserves Porpoise's patched Vulkan device sharing, JIT, audio pull, loader,
controllers, Wii functionality and SDK shims. Launcher preparation and an experimental host migration are implemented;
the PS5 port is unfinished.

| Source | Inspected revision | Finding |
| --- | --- | --- |
| Porpoise Main | `61f38f6079a3011abba88e8f4f1cb0ddc0ed27a8` | Core source is fetched, not vendored |
| libretro/dolphin build pin | `4d23cf151640eb810cb1b8e9d9fc922cf59c0b87` | Recent Dolphin, PS5 patch workflow; no Slippi device |
| project-slippi/dolphin, slippi | `5a1d426ffb3440a4d38a18b687ae59971dff0163` | Modern EXI/System APIs, SlippiSavestate, ENet; no DolphinLibretro frontend |
| buro33/slippi-android | `6155cd0b860b4a997a6e405acd4270cb97441da4` | Website export capture; older Ishiiruka core plus a mainline submodule |
| project-slippi/slippi-rust-extensions | `af46ba77bd9eaca96eb623a50bdc531ecefcbd34` | Account, WebSocket matchmaking, STUN and FFI interfaces inspected |
| project-slippi/slippi-ssbm-asm | `394805595f9c21caca6e2439f3097117947b8d79` | Bundled `Output/Netplay/GALE01r2.ini` and `GALJ01r2.ini` |

The mainline Slippi checkout itself pins Rust at
`ddcee36ba7b2d3280e46df3197c8a900ea7267d0`; Android pins a different revision.
The standalone adapter uses the explicitly inspected newer Rust API, **not an
asserted compatible C++/Rust pair**. The experimental core uses the exact C++/Rust pair and matching codes/assets
from that C++ reference checkout. Its development version and development
service selection are retained. The launcher profiles remain separate.

Approach B would need a second PS5 renderer/libretro/SDK port; neither inspected
Slippi fork supplies Porpoise's libretro frontend. Approach C cannot remove the
need to integrate EXI command handling, game memory and rollback inside Dolphin.
No feasibility or performance claim is made until the PS5 prerequisites below
are resolved.

### Authentication findings

Ishiiruka/Android's `SlippiUser(uintptr_t)` wraps
`slprs_user_attempt_login(uintptr_t)` and
`slprs_user_get_is_logged_in(uintptr_t)`. That pointer is an owned Rust shadow
EXI device, not an arbitrary frontend handle. Calling those functions without
creating and integrating the matching EXI device is invalid.

At the inspected Rust revision, `UserManager::attempt_login()` parses the
export, marks it loaded, and tries a user-info GET. It can return true even if
that GET fails. The GET is keyed by uid and does **not** validate the play key;
matchmaking sends uid/playKey through upstream's protocol. The adapter therefore
calls its frontend operation `account_load`, not `authenticate`.

The inspected Android flow captures the official website's exported `user.json`;
it does not provide a verified email/password-to-playKey exchange that can be
ported independently. Direct password entry is not enabled. No Firebase token
is substituted for `playKey`, and no private key or invented endpoint is used.

### Code requirements confirmed from the pinned files

- Required: General Codes
- Required: Slippi Recording
- Required: Slippi Online
- Enabled by default: Recommended: Normal Lag Reduction; Recommended: Apply
  Delay to all In-Game Scenes; Recommended: Lagless FoD.
- Optional: Freeze Final Destination Background; Widescreen 16:9; Disable Screen
  Shake; Center Align 2P HUD; Flash Red on Failed L-Cancel; Show Friendly Player
  Indicators.

The required codes explicitly need a Slippi EXI device in Slot B. Prepared files
retain upstream's Core settings, but those values cannot be assumed compatible
with the recent PS5 core without the migration. Do not copy a prepared file into
offline GameSettings as a way to enable online play.

## Validation and build results

| Check | Result |
| --- | --- |
| `bash tools/test-slippi.sh` | 172 synthetic C++ assertions passed, including direct FTP uploads; changed UI syntax checked with repository headers |
| Asset SHA-256 and compiled digest consistency | Passed for both regional profiles |
| Shared SHA-256 extraction | Empty, abc and million-a published vectors passed |
| `g++ ... -fsyntax-only src/porpoise_update.cpp` | Passed after shared SHA-256 extraction |
| `git diff --check`, shell syntax | Passed |
| ASan/UBSan | 172 checks passed with `ASAN_OPTIONS=detect_leaks=0`; default LeakSanitizer cannot inspect this executor's restricted /proc. No address/undefined-behavior findings in that run |
| `bash tools/build-dolphin.sh` | Blocked immediately: `error: bootstrap this project's SDK first` |
| `bash tools/build-porpoise.sh` | Blocked immediately: `clang, an archive tool, and ranlib are required to build zlib` |
| Matched Rust FFI and complete Linux libretro module | Built and linked locally with Rust 1.88.0, locked dependencies and the matched mainline C++ source |
| Core runtime tests | 20 passed: guest-memory rollback/preservation, EXI/DMA/Rust ownership, state guards and real Slippi peer inputs over loopback |
| Matched Rust matchmaking tests | 9 passed: protocol/STUN and local queue/cancellation tests; no real service/account |
| Slippi-disabled compilation | 15 modified translation units passed |
| Publishing / Actions execution | Sources committed to Main after GitHub write access was restored. Build runs host checks and uploads host development outputs as artifacts; check the current Actions run for its result. No PS5 package is produced |
| PS5 application/core build, hardware and live service tests | Not performed; no PS5 package produced |

All account keys, names and disc bytes in tests are synthetic. Tests include
invalid JSON, bad field types, Unicode, duplicates, overlong/deep inputs,
symlinks/FIFO, private file permissions, failed-import preservation, sign-out,
canonical FTP uploads/reloads, replacement/partial/missing account files,
regional profiles/defaults, required-code/foreign-cheat rejection, asset
tampering, optional preference round trips and malformed/revision-mismatched
disc headers. They do not validate an actual game or an actual account.

## Files changed

- `src/porpoise_slippi.{hpp,cpp}`, `src/slippi_codes.inc`: account/profile/image
  preparation and compiled asset pins.
- `src/ui_app_slippi.cpp`, `src/ui_app.{hpp,cpp}`, `src/ui_app_controls.cpp`,
  `src/ui_app_online.cpp`, `src/ui_app_settings.cpp`, `src/ui_app_common.hpp`:
  controller UI, launch choice and browser integration.
- `src/ui_cheats.{hpp,cpp}`, `src/ui_app_cheats_view.cpp`: shared parser,
  code-name handling and reusable switch/lock renderer.
- `src/porpoise_sha256.hpp`, `src/porpoise_update.cpp`: extraction of the existing
  updater's SHA-256 for reuse, with digest-vector checks.
- `assets/slippi/*`, `third_party/nlohmann/*`,
  `tooling/notices/components.json`: pinned code assets and license attribution.
- `tooling/slippi/{Cargo.toml,src/lib.rs,porpoise_slippi_services.h}`: upstream
  Rust services adapter and its two non-network host tests.
- `tests/slippi_tests.cpp`, `tools/test-slippi.sh`,
  `.github/workflows/build.yml`, `.gitignore`, `BUILDING.md`, this status report
  and `docs/SLIPPI.md`: tests, CI, credential exclusions and setup/status docs.

Core changes are now tracked in the PS5 patch behind `ENABLE_SLIPPI`; the
ordinary PS5 core defaults to Slippi off. New launcher C++ sources are
picked up by the existing source enumeration; Slippi assets use existing asset
staging and notice generation.

## Remaining work and blocking gates

1. Bootstrap the documented SDK/RADV/Clang environment and prove the baseline
   PS5 build. There is no PS5 SDK or hardware available in this executor.
2. The first matched core integration is tracked in
   `patches/dolphin/ps5-port.patch`. Prove booted-game compatibility and
   memory/JIT/cache behavior; synthetic host tests do not prove an actual
   rollback match or a compatible PS5 build.
3. Implement/prove a PS5 Rust target and standard-library support with the
   payload SDK ABI. An x86-64 Linux or FreeBSD static library is not automatically
   a compatible PS5 library. Verify getrandom/entropy, filesystem/time,
   pthread synchronization, socket/select/poll, DNS, ring/rustls certificates,
   WebSocket HTTPS and ENet UDP on actual hardware. Existing sceHttp2 support
   does not automatically supply Rust's std::net or tungstenite transports.
4. The host core connects the matched FFI, EXI lifetime, ENet socket,
   upstream STUN, inputs and rollback. Port and prove those transports on PS5,
   including real account acceptance, NAT/peer failures and session teardown.
   Loopback input delivery is not an internet match.
5. Use Dolphin's volume APIs for revision/partition validation and full image
   checks, including compressed images. A header-compatible ISO can still be
   modified/corrupt and cause desyncs.
6. Add a session-only core/config profile: mandatory codes, Slot B, determinism,
   supported timing settings and exclusion of offline cheats. Lock save/load
   state, quick resume, turbo, fast-forward and unsafe timing changes throughout
   a real online session, then restore offline preferences on all exit paths.
   Currently no online session can launch, so no offline preference is altered.
7. Expose actual account/match/peer/error events to the launcher; launch Unranked
   and Direct; test cancellation, failures, disconnect/reconnect, NAT types and
   teardown. Only then consider teams, ranked and replay recording.
8. Cross-build the entire app and core, install on PS5, verify offline GameCube
   and Wii/Vulkan/audio/DualSense regression behavior, then test authentic
   account acceptance and real Slippi opponents. Only that can complete the
   requested Detect → Codes → Authenticate → EXI → Launch → Matchmaking slice.
