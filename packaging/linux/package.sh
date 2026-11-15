#!/bin/sh
# Builds the Linux release package: an AppImage with Qt and ffmpeg inside.
#
#   packaging/linux/package.sh [build-dir]
#
# Run after `cmake --preset dist && cmake --build --preset dist`, with the Qt to deploy on the
# PATH (qmake), ffmpeg on the PATH (FFMPEG), and linuxdeploy and linuxdeploy-plugin-qt on the
# PATH. Writes dist/SatReplay-<version>-Linux-x86_64.AppImage.
set -eu

cd "$(dirname "$0")/../.."
BUILD="${1:-build/dist}"
FFMPEG="${FFMPEG:-$(command -v ffmpeg)}"
VERSION=$(sed -n 's/^ *VERSION \([0-9.]*\)$/\1/p' CMakeLists.txt | head -1)
APPDIR="$BUILD/AppDir"

rm -rf "$APPDIR"
cmake --install "$BUILD" --prefix "$APPDIR/usr"
# ffmpeg goes next to the executable, where the app looks for it first.
cp "$FFMPEG" "$APPDIR/usr/bin/ffmpeg"

export QMAKE="${QMAKE:-$(command -v qmake6 || command -v qmake)}"
export EXTRA_PLATFORM_PLUGINS="libqxcb.so;libqwayland-generic.so"
export LDAI_OUTPUT="SatReplay-${VERSION}-Linux-x86_64.AppImage"
linuxdeploy --appdir "$APPDIR" \
    --executable "$APPDIR/usr/bin/satreplay" \
    --executable "$APPDIR/usr/bin/ffmpeg" \
    --desktop-file "$APPDIR/usr/share/applications/satreplay.desktop" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/satreplay.png" \
    --plugin qt --output appimage

mkdir -p dist
mv "$LDAI_OUTPUT" dist/
echo "dist/$LDAI_OUTPUT"
