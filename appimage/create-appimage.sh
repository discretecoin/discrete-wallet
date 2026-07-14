#!/bin/bash
set -euo pipefail

APP=DiscreteWallet
APPDIR=AppDir

# Download tools
if ! test -f linuxdeploy-x86_64.AppImage; then
    wget https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
fi

if ! test -f linuxdeploy-plugin-qt-x86_64.AppImage; then
    wget https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
fi

chmod +x *.AppImage

# Clean AppDir
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin"
mkdir -p "$APPDIR/usr/share/icons/hicolor/256x256/apps"
mkdir -p "$APPDIR/usr/share/applications"
mkdir -p "$APPDIR/usr/share/discrete/languages"

# Copy binary
cp "../build/release/$APP" "$APPDIR/usr/bin/"

# Copy desktop file
cp ../build/release/discretewallet.desktop "$APPDIR/usr/share/applications/discretewallet.desktop"

# Copy icon
cp ../src/images/discrete.png \
   "$APPDIR/usr/share/icons/hicolor/256x256/apps/discrete.png"

# Copy translations
cp ../build/release/languages/*.qm \
   "$APPDIR/usr/share/discrete/languages/"

if command -v qmake6 >/dev/null 2>&1; then
  export QMAKE="$(command -v qmake6)"
elif [ -x /usr/lib/qt6/bin/qmake6 ]; then
  export QMAKE=/usr/lib/qt6/bin/qmake6
elif [ -x /usr/lib/qt6/bin/qmake ]; then
  export QMAKE=/usr/lib/qt6/bin/qmake
elif command -v qmake >/dev/null 2>&1; then
  export QMAKE="$(command -v qmake)"
else
  echo "Qt6 qmake was not found; install Qt 6 development tools." >&2
  exit 1
fi

# Define extra modules to ensure SVG support is bundled
export EXTRA_QT_MODULES="svg"

# Build AppImage
./linuxdeploy-x86_64.AppImage \
  --appdir "$APPDIR" \
  --executable "$APPDIR/usr/bin/$APP" \
  --desktop-file "$APPDIR/usr/share/applications/discretewallet.desktop" \
  --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/discrete.png" \
  --plugin qt \
  --output appimage
