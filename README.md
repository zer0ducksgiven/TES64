# TES64: Super Mario 64's Mario in Skyrim

Press **F10** to become Mario: Super Mario 64's own movement, running on Skyrim's world, with Skyrim's weapons,
spells, shouts, menus, quests and HUD still working. TES64 is an SKSE plugin for Skyrim Special Edition / Anniversary
Edition. The movement is [libsm64](https://github.com/libsm64/libsm64) (a faithful decompilation of the game's own
code), fed Skyrim's real collision every frame.

**You need your own Super Mario 64 (US) ROM.** This repository and its releases contain nothing from the game: Mario,
his animations, the textures, the HUD graphics, the Power Star, the coins and every sound are read from your ROM, on
your PC, when the game starts. The installer asks for it.

> Not affiliated with or endorsed by Nintendo or Bethesda. "Super Mario 64" and "The Elder Scrolls" belong to their
> owners.

## What it does
- **Mario's moves**: walk, run, triple jump, long jump, wall kick, backflip, ground pound, dive, slide, ledge grab,
  swimming, the caps: SM64's physics, tick for tick.
- **Skyrim's collision** is Mario's world: terrain, buildings, rocks, trees, doors, bridges, interiors, streamed in as
  he moves. Doors and fast travel take him along.
- **Skyrim's combat still works.** A sword swing, spell, block or shout is Skyrim's, and Mario plays Skyrim's animation
  for it with his upper body while his legs keep running and strafing. Weapons are carried at their real size.
  Skyrim's furniture (chairs, beds) and killmoves put Mario's whole body in the player's pose.
- **Mario hurts things too**: punches, kicks, dives and stomps damage Skyrim's people and creatures. A stomp bounces
  once per enemy per jump and ragdolls them; a ground pound lands for heavy damage and a shockwave that throws people,
  bodies and clutter. Fall damage and burns come from SM64.
- **A Super Mario 64 HUD** over Skyrim's: LIFE (health, the POWER meter), MAGIKA and STAMINA wedges bottom left, star
  and coin counters top right, an SM64-style compass at the top with Skyrim's own quest and location markers over it.
  The crosshair hides while your weapons are away. Coins are your gold (picking gold up heals).
- **Power Stars**: complete a quest as Mario and a Power Star appears nearby. Touch it for the star dance, the jingle and
  a full heal.
- **Gold from the dead** floats out of the body as spinning SM64 coins.
- **Mario's voice and SM64's sounds**, from the ROM. Skyrim's text in the SM64 font (optional: see Building).

## Requirements
- Skyrim Special Edition / Anniversary Edition (Steam; developed on 1.7.104)
- [SKSE64](https://skse.silverlock.org): start the game with `skse64_loader.exe`
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)
- Your own Super Mario 64 (US) ROM (`.z64`, `.v64` or `.n64`)
- Optional, for the in-game settings page: [SkyUI](https://www.nexusmods.com/skyrimspecialedition/mods/12604) and
  [MCM Helper](https://www.nexusmods.com/skyrimspecialedition/mods/53000). Without them, edit
  `Data/SKSE/Plugins/TES64.ini`.

## Installing
Run **TES64-Setup.exe** from the [releases](../../releases): choose your ROM, check the Skyrim folder it found, install.
It puts the plugin in `Data`, copies your ROM to `%LOCALAPPDATA%\TES64`, and enables `TES64.esp` (a light plugin for the
settings menu). Uninstall from Windows' Installed apps.

## Controls
| | Keyboard / mouse | Xbox pad |
|---|---|---|
| Mario mode on/off | **F10** | |
| Move | W A S D | left stick |
| Jump (A) | Space | **A** (activates instead when you're at something usable) |
| Crouch / ground pound (Z) | Ctrl (sneak) | **LB**, or left-stick click |
| Punch / kick / dive (B) | **V** | **Y** punches and kicks; a quick tap of **X** dives |
| Weapon / spell | left / right mouse | triggers |
| Draw / sheathe weapons | R | hold **X** |
| Shout, activate, menus | as in Skyrim | as in Skyrim |

## Settings
`TES64.ini` or the MCM page: toggle and attack keys, Mario's size, attack damage, weapon size, body-pose and HUD options
(HUD size, crosshair, compass, SM64 font), sounds and volume, NPC comments.

## Building from source
1. **Dependencies** go in `extern/` (not in this repository). The commits it was built against:
   - `extern/libsm64`: [libsm64](https://github.com/libsm64/libsm64) `fd11813208`
   - `extern/NG64`: [NG64](https://github.com/zer0ducksgiven/NG64) `e5fc763f2d` (its patches and ROM loaders for libsm64)
   - `extern/CommonLibSSE`: [CommonLibVR](https://github.com/alandtse/CommonLibVR), branch `ng` (CommonLibSSE NG) `8ffc2b74af`
   - `extern/vcpkg`: [vcpkg](https://github.com/microsoft/vcpkg) `e456309491`
2. `native/sm64/build.sh` (MSYS2 MinGW-w64 at `C:\msys64`) builds `sm64.dll`: libsm64 + NG64's patches + TES64's grid
   and slope patches + the ROM model, HUD, audio and object loaders.
3. `plugin/`: from a VS 2022 developer shell, `cmake --preset release && cmake --build --preset release`.
4. `./package.sh` generates the template meshes and the ESP (`tools/`), assembles `dist/` and builds the installer
   (Inno Setup 6). `TES64_RELEASE=1 ./package.sh` is the build for publishing.
5. **The SM64 font** (optional): put a TrueType pixel font named `Mario64.ttf` beside `package.sh` and it is converted
   into a Skyrim font file (`tools/make_font_swf.py`; needs `pip install fonttools skia-pathops`). Without it the
   game keeps its own fonts. The font is not part of this repository.

For a private test build, `./package.sh` (without `TES64_RELEASE`) also bundles SKSE64 and Address Library into the
installer if the SKSE `.7z` is in the repository folder and Address Library is in your Skyrim. Neither may be
redistributed, so never publish that installer.

## Credits and licence
MIT licence (see `LICENSE`). Built on [libsm64](https://github.com/libsm64/libsm64) (MIT), the decompilation of Super Mario
64, and [NG64](https://github.com/zer0ducksgiven/NG64) (MIT), the same idea for BeamNG. CommonLibSSE NG ([CommonLibVR](https://github.com/alandtse/CommonLibVR))
and [SKSE64](https://skse.silverlock.org) make the plugin possible.
