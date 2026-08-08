#!/usr/bin/env bash

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

source "$SCRIPT_DIR/../config/build.conf"
source "$SCRIPT_DIR/../config/versions.conf"

PACKAGE="linux"
VERSION="$LINUX_VERSION"

ARCHIVE="$KRATOS_DOWNLOADS/linux-$VERSION.tar.xz"
SOURCE_DIR="$KRATOS_SOURCES/linux-$VERSION"

URL="https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-$VERSION.tar.xz"

echo "================================"
echo "     KRATOSOS LINUX HEADERS"
echo "================================"
echo
echo "Version: $VERSION"
echo "Sysroot: $KRATOS_SYSROOT"
echo

mkdir -p "$KRATOS_DOWNLOADS"
mkdir -p "$KRATOS_SOURCES"

if [ ! -f "$ARCHIVE" ]; then
    echo "[+] Downloading Linux..."
    curl -L "$URL" -o "$ARCHIVE"
else
    echo "[+] Linux archive already exists."
fi

if [ ! -d "$SOURCE_DIR" ]; then
    echo "[+] Extracting Linux..."
    tar -xf "$ARCHIVE" -C "$KRATOS_SOURCES"
else
    echo "[+] Linux sources already extracted."
fi

echo "[+] Installing Linux userspace headers..."

cd "$SOURCE_DIR"

make ARCH=x86_64 headers_install \
    INSTALL_HDR_PATH="$KRATOS_SYSROOT/usr"

echo
echo "[+] Linux headers installed successfully."
echo
echo "Headers:"
echo "  $KRATOS_SYSROOT/usr/include"
