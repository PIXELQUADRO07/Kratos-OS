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
FETCH_SRC="$KRATOS_ROOT/pkg/kratos-fetch.c"

PKG_OUT="$SYSROOT/usr/libexec/kratos-pkg"
CLI_OUT="$SYSROOT/usr/bin/kratos"
PACK_OUT="$SYSROOT/usr/bin/kratos-pack"
FETCH_OUT="$SYSROOT/usr/bin/kratos-fetch"

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
mkdir -p "$SYSROOT/var/lib/kratos/repo-cache"

echo "[+] Compiling /usr/libexec/kratos-pkg (Engine)..."
"$CC" \
    --sysroot="$SYSROOT" \
    -O2 \
    -Wall \
    -fstack-protector-strong \
    -D_FORTIFY_SOURCE=2 \
    -Wextra \
    -std=gnu11 \
    -I"$KRATOS_ROOT/pkg" \
    -o "$PKG_OUT" \
    "$PKG_SRC" \
    "$KRATOS_ROOT/pkg/kratos-repo.c" \
    "$KRATOS_ROOT/pkg/kratos-sign.c" \
    "$KRATOS_ROOT/pkg/kratos-tar.c" \
    "$KRATOS_ROOT/pkg/kratos-sha256.c" \
    "$KRATOS_ROOT/pkg/kratos-deps.c" \
    "$KRATOS_ROOT/pkg/kratos-json.c" \
    -lmbedtls -lmbedx509 -lmbedcrypto \
    -fPIE -pie \
    -Wl,-z,relro,-z,now
echo "[✓] kratos-pkg engine compiled."

echo "[+] Compiling /usr/bin/kratos (CLI Frontend)..."
"$CC" \
    --sysroot="$SYSROOT" \
    -O2 \
    -Wall \
    -fstack-protector-strong \
    -D_FORTIFY_SOURCE=2 \
    -Wextra \
    -std=gnu11 \
    -I"$KRATOS_ROOT/pkg" \
    -o "$CLI_OUT" \
    "$CLI_SRC" \
    -fPIE -pie \
    -Wl,-z,relro,-z,now
echo "[✓] kratos CLI frontend compiled."

echo "[+] Compiling /usr/bin/kratos-pack (Package Builder)..."
"$CC" \
    --sysroot="$SYSROOT" \
    -O2 \
    -Wall \
    -fstack-protector-strong \
    -D_FORTIFY_SOURCE=2 \
    -Wextra \
    -std=gnu11 \
    -I"$KRATOS_ROOT/pkg" \
    -o "$PACK_OUT" \
    "$PACK_SRC" \
    "$KRATOS_ROOT/pkg/kratos-tar.c" \
    "$KRATOS_ROOT/pkg/kratos-sha256.c" \
    -fPIE -pie \
    -Wl,-z,relro,-z,now
echo "[✓] kratos-pack tool compiled."

echo "[+] Compiling /usr/bin/kratos-fetch (HTTPS Client)..."
"$CC" \
    --sysroot="$SYSROOT" \
    -O2 \
    -Wall \
    -fstack-protector-strong \
    -D_FORTIFY_SOURCE=2 \
    -Wextra \
    -std=gnu11 \
    -I"$KRATOS_ROOT/pkg" \
    -o "$FETCH_OUT" \
    "$FETCH_SRC" \
    -lmbedtls -lmbedx509 -lmbedcrypto \
    -fPIE -pie \
    -Wl,-z,relro,-z,now
echo "[✓] kratos-fetch compiled."

echo
echo "Installed package binaries:"
ls -lh "$PKG_OUT" "$CLI_OUT" "$PACK_OUT" "$FETCH_OUT"

echo
echo "[✓] KratosOS Package Manager built successfully."
