<p align="center"><img src="https://github.com/elripalda/Porpoise-Dolphin-Emulator-for-PS5/raw/Main/docs/images/banner.png" alt="Porpoise" width="100%"></p>

**Porpoise 4.0 Alpha Build 4**

### New
- **Frame Generation (beta)** in Settings → Video and the in-game menu: makes frames between the game's own, so a 30 fps game is shown at 60, and with the PS5 set to 120 Hz a 60 fps game at 120. Off by default; needs V-Sync
- **Steady 30 FPS**, under Frame Generation: shows the game's frames at an even 30 and frame generation makes it 60, for games that swing between 30 and 60
- **Change Disc** in the in-game menu for games on two discs, or another game's disc when a game asks for one
- **GameCube Menu** in Settings → Console: start your own GameCube's menu, with a game in the drive or none (needs your own BIOS)
- **Wii Menu** in Settings → Console: start your own Wii's menu, with a Wii game in the drive or none
- **Wii Channels** in Settings → Console: put your channels (WADs) in the Wii Menu
- **Wii System Update** in Settings → Console: download the Wii's system software (Wii Menu, IOS and channels) from Nintendo's servers, like Dolphin's Online System Update
- **Add a Code** in a game's Cheats and Patches: type in a Gecko code (like WiiLink WFC's) with a name
- **Remove Wii System Data** to start fresh (what was in it is kept in `saves/User/Wii-removed`)

### Fixes
- Rumble no longer sets off a shake (the Nunchuk shake in two-controller play)
- Games on two discs change to the other disc by themselves when they ask for it
- The launch screen no longer looks frozen while a game is checked for achievements
- "An incorrect disc has been inserted" with Start Wii Discs in the Wii Menu on: GameCube games and other regions' Wii discs now start directly
- Cover downloads start again after a cancelled Wii System Update

> [!WARNING]
> **Porpoise is still in early development on the PS5.** If you run into bugs or glitches in a game, please report them on the **[RIPALDA Discord](https://discord.gg/GgDE5Vynyu)**, with the game's name, and we'll get them fixed as soon as we can.

## Install or update

**Use ShadowMountPlus 1.7 beta 4 or newer.** With older versions, Porpoise can close the moment it opens on some setups (firmware 12.x with the Lapy JB Daemon or LegacyJB).

**From 2.0 or later:** **Settings → About → Updates**.

**By hand:** download **`Porpoise-4.0.zip`**, delete `/data/homebrew/PPSA99764/` on the PS5, and copy the **`PPSA99764`** folder into **`/data/homebrew/`**. With FTP, use binary transfers (FileZilla: Transfer → Transfer type → Binary), or some text files won't copy. Your games, saves and settings in `/data/porpoise/` are kept.

Porpoise contains no games, BIOS or firmware. Play only games you own. Not affiliated with Nintendo, Sony or RetroAchievements. Licenses are in `licenses/README.txt` inside the zip.

`Porpoise-4.0.zip` SHA-256: `66552d6ad2dd4e41c03374d91c5d71d550354bea22015ff2732166cf817912ff`
