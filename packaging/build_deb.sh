#!/usr/bin/env bash
# Build a minimal Debian .deb package for nfm
set -e

PKG="nfm"
VERSION="1.0.0"
ARCH=$(dpkg --print-architecture 2>/dev/null || echo "amd64")
PKGDIR="$(pwd)/build/deb/${PKG}_${VERSION}_${ARCH}"

echo "Building deb package: ${PKG}_${VERSION}_${ARCH}.deb"

rm -rf "$PKGDIR"
mkdir -p "$PKGDIR/usr/bin"
mkdir -p "$PKGDIR/usr/share/nfm/presets"
mkdir -p "$PKGDIR/DEBIAN"

# Binary
cp ./nfm "$PKGDIR/usr/bin/nfm"
chmod 755 "$PKGDIR/usr/bin/nfm"

# Presets
cp presets/*.preset "$PKGDIR/usr/share/nfm/presets/"

# Control file (fill in architecture)
sed "s/^Architecture:.*/Architecture: ${ARCH}/" \
    packaging/debian/control > "$PKGDIR/DEBIAN/control"

# Build package
dpkg-deb --build "$PKGDIR" "build/${PKG}_${VERSION}_${ARCH}.deb"
echo "Created: build/${PKG}_${VERSION}_${ARCH}.deb"
