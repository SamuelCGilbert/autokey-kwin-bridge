#!/bin/bash
# Build the plugin against the headers of the KWin version that is installed.
# Usage: SYSROOT=<dir with matching kwin-dev/kf6 headers> ./build.sh
set -eu
cd "$(dirname "$0")"
SR=${SYSROOT:-$HOME/src/x11-to-wayland/sysroot}
OUT=${OUT:-build/$(dpkg-query -W -f='${Version}' kwin-wayland | sed 's/[^A-Za-z0-9.]/_/g')}
mkdir -p "$OUT"
MOC=/usr/lib/qt6/libexec/moc
LIB=/usr/lib/x86_64-linux-gnu
INC="-I. -I$OUT -I$SR/usr/include/x86_64-linux-gnu/qt6 -I$SR/usr/include/x86_64-linux-gnu/qt6/QtQml -I$SR/usr/include/kwin -I$SR/usr/include/KF6/KConfig -I$SR/usr/include/KF6/KConfigCore -I$SR/usr/include/KF6/KCoreAddons -I$SR/usr/include/KF6/KWindowSystem -I$SR/usr/include"
QT="$(pkg-config --cflags Qt6Core Qt6Gui Qt6DBus Qt6Widgets)"
$MOC $INC autokeybridge.h -o "$OUT/moc_autokeybridge.cpp"
$MOC $INC main.cpp -o "$OUT/main.moc"
g++ -std=c++23 -O2 -fPIC -shared -fvisibility=default -DQT_NO_KEYWORDS $INC $QT \
  autokeybridge.cpp main.cpp "$OUT/moc_autokeybridge.cpp" \
  -o "$OUT/autokeybridge.so" \
  ${LIBKWIN:-$LIB/libkwin.so.6} $(pkg-config --libs Qt6Core Qt6Gui Qt6DBus) -lxkbcommon
echo "$OUT/autokeybridge.so"
