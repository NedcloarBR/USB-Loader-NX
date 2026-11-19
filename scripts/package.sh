#!/usr/bin/env bash
# Assembles the release zip from the last build: dist/usb-loader-nx-<version>.zip, holding usb-loader-nx/ with
#   SD/         what goes on the SD card: atmosphere/ (Atmosphère with USB Loader NX), switch/ (Atmosphère's homebrew and
#               USBLoaderNX-Manager.nro), bootloader/payloads/fusee.bin and the example theme in config/
#   USB/        what goes on the USB drive: the empty usbloadernx/games folder
#   README.md   the quick guide (from packaging/)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="${USBL_VERSION:-$(git -C "$ROOT" describe --tags --match 'v*' --dirty 2>/dev/null || echo dev)}"
ATMOSPHERE_VERSION="$(cat "$ROOT/ATMOSPHERE_VERSION")"
RELEASE_DIR="$ROOT/atmosphere/out/nintendo_nx_arm64_armv8a/release"
ATMOSPHERE_ZIP="$(ls -t "$RELEASE_DIR"/atmosphere-*.zip | head -n1)"
MANAGER_NRO="$ROOT/manager/USBLoaderNX-Manager.nro"

STAGE="$(mktemp -d)"
trap 'rm -rf "${STAGE:?}"' EXIT
PACKAGE="$STAGE/usb-loader-nx"
SD="$PACKAGE/SD"

mkdir -p "$SD" "$PACKAGE/USB/usbloadernx/games"
unzip -q "$ATMOSPHERE_ZIP" -d "$SD"
mkdir -p "$SD/bootloader/payloads"
cp "$RELEASE_DIR/fusee.bin" "$SD/bootloader/payloads/fusee.bin"

if [[ -f "$MANAGER_NRO" ]]; then
    mkdir -p "$SD/switch" "$SD/config/usbloadernx-manager/themes"
    cp "$MANAGER_NRO" "$SD/switch/"
    cp -r "$ROOT/manager/themes/midnight" "$SD/config/usbloadernx-manager/themes/"
else
    echo "note: no Manager build, packaging Atmosphère with USB Loader NX only" >&2
fi

# Windows line endings, so the guide opens cleanly in Notepad.
crlf() { sed -e "s/{{VERSION}}/$VERSION/g" -e "s/{{ATMOSPHERE}}/$ATMOSPHERE_VERSION/g" -e 's/$/\r/' "$1" > "$2"; }
crlf "$ROOT/packaging/README.md" "$PACKAGE/README.md"
crlf "$ROOT/packaging/games-folder.txt" "$PACKAGE/USB/usbloadernx/games/PUT_YOUR_GAMES_HERE.txt"

mkdir -p "$ROOT/dist"
OUTPUT="$ROOT/dist/usb-loader-nx-$VERSION.zip"
rm -f "$OUTPUT"
(cd "$STAGE" && zip -qr "$OUTPUT" usb-loader-nx)
echo "$OUTPUT"
