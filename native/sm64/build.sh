#!/bin/bash
# Builds sm64.dll (libsm64 + NG64's patches + the ROM model loader) with MSYS2 MinGW-w64 into native/sm64/dist.
# Nothing of Nintendo's is compiled in: Mario's model, textures and sounds are read from the player's ROM at runtime.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$HERE/../.."
LIB="$ROOT/extern/libsm64"
PATCHES="$ROOT/extern/NG64/helper/patches"
/c/msys64/usr/bin/bash.exe -lc "
set -e
export PATH=/mingw64/bin:/usr/bin:\$PATH
cd '$LIB'
grep -q 'sm64_mario_pick_up' src/libsm64.c || patch -p1 -s < '$PATCHES/libsm64-carry.patch'
grep -q 'sm64_ng64_music_play' src/libsm64.c || patch -p1 -s < '$PATCHES/libsm64-music.patch'
grep -q 'sm64_mario_grab_bowser' src/libsm64.c || patch -p1 -s < '$PATCHES/libsm64-spin.patch'
grep -q 'sm64_mario_burn' src/libsm64.c || patch -p1 -s < '$PATCHES/libsm64-burn.patch'
grep -q 'sm64_ng64_play_sound' src/libsm64.c || patch -p1 -s < '$PATCHES/libsm64-sound.patch'
grep -q 'g_ng64PartCount' src/gfx_adapter.c || patch -p1 -s < '$PATCHES/libsm64-parts.patch'
grep -q 'read from the player' Makefile || patch -p1 -s < '$PATCHES/libsm64-rommodel.patch'
grep -q 'NG64: the game.s handler without the level exit' src/decomp/game/mario_actions_cutscene.c || patch -p1 -s < '$PATCHES/libsm64-stardance.patch'
grep -q 'sm64_ng64_offer_grab' src/libsm64.c || patch -p1 -s < '$PATCHES/libsm64-grab.patch'
grep -q 'register s64 x1' src/decomp/engine/surface_collision.c || patch -p1 -s < '$PATCHES/libsm64-bigworld.patch'
# TES64: a spatial grid for static surfaces (Skyrim streams tens of thousands of triangles)
grep -q 'loaded_surface_iter_begin' src/load_surfaces.c || patch -p1 -s --binary < '$HERE/libsm64-tes64-grid.patch'
# anything downloaded from the decomp goes, so it can't end up in the dll
rm -f src/decomp/mario/*.inc.c src/decomp/mario/*.inc.h build/src/decomp/mario/*.o build/src/decomp/mario/*.d
mingw32-make lib CC=gcc OS=Windows_NT -j8 >/dev/null 2>&1
cd '$HERE' && mkdir -p dist
gcc -O2 -shared -DGBI_FLOATS -DSM64_LIB_EXPORT -DVERSION_US -DNO_SEGMENTED_MEMORY \
  -I'$LIB/src' -I'$LIB/src/decomp/include' -o dist/sm64.dll \
  tes64_rom.c mario_rom_geo.c mario_rom_dl.c rom_format.c hud.c \
  \$(ls ../../extern/libsm64/build/src/*.o ../../extern/libsm64/build/src/decomp/*.o ../../extern/libsm64/build/src/decomp/*/*.o ../../extern/libsm64/build/src/decomp/*/*/*.o 2>/dev/null) \
  -static -static-libgcc -lm
cp '$LIB/src/libsm64.h' dist/
"
echo built "$HERE/dist/sm64.dll"
