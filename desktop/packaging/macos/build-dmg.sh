#!/usr/bin/env bash
# Builds Quarzlampe.dmg from already-built release binaries: a real
# Quarzlampe.app bundle (not loose binaries), ad-hoc signed (free - see
# desktop/PLAN.md for why that's not the same as real Gatekeeper
# notarization), inside a disk image with a symlink to /Applications for
# the standard drag-to-install flow.
#
# Only uses tools already on every macOS runner (sips, iconutil, hdiutil,
# codesign) - no third-party packaging tool. Unverified: no macOS machine
# in this dev environment to actually run this.
#
# Usage: build-dmg.sh <path-to-target/release> <output-dir>
set -euo pipefail

if [ "$#" -ne 2 ]; then
  echo "Usage: $0 <path-to-target/release> <output-dir>" >&2
  exit 1
fi

RELEASE_DIR="$1"
OUT_DIR="$2"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

APP="$WORK_DIR/Quarzlampe.app"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"

cp "$RELEASE_DIR/quarzlampe-tray" "$APP/Contents/MacOS/"
cp "$RELEASE_DIR/quarzlampe-console" "$APP/Contents/MacOS/"
cp "$SCRIPT_DIR/Info.plist" "$APP/Contents/Info.plist"

# Build a real multi-resolution .icns from the one source PNG (only
# `sips`/`iconutil`, both built into macOS - no ImageMagick or similar).
ICONSET="$WORK_DIR/quarzlampe.iconset"
mkdir -p "$ICONSET"
SOURCE_PNG="$SCRIPT_DIR/../../app/assets/icon.png"
for spec in "16:icon_16x16" "32:icon_16x16@2x" "32:icon_32x32" "64:icon_32x32@2x" \
            "128:icon_128x128" "256:icon_128x128@2x" "256:icon_256x256" \
            "512:icon_256x256@2x" "512:icon_512x512" "1024:icon_512x512@2x"; do
  size="${spec%%:*}"
  name="${spec##*:}"
  sips -z "$size" "$size" "$SOURCE_PNG" --out "$ICONSET/$name.png" >/dev/null
done
iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/quarzlampe.icns"

# Free, no Apple Developer account needed - satisfies Apple Silicon's
# "must carry some signature to execute" requirement, does not remove
# Gatekeeper's download warning (no free way to do that - see PLAN.md).
codesign --force --deep --sign - "$APP"

mkdir -p "$OUT_DIR"
DMG_STAGING="$WORK_DIR/dmg-staging"
mkdir -p "$DMG_STAGING"
cp -R "$APP" "$DMG_STAGING/"
ln -s /Applications "$DMG_STAGING/Applications"

hdiutil create -volname "Quarzlampe" -srcfolder "$DMG_STAGING" -ov -format UDZO "$OUT_DIR/Quarzlampe.dmg"
echo "Built $OUT_DIR/Quarzlampe.dmg"
