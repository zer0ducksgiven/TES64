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
# TES64: Skyrim's terrain is lumpier than SM64's levels. Sliding (and the steep-jump rule) starts at 55 degrees instead of
# 38 and 30, so ordinary hills are walkable and only really steep ground slides
grep -q 'TES64 slope' src/decomp/game/mario.c || {
  sed -i 's|normY = 0.7880108f; //~cos(38 deg)|normY = 0.5735764f; /* TES64 slope: cos(55 deg) */|' src/decomp/game/mario.c
  sed -i 's|normY = 0.8660254f; // ~cos(30 deg)|normY = 0.5735764f; /* TES64 slope */|' src/decomp/game/mario.c
}
# ...and ordinary slopes only slow Mario a little, so he can climb Skyrim's hills (SM64's own pull is 1.7 a tick)
grep -q 'TES64 slope pull' src/decomp/game/mario_actions_moving.c || \
  sed -i 's|slopeAccel = 1.7f;|slopeAccel = 0.5f; /* TES64 slope pull */|' src/decomp/game/mario_actions_moving.c
# anything downloaded from the decomp goes, so it can't end up in the dll
rm -f src/decomp/mario/*.inc.c src/decomp/mario/*.inc.h build/src/decomp/mario/*.o build/src/decomp/mario/*.d
mingw32-make lib CC=gcc OS=Windows_NT -j8 >/dev/null 2>&1
cd '$HERE' && mkdir -p dist
gcc -O2 -shared -DGBI_FLOATS -DSM64_LIB_EXPORT -DVERSION_US -DNO_SEGMENTED_MEMORY \
  -I'$LIB/src' -I'$LIB/src/decomp/include' -o dist/sm64.dll \
  tes64_rom.c mario_rom_geo.c mario_rom_dl.c rom_format.c hud.c audio.c \
  \$(ls ../../extern/libsm64/build/src/*.o ../../extern/libsm64/build/src/decomp/*.o ../../extern/libsm64/build/src/decomp/*/*.o ../../extern/libsm64/build/src/decomp/*/*/*.o 2>/dev/null) \
  -static -static-libgcc -lm -lwinmm
cp '$LIB/src/libsm64.h' dist/
"
echo built "$HERE/dist/sm64.dll"
