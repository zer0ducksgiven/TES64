# TES64: Super Mario 64's Mario in Skyrim

Press **F10** in game to switch to Mario: Super Mario 64's movement, powered by [libsm64](https://github.com/libsm64/libsm64).

**You need your own Super Mario 64 (US) ROM.** TES64 contains nothing from the game; Mario's model, textures and
sounds are read from your ROM. The installer asks for it.

Requires: Skyrim Special Edition / Anniversary Edition, [SKSE64](https://skse.silverlock.org), and
[Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444).

## Building
1. `native/sm64/build.sh` (MSYS2 MinGW-w64) builds `sm64.dll`: libsm64 with NG64's patches and its ROM model loader.
2. In `plugin/`, from a VS 2022 developer shell: `cmake --preset release && cmake --build --preset release`.
3. `./package.sh` builds `dist/` and the installer (Inno Setup 6).

Based on [NG64](https://github.com/zer0ducksgiven/NG64) (MIT).
