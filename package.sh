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
cp README.md LICENSE dist/TES64/ 2>/dev/null || true

ISCC=""
for c in "$LOCALAPPDATA/Programs/Inno Setup 6/ISCC.exe" "/c/Program Files (x86)/Inno Setup 6/ISCC.exe" "/c/Program Files/Inno Setup 6/ISCC.exe"; do
  [ -f "$c" ] && ISCC="$c" && break
done
if [ -z "$ISCC" ]; then
  echo "packaged dist/TES64/ (no installer: Inno Setup 6 isn't installed)"
  exit 1
fi
MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL="*" "$ISCC" /Q "/DAppVersion=$VERSION" installer/tes64.iss
echo "packaged $VERSION: dist/TES64/, dist/TES64-Setup.exe"
