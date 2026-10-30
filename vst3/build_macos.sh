#!/bin/bash
# build_macos.sh -- build PyGamer SPX (VST3) on macOS and install it
# for Ableton Live. One command:
#
#     ./build_macos.sh              # Intel (x86_64) build
#     ARCH=universal ./build_macos.sh   # universal x86_64+arm64 build
#
# NOTE: this script was written on a Linux machine and has NOT been
# run on a real Mac. It is written defensively (set -e, explicit
# checks with plain-English fixes) -- if a step fails it stops and
# tells you what to install. Requirements: macOS with internet
# (first run clones JUCE), Xcode Command Line Tools, CMake.
set -euo pipefail
cd "$(dirname "$0")"

ARCH="${ARCH:-x86_64}"
case "$ARCH" in
  x86_64)   OSX_ARCH="x86_64" ;;
  universal) OSX_ARCH="x86_64;arm64" ;;
  *) echo "ERROR: ARCH must be 'x86_64' or 'universal' (got '$ARCH')" >&2; exit 1 ;;
esac

echo "== PyGamer SPX macOS build (arch: $OSX_ARCH) =="

# --- platform check ---------------------------------------------------
if [ "$(uname -s)" != "Darwin" ]; then
  echo "ERROR: this script is for macOS. On Linux use build_linux.sh." >&2
  exit 1
fi

# --- Xcode Command Line Tools -----------------------------------------
if ! xcode-select -p >/dev/null 2>&1; then
  echo "Xcode Command Line Tools are not installed."
  echo "Run:  xcode-select --install   (click through the dialog, then re-run this script)"
  exit 1
fi

# --- CMake -------------------------------------------------------------
if ! command -v cmake >/dev/null 2>&1; then
  echo "CMake is not installed. Easiest routes:"
  echo "  - Homebrew:  brew install cmake"
  echo "  - or download the CMake app from https://cmake.org/download/"
  echo "    (open it once, then run: sudo /Applications/CMake.app/Contents/bin/cmake-gui --install)"
  exit 1
fi

# --- git (ships with the CLT) ------------------------------------------
if ! command -v git >/dev/null 2>&1; then
  echo "ERROR: git not found (it should come with the Xcode Command Line Tools)." >&2
  exit 1
fi

# --- JUCE (fetched, not shipped in the project zip) ---------------------
if [ ! -d third_party/JUCE ]; then
  echo "-- Cloning JUCE 7.0.12 (one-time, ~100 MB)..."
  mkdir -p third_party
  git clone --depth 1 --branch 7.0.12 https://github.com/juce-framework/JUCE.git third_party/JUCE
fi

# --- configure + build ---------------------------------------------------
echo "-- Configuring..."
cmake -B build-mac -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_ARCHITECTURES="$OSX_ARCH" \
      -DCMAKE_OSX_DEPLOYMENT_TARGET=10.13

echo "-- Building VST3 (Release)..."
cmake --build build-mac --config Release --target PyGamerSPX_VST3 -j 4

# JUCE puts artefacts under build-mac/PyGamerSPX_artefacts/Release/VST3/
VST3_SRC="$(find build-mac -type d -name 'PyGamer SPX.vst3' | head -n 1)"
if [ -z "$VST3_SRC" ]; then
  echo "ERROR: build finished but no 'PyGamer SPX.vst3' bundle was found." >&2
  exit 1
fi
echo "-- Built: $VST3_SRC"

# --- install --------------------------------------------------------------
DEST="$HOME/Library/Audio/Plug-Ins/VST3"
mkdir -p "$DEST"
echo "-- Installing to $DEST ..."
rm -rf "$DEST/PyGamer SPX.vst3"
cp -R "$VST3_SRC" "$DEST/"
# Locally built bundles are not quarantined, but strip the attribute
# anyway so Gatekeeper never gets a say:
xattr -dr com.apple.quarantine "$DEST/PyGamer SPX.vst3" 2>/dev/null || true

cat <<EOF

== Done. "PyGamer SPX.vst3" is installed. ==

In Ableton Live (10.1 or later):
  1. Preferences (Cmd+,) -> Plug-Ins
  2. "Use VST3 Plug-In System Folders" = On
  3. Click "Rescan" (or hold Option/Alt while clicking Rescan for a deep rescan)
  4. Find "PyGamer SPX" under Plug-Ins -> VST3 -> PyGamerSPX and drag it onto a track.

If Live still won't list it: fully quit and relaunch Live (it only
scans changed folders on launch when rescan-on-startup is enabled).
EOF
