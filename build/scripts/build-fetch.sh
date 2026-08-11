#!/usr/bin/env bash

# build-fetch.sh — Build KratosOS HTTPS Download Client

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../config/build.conf"

SYSROOT="$KRATOS_SYSROOT"
TOOLS="$KRATOS_TOOLS"
CC="$TOOLS/bin/$TARGET-gcc"

FETCH_SRC="$KRATOS_ROOT/pkg/kratos-fetch.c"
FETCH_OUT="$SYSROOT/usr/bin/kratos-fetch"

echo "========================================"
echo "       KRATOSOS HTTPS CLIENT BUILD"
echo "========================================"
echo "  Target:  $TARGET"
echo "  Sysroot: $SYSROOT"
echo "  CC:      $CC"
echo

if [ ! -f "$CC" ]; then
    echo "[!] Cross-compiler not found: $CC"
    exit 1
fi

# Verify mbedTLS is available
if [ ! -f "$SYSROOT/usr/lib/libmbedtls.a" ]; then
    echo "[!] mbedTLS not found in sysroot. Run build-mbedtls.sh first."
    exit 1
fi

mkdir -p "$SYSROOT/usr/bin"

echo "[+] Compiling /usr/bin/kratos-fetch..."
"$CC" \
    --sysroot="$SYSROOT" \
    -O2 \
    -Wall \
    -Wextra \
    -std=gnu11 \
    -fstack-protector-strong \
    -D_FORTIFY_SOURCE=2 \
    -fPIE \
    -o "$FETCH_OUT" \
    "$FETCH_SRC" \
    -lmbedtls -lmbedx509 -lmbedcrypto \
    -pie \
    -Wl,-z,relro,-z,now
echo "[✓] kratos-fetch compiled."

echo
echo "Installed binary:"
ls -lh "$FETCH_OUT"
echo
echo "[✓] KratosOS HTTPS client built successfully."
