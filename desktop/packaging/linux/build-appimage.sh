#!/usr/bin/env bash
# Builds Quarzlampe-x86_64.AppImage from already-built release binaries.
# Verified live on this dev machine (Manjaro): the daemon starts correctly
# from inside the mounted AppImage and finds the console binary as a
# sibling at its mounted path - see desktop/PLAN.md for the exact test.
#
# Usage: build-appimage.sh <path-to-target/release> <output-dir>
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

APPDIR="$WORK_DIR/AppDir"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/share/applications" "$APPDIR/usr/share/icons/hicolor/256x256/apps"

cp "$RELEASE_DIR/quarzlampe-tray" "$APPDIR/usr/bin/"
cp "$RELEASE_DIR/quarzlampe-console" "$APPDIR/usr/bin/"

cp "$SCRIPT_DIR/quarzlampe.png" "$APPDIR/usr/share/icons/hicolor/256x256/apps/quarzlampe.png"
cp "$SCRIPT_DIR/quarzlampe.png" "$APPDIR/quarzlampe.png"

cp "$SCRIPT_DIR/quarzlampe.desktop" "$APPDIR/usr/share/applications/quarzlampe.desktop"
cp "$SCRIPT_DIR/quarzlampe.desktop" "$APPDIR/quarzlampe.desktop"

cp "$SCRIPT_DIR/AppRun" "$APPDIR/AppRun"
chmod +x "$APPDIR/AppRun"

if [ ! -x "$WORK_DIR/appimagetool" ]; then
  curl -sL -o "$WORK_DIR/appimagetool" \
    "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage"
  chmod +x "$WORK_DIR/appimagetool"
fi

mkdir -p "$OUT_DIR"
ARCH=x86_64 "$WORK_DIR/appimagetool" --appimage-extract-and-run "$APPDIR" "$OUT_DIR/Quarzlampe-x86_64.AppImage"
echo "Built $OUT_DIR/Quarzlampe-x86_64.AppImage"
