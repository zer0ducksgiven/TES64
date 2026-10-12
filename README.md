# TES64: Super Mario 64's Mario in Skyrim

Press **F10** to become Mario: Super Mario 64's own movement (through [libsm64](https://github.com/libsm64/libsm64))
running on Skyrim's world, with Skyrim's weapons, spells, shouts, menus and HUD still working.

**You need your own Super Mario 64 (US) ROM.** TES64 contains nothing from the game: Mario's model, animations,
textures, HUD graphics and sounds are all read from your ROM on your PC. The installer asks for it.

## Requirements
- Skyrim Special Edition / Anniversary Edition (Steam; tested on 1.7.104)
- [SKSE64](https://skse.silverlock.org) (start the game with `skse64_loader.exe`)
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)
- Optional, for the in-game settings page: [SkyUI](https://www.nexusmods.com/skyrimspecialedition/mods/12604) and
  [MCM Helper](https://www.nexusmods.com/skyrimspecialedition/mods/53000). Without them, edit
  `Data/SKSE/Plugins/TES64.ini`.

## Installing
Run **TES64-Setup.exe**: choose your ROM (any name; .z64/.v64/.n64), check the Skyrim folder it found, install. It
puts the plugin in `Data`, your ROM in `%LOCALAPPDATA%\TES64`, and enables `TES64.esp` (a light plugin, for the
settings menu). Uninstall from Windows' Installed apps.

## Playing
| | Keyboard / mouse | Notes |
|---|---|---|
| Mario mode on/off | **F10** | |
| Move | W A S D | relative to the camera, like SM64 |
| Jump (A) | Space | hold for higher; triple jump, long jump (crouch + jump), wall kick, backflip... |
| Crouch / ground pound (Z) | Sneak (Ctrl) | ground pound in the air |
| Punch / kick / dive (B) | **V** | |
| Xbox pad | **A** jump, **Y** punch/kick, tap **X** dive (hold X: draw/sheathe), **LB** or left-stick click crouch / ground pound | sticks, triggers (attack / cast), B (menu), shouts and the d-pad stay Skyrim's |
| Skyrim weapon / spell | left / right mouse | Mario turns to the crosshair and his upper body plays Skyrim's swing, cast or block; he can still run and strafe any way. Skyrim deals the damage |
| Shout | Z | |
| Activate, grab, menus | as in Skyrim | |

- **Weapons** are carried at their real size, so a greatsword is nearly as big as Mario. Stomps bounce once per enemy per jump and ragdoll them; ground pounds hit far harder.
- **Combat**: Mario's punches, kicks, dives, slide kicks, ground pounds and stomps hurt Skyrim's people and creatures
  (strong hits knock them down) and shove physics clutter. Skyrim's hits knock Mario back, SM64-style.
- **Health**: Mario's power meter shows your Skyrim health. Falls and burns in SM64 cost Skyrim health.
- **HUD**: LIFE (your health, the POWER meter), MAGIKA and STAMINA in the bottom left; stars and coins top right; an SM64-style compass at the top. Skyrim's bars and compass are hidden while you're Mario, and so is the crosshair while your weapons are away. All of Skyrim's text uses the SM64 font (needs `Mario64.ttf` when packaging; restart after changing).
- **Furniture and executions**: while the player sits, sleeps, uses furniture or is in a killmove, Mario's whole body copies their pose and he goes where they are. Killmoves aren't verified in-game yet.
- **Power Stars**: every quest you complete as Mario puts a Power Star on open ground nearby. Touch it for the star dance and a full heal.
- **Coins and stars**: coins are your gold (picking gold up heals, like SM64's coins), stars are completed quests.
- **Caps**: invisibility (spell or potion) gives the Vanish Cap, the armour spells (Oakflesh...) the Metal Cap,
  Whirlwind Sprint the Wing Cap.
- The world is Skyrim's real collision (terrain, buildings, rocks, interiors), streamed in as you move. Doors and fast
  travel take Mario along.

## Settings (`TES64.ini` or the MCM page)
Toggle and attack keys, Mario's size (`fScale`), his attack damage, weapon size, sounds and volume, NPC comments.

## Building
1. `native/sm64/build.sh` (MSYS2 MinGW-w64 at `C:\msys64`) builds `sm64.dll`: libsm64 + NG64's patches + TES64's
   grid patch + NG64's ROM model/HUD/audio loaders.
2. `plugin/`: from a VS 2022 developer shell, `cmake --preset release && cmake --build --preset release`.
3. `./package.sh` generates the template mesh and ESP (`tools/`), assembles `dist/` and builds the installer (Inno
   Setup 6).
4. The UAT harness lives beside this repo (`Documents/TES64-UAT`): `py -3 -m pytest`.

Based on [NG64](https://github.com/zer0ducksgiven/NG64) and [libsm64](https://github.com/libsm64/libsm64) (MIT).
Not affiliated with Nintendo or Bethesda.
