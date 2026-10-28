# Slippi preparation in Porpoise

**Online matches are not playable in this build.** Account import and official
code profile preparation are implemented; the PS5 Slippi core migration is
unfinished. Read [SLIPPI_PS5_STATUS.md](../SLIPPI_PS5_STATUS.md) before testing.

## Add the required online account through FTP

1. On your phone or PC, open [Slippi's official sign-in page](https://slippi.gg/online/enable).
2. Sign in and download the official `user.json` export.
3. Open Porpoise > Settings > Online > Slippi Online > Login (FTP). This
   creates the account folder and displays its exact active path.
4. Connect to your PS5 through your existing FTP setup and upload the file to
   that path. With the default data folder, the destination is:

   `/data/porpoise/saves/User/Slippi/user.json`

5. Finish the transfer, then choose Reload FTP Account or Play Slippi Online.
   Porpoise also checks the path automatically when the Slippi panel opens.

If you moved Porpoise's data folder or use its sandbox fallback, use the path
shown by Login (FTP) instead of assuming the default. No file-browser
import step is needed. Upload the exported file, not your Slippi password.

Online preparation refuses to continue if this file is missing, unreadable or
invalid, and shows the FTP instructions and destination. Offline play does not
require an account. Reloading also detects replacement files and clears stale
account details if the current file is invalid or has been removed. Porpoise
validates the file's location and contents; it cannot attest which transfer
program or protocol wrote it.

Porpoise normalizes the export under `<Porpoise saves>/User/Slippi/user.json`
with private permissions where supported. The screen says **Loaded, unverified**, and shows only the name and connect
code. A valid export is not proof that matchmaking accepted its play key. No
password entry or direct email/password authentication is available.

Sign Out removes this console's uploaded copy. It does not revoke the play key or
delete your original download. Account files do not enter Porpoise's diagnostic
report, settings, or source control; do not share the original export.

## Melee and official codes

Starting a GALE01/GALJ01 library game opens a choice. **Play Offline** follows
normal Porpoise behavior, including its existing resume/settings functionality.
**Play Slippi Online** runs the preparation checks but stops with the missing
backend message; it never launches mandatory online codes on the offline core.

Gecko Codes is also accessible from Melee > Cheats and Patches > Slippi Online
Profile. Required codes have locks. Optional switches are saved separately from
your offline cheats. The region's official recommended defaults apply initially.

**Prepare Official Profile** checks the raw ISO/GCM's actual ID/revision/header
and the pinned official code asset, then writes `<Porpoise data>/slippi/GALE01r2.ini`
or `GALJ01r2.ini`. These are prepared files for core integration, not active
offline cheats. Full ISO validation, compressed images and a real Slippi core
remain required. No game images are included.

## Development checks

```sh
bash tools/test-slippi.sh
SANITIZE=1 bash tools/test-slippi.sh
cargo test --manifest-path tooling/slippi/Cargo.toml
```

The Rust command builds a host-only adapter against pinned upstream account
and matchmaking modules. Its tests do not contact Slippi services or use an
account. `account_load` invokes upstream `attempt_login`, which loads credentials
and attempts a public profile refresh; it does not validate the play key.
`search_begin` delegates to upstream's actual discovery implementation, accepts
only Unranked/Direct and requires the real peer socket's bound port. It must not
be wired to a Play button before EXI, rollback and peer networking are working.
No protocol is reimplemented and no localhost service or override is used.

The adapter is not included in the existing PS5 link. `Build` Actions runs host
checks and uploads host development outputs under the name `artifacts`, not a PS5 package build. Use [BUILDING.md](../BUILDING.md) for the full
SDK/RADV cross-compilation prerequisites.

The tracked Dolphin patch now includes an experimental Linux integration of
real Slippi EXI, rollback, Rust matchmaking and ENet peers. See
[SLIPPI_CORE.md](SLIPPI_CORE.md) for its matched sources, build and limitations.
The PS5 application still needs its Rust/socket/TLS target and session wiring.
