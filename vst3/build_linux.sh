#!/bin/bash
# build_linux.sh -- build the Linux VST3 + validation/screenshot tools.
#   ./build_linux.sh            configure + build everything
# Artefacts: build/PyGamerSPX_artefacts/Release/VST3/PyGamer SPX.vst3
# (copied to dist-linux/), tools at build/SpxHarness + build/SpxScreenshot.
set -euo pipefail
cd "$(dirname "$0")"

for cmd in git cmake g++; do
  command -v "$cmd" >/dev/null 2>&1 || { echo "ERROR: '$cmd' not found." >&2; exit 1; }
done

# JUCE Linux GUI deps (Debian/Ubuntu names); checked here so failures
# are obvious instead of a wall of compiler errors:
MISSING=""
for h in X11/extensions/Xrandr.h X11/extensions/Xinerama.h X11/Xcursor/Xcursor.h X11/extensions/Xcomposite.h alsa/asoundlib.h freetype2/freetype/freetype.h; do
  [ -f "/usr/include/$h" ] || MISSING="$MISSING $h"
done
if [ -n "$MISSING" ]; then
  echo "Missing headers:$MISSING"
  echo "On Debian/Ubuntu: sudo apt install libx11-dev libxrandr-dev libxinerama-dev \\"
  echo "  libxcursor-dev libxcomposite-dev libxdamage-dev libasound2-dev libfreetype-dev"
  exit 1
fi

if [ ! -d third_party/JUCE ]; then
  echo "-- Cloning JUCE 7.0.12 (one-time)..."
  mkdir -p third_party
  git clone --depth 1 --branch 7.0.12 https://github.com/juce-framework/JUCE.git third_party/JUCE
fi

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j "$(nproc)"

VST3_SRC="$(find build -type d -name 'PyGamer SPX.vst3' | head -n 1)"
if [ -n "$VST3_SRC" ]; then
  mkdir -p dist-linux
  rm -rf "dist-linux/PyGamer SPX.vst3"
  cp -R "$VST3_SRC" dist-linux/
  echo "-- Linux VST3 copied to dist-linux/PyGamer SPX.vst3"
fi
echo "-- Done. Tools: build/SpxHarness, build/SpxScreenshot"
