#!/usr/bin/env bash

# build-pkg.sh — Build KratosOS Package Manager & Package Tools

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../config/build.conf"

SYSROOT="$KRATOS_SYSROOT"
TOOLS="$KRATOS_TOOLS"
CC="$TOOLS/bin/$TARGET-gcc"

PKG_SRC="$KRATOS_ROOT/pkg/kratos-pkg.c"
CLI_SRC="$KRATOS_ROOT/pkg/kratos-cli.c"
PACK_SRC="$KRATOS_ROOT/pkg/kratos-pack.c"

PKG_OUT="$SYSROOT/usr/libexec/kratos-pkg"
CLI_OUT="$SYSROOT/usr/bin/kratos"
PACK_OUT="$SYSROOT/usr/bin/kratos-pack"

echo "========================================"
echo "       KRATOSOS PACKAGE TOOLS BUILD"
echo "========================================"
echo "  Target:  $TARGET"
echo "  Sysroot: $SYSROOT"
echo "  CC:      $CC"
echo

if [ ! -f "$CC" ]; then
    echo "[!] Cross-compiler not found: $CC"
    exit 1
fi

mkdir -p "$SYSROOT/usr/libexec"
mkdir -p "$SYSROOT/usr/bin"
mkdir -p "$SYSROOT/var/lib/kratos/db/packages"
mkdir -p "$SYSROOT/var/lib/kratos/db/files"
mkdir -p "$SYSROOT/var/lib/kratos/cache"

echo "[+] Compiling /usr/libexec/kratos-pkg (Engine)..."
"$CC" \
    --sysroot="$SYSROOT" \
    -O2 \
    -Wall \
    -Wextra \
    -std=gnu11 \
    -o "$PKG_OUT" \
    "$PKG_SRC"
echo "[✓] kratos-pkg engine compiled."

echo "[+] Compiling /usr/bin/kratos (CLI Frontend)..."
"$CC" \
    --sysroot="$SYSROOT" \
    -O2 \
    -Wall \
    -Wextra \
    -std=gnu11 \
    -o "$CLI_OUT" \
    "$CLI_SRC"
echo "[✓] kratos CLI frontend compiled."

echo "[+] Compiling /usr/bin/kratos-pack (Package Builder)..."
"$CC" \
    --sysroot="$SYSROOT" \
    -O2 \
    -Wall \
    -Wextra \
    -std=gnu11 \
    -o "$PACK_OUT" \
    "$PACK_SRC"
echo "[✓] kratos-pack tool compiled."

echo
echo "Installed package binaries:"
ls -lh "$PKG_OUT" "$CLI_OUT" "$PACK_OUT"

echo
echo "[✓] KratosOS Package Manager built successfully."
