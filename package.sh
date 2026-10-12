#!/bin/bash
# Builds everything into dist/:
#   dist/TES64/            the files for a by-hand install (Data/SKSE/Plugins/...)
#   dist/TES64-Setup.exe   the installer (needs Inno Setup 6: winget install JRSoftware.InnoSetup)
# The plugin must already be built (cmake --build --preset release in plugin/, from a VS developer shell).
set -e
cd "$(dirname "$0")"
VERSION="$(tr -d ' \r\n' < VERSION)"
./native/sm64/build.sh
P=dist/TES64/Data/SKSE/Plugins
rm -rf dist && mkdir -p "$P/TES64"
cp plugin/build/release/TES64.dll "$P/"
cp plugin/TES64.ini "$P/"
cp native/sm64/dist/sm64.dll "$P/TES64/"
py -3 tools/make_mario_nif.py >/dev/null
py -3 tools/make_esp.py >/dev/null
cp -r plugin/data/meshes plugin/data/textures plugin/data/MCM plugin/data/TES64.esp dist/TES64/Data/
cp README.md LICENSE dist/TES64/ 2>/dev/null || true

# Test bundle: SKSE64 and Address Library, from the copies on this PC (the SKSE .7z in the repo root, the bins in the
# installed Skyrim). Not for public release: neither mod's licence lets TES64 redistribute them.
R=dist/TES64/redist
SZ="/c/Program Files/7-Zip/7z.exe"
SKSE7Z="$(ls | grep -i '^Skyrim Script Extender.*7z$' | head -1)"
SKYRIM="$(grep -o 'sSkyrimDir=.*' plugin/TES64.ini 2>/dev/null | cut -d= -f2)"
[ -d "$SKYRIM" ] || SKYRIM="/c/Program Files (x86)/Steam/steamapps/common/Skyrim Special Edition"
if [ -n "$SKSE7Z" ] && [ -f "$SZ" ] && ls "$SKYRIM"/Data/SKSE/Plugins/versionlib-*.bin >/dev/null 2>&1; then
  mkdir -p "$R/skse" "$R/addresslib"
  T="$(mktemp -d)"; "$SZ" x -y -o"$T" "$SKSE7Z" >/dev/null
  S="$(ls -d "$T"/skse64_*)"
  cp "$S"/skse64_*.dll "$S"/skse64_loader.exe "$R/skse/"
  cp -r "$S/Data/Scripts" "$R/skse/Scripts"; rm -rf "$R/skse/Scripts/Source"
  cp "$SKYRIM"/Data/SKSE/Plugins/versionlib-*.bin "$R/addresslib/"
  rm -rf "$T"
  BUNDLE=1
else
  echo "no SKSE/Address Library bundle (sources not found)"
fi

ISCC=""
for c in "$LOCALAPPDATA/Programs/Inno Setup 6/ISCC.exe" "/c/Program Files (x86)/Inno Setup 6/ISCC.exe" "/c/Program Files/Inno Setup 6/ISCC.exe"; do
  [ -f "$c" ] && ISCC="$c" && break
done
if [ -z "$ISCC" ]; then
  echo "packaged dist/TES64/ (no installer: Inno Setup 6 isn't installed)"
  exit 1
fi
MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL="*" "$ISCC" /Q ${BUNDLE:+/DBundle} "/DAppVersion=$VERSION" installer/tes64.iss
cp dist/TES64-Setup.exe dist/TES64/
(cd dist && rm -f "TES64-$VERSION.zip" && py -3 -c "import shutil,sys; shutil.make_archive(sys.argv[1], 'zip', '.', 'TES64')" "TES64-$VERSION")
echo "packaged $VERSION: dist/TES64/, dist/TES64-Setup.exe, dist/TES64-$VERSION.zip"
