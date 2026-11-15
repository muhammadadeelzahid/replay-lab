#!/bin/sh
# Builds the macOS release package: "SatReplay.app" with Qt and ffmpeg inside, zipped.
#
#   packaging/macos/package.sh [build-dir]
#
# Run after building the dist preset (`cmake --preset dist && cmake --build --preset dist`)
# against an official Qt build (the Qt installer or aqtinstall; Homebrew's Qt cannot be
# deployed into a bundle). Uses the macdeployqt of that Qt (MACDEPLOYQT, or the one on the
# PATH) and the ffmpeg on the PATH (FFMPEG), whose libraries are copied into the bundle too.
# Writes dist/SatReplay-<version>-macOS-<arch>.zip.
set -eu

cd "$(dirname "$0")/../.."
BUILD="${1:-build/dist}"
APP="$BUILD/app/SatReplay.app"
MACDEPLOYQT="${MACDEPLOYQT:-macdeployqt}"
FFMPEG="${FFMPEG:-$(command -v ffmpeg)}"
VERSION=$(sed -n 's/^ *VERSION \([0-9.]*\)$/\1/p' CMakeLists.txt | head -1)
ARCH=$(uname -m)
OUT="dist/SatReplay-${VERSION}-macOS-${ARCH}.zip"

test -d "$APP" || { echo "build the dist preset first" >&2; exit 1; }
test -x "$FFMPEG" || { echo "ffmpeg not found" >&2; exit 1; }

# ffmpeg goes next to the executable, where the app looks for it first.
cp "$FFMPEG" "$APP/Contents/MacOS/ffmpeg"
chmod 755 "$APP/Contents/MacOS/ffmpeg"

# Qt frameworks and plugins, and every non-system library the app or ffmpeg loads.
"$MACDEPLOYQT" "$APP" -always-overwrite -executable="$APP/Contents/MacOS/ffmpeg"

# Run paths into the build machine's package manager would let the app load libraries from
# there instead of from the bundle; remove them.
find "$APP/Contents" -type f \( -name "*.dylib" -o -perm -111 \) | while read -r binary; do
    otool -l "$binary" 2>/dev/null | awk '/LC_RPATH/ {getline; getline; print $2}' \
        | grep -E "^/(opt/homebrew|usr/local|Users|private)" | while read -r rpath; do
            install_name_tool -delete_rpath "$rpath" "$binary" 2>/dev/null || true
        done
done

# Nothing may still refer to a library outside the bundle or the system.
if find "$APP/Contents" -type f \( -name "*.dylib" -o -perm -111 \) -exec otool -L {} \; 2>/dev/null \
    | grep -E "^[[:space:]]+/(opt|usr/local|Users|private)"; then
    echo "the bundle still refers to libraries outside it" >&2
    exit 1
fi

# Ad-hoc signature: Apple silicon does not run unsigned code. (Not notarised: on first launch,
# right-click the app and choose Open.)
codesign --force --deep --sign - "$APP"

mkdir -p dist
rm -f "$OUT"
ditto -c -k --keepParent "$APP" "$OUT"
echo "$OUT"
