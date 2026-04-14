#!/usr/bin/env bash
# install.sh — Quick install script for nfm
# Works on Ubuntu/Debian and macOS (with Homebrew)
set -e

UNAME=$(uname)
PREFIX="${PREFIX:-/usr/local}"

echo "=== nfm installer ==="
echo "Installing to: $PREFIX"
echo ""

# Check / install build dependencies
if [ "$UNAME" = "Darwin" ]; then
    if ! command -v brew >/dev/null 2>&1; then
        echo "Homebrew not found. Please install it from https://brew.sh"
        exit 1
    fi
    echo "Installing dependencies via Homebrew..."
    brew install ncurses ffmpeg 2>/dev/null || true
else
    echo "Installing dependencies via apt..."
    sudo apt-get update -qq
    sudo apt-get install -y gcc make libncurses-dev libncursesw-dev ffmpeg
fi

echo ""
echo "Building nfm..."
make clean
make

echo ""
echo "Installing..."
sudo make install PREFIX="$PREFIX"

echo ""
echo "Done! Run 'nfm' from any directory."
