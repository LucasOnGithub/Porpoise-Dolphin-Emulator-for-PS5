<p align="center"><img src="https://github.com/elripalda/Porpoise-Dolphin-Emulator-for-PS5/raw/Main/docs/images/banner.png" alt="Porpoise" width="100%"></p>

**Porpoise 3.0.1**

### New
- **GameCube Menu** in Settings → Console: start your own GameCube's menu, with a game in the drive or none (needs your own BIOS)
- **Wii Menu** in Settings → Console: start your own Wii's menu, with a Wii game in the drive or none
- **Wii Channels** in Settings → Console: put your channels (WADs) in the Wii Menu
- **Wii System Update** in Settings → Console: download the Wii's system software (Wii Menu, IOS and channels) from Nintendo's servers, like Dolphin's Online System Update
- **Add a Code** in a game's Cheats and Patches: type in a Gecko code (like WiiLink WFC's) with a name
- **Remove Wii System Data** to start fresh (what was in it is kept in `saves/User/Wii-removed`)

### Fixes
- "An incorrect disc has been inserted" with Start Wii Discs in the Wii Menu on: GameCube games and other regions' Wii discs now start directly

> [!WARNING]
> **Porpoise is still in early development on the PS5.** If you run into bugs or glitches in a game, please report them on the **[RIPALDA Discord](https://discord.gg/GgDE5Vynyu)**, with the game's name, and we'll get them fixed as soon as we can.

## Install or update

**Use ShadowMountPlus 1.7 beta 4 or newer.** With older versions, Porpoise can close the moment it opens on some setups (firmware 12.x with the Lapy JB Daemon or LegacyJB).

**From 2.0 or later:** **Settings → About → Updates**.

**By hand:** download **`Porpoise-3.0.1.zip`**, delete `/data/homebrew/PPSA99764/` on the PS5, and copy the **`PPSA99764`** folder into **`/data/homebrew/`**. With FTP, use binary transfers (FileZilla: Transfer → Transfer type → Binary), or some text files won't copy. Your games, saves and settings in `/data/porpoise/` are kept.

Porpoise contains no games, BIOS or firmware. Play only games you own. Not affiliated with Nintendo, Sony or RetroAchievements. Licenses are in `licenses/README.txt` inside the zip.

`Porpoise-3.0.1.zip` SHA-256: `9c67a86827910e5392e273141f8050e0ebffee4d6486d9fbb6d46f05e80b1187`
