#!/usr/bin/env bash
# Assembles the SD card release zip from the last build: dist/usb-loader-nx-<version>.zip
#   atmosphere/  Atmosphère with USB Loader NX (package3, stratosphere.romfs, config_templates/usbloadernx.ini, ...)
#   switch/      Atmosphère's homebrew + USBLoaderNX-Manager.nro (when the Manager was built)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="${USBL_VERSION:-$(git -C "$ROOT" describe --tags --match 'v*' --dirty 2>/dev/null || echo dev)}"
ATMOSPHERE_ZIP="$(ls -t "$ROOT"/atmosphere/out/nintendo_nx_arm64_armv8a/release/atmosphere-*.zip | head -n1)"
MANAGER_NRO="$ROOT/manager/USBLoaderNX-Manager.nro"

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

unzip -q "$ATMOSPHERE_ZIP" -d "$STAGE"
if [[ -f "$MANAGER_NRO" ]]; then
    mkdir -p "$STAGE/switch"
    cp "$MANAGER_NRO" "$STAGE/switch/"
else
    echo "note: no Manager build, packaging Atmosphère with USB Loader NX only" >&2
fi

mkdir -p "$ROOT/dist"
OUTPUT="$ROOT/dist/usb-loader-nx-$VERSION.zip"
rm -f "$OUTPUT"
(cd "$STAGE" && zip -qr "$OUTPUT" .)
echo "$OUTPUT"
