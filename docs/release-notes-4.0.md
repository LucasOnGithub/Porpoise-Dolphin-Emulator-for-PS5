<p align="center"><img src="https://github.com/elripalda/Porpoise-Dolphin-Emulator-for-PS5/raw/Main/docs/images/banner.png" alt="Porpoise" width="100%"></p>

**Porpoise 4.0**

### New
- **Frame Generation (beta)** in Settings → Video and the in-game menu: a 30 fps game shown at 60, and with the PS5 at 120 Hz a 60 fps game at 120. Off by default; needs V-Sync
- **Steady 30 FPS**, under Frame Generation, for games that swing between 30 and 60
- **Simpler Settings**: just what most players need, with **Show All Settings** for everything
- **Auto Boost** in a game's settings: the settings that run it best on PS5, with one press
- **A calmer Porpoise theme**: fainter grid, no light beams, cleaner glass
- **Star Cube** redone after the GameCube's own menu
- **Smooth Edges (FXAA)** screen filter
- **Player 1's Port** for games that want your controller in another port
- **Change Disc** in the in-game menu, for two-disc games and expansion discs

### Fixes and speed
- Smoother quick saves and screenshots, lighter input and logging work while you play
- Games with a big shader cache no longer stall on a black screen before they start
- Luigi's Mansion (PAL) and other games start with cheats on
- NFS shares with version 3 turned off now connect
- Rumble no longer sets off Wii Remote and Nunchuk shakes
- Several rare crashes and hangs

> [!NOTE]
> Found a bug? **Settings → About → Report a Bug** saves Porpoise's logs. Send them with the game's name on the **[RIPALDA Discord](https://discord.gg/GgDE5Vynyu)**.

## Install or update

**Use ShadowMountPlus 1.7 beta 4 or newer.** With older versions, Porpoise can close the moment it opens on some setups (firmware 12.x with the Lapy JB Daemon or LegacyJB).

**From 2.0 or later:** **Settings → About → Updates**.

**By hand:** download **`Porpoise-4.0.zip`**, delete `/data/homebrew/PPSA99764/` on the PS5, and copy the **`PPSA99764`** folder into **`/data/homebrew/`**. With FTP, use binary transfers (FileZilla: Transfer → Transfer type → Binary), or some text files won't copy. Your games, saves and settings in `/data/porpoise/` are kept.

Porpoise contains no games, BIOS or firmware. Play only games you own. Not affiliated with Nintendo, Sony or RetroAchievements. Licenses are in `licenses/README.txt` inside the zip.

`Porpoise-4.0.zip` SHA-256: `CHECKSUM`
