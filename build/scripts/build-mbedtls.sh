#!/usr/bin/env bash

# build-mbedtls.sh — Cross-compile mbedTLS for KratosOS sysroot

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../config/build.conf"
source "$SCRIPT_DIR/../config/versions.conf"

SYSROOT="$KRATOS_SYSROOT"
TOOLS="$KRATOS_TOOLS"
CC="$TOOLS/bin/$TARGET-gcc"
AR="$TOOLS/bin/$TARGET-ar"
RANLIB="$TOOLS/bin/$TARGET-ranlib"

SRC_DIR="$KRATOS_SOURCES/mbedtls-${MBEDTLS_VERSION}"
WORK_DIR="$KRATOS_WORK/mbedtls"

echo "========================================"
echo "       KRATOSOS mbedTLS BUILD"
echo "========================================"
echo "  Version: ${MBEDTLS_VERSION}"
echo "  Target:  $TARGET"
echo "  Sysroot: $SYSROOT"
echo "  CC:      $CC"
echo

if [ ! -f "$CC" ]; then
    echo "[!] Cross-compiler not found: $CC"
    exit 1
fi

# Download if not present
MBEDTLS_TAR="$KRATOS_DOWNLOADS/mbedtls-${MBEDTLS_VERSION}.tar.bz2"
if [ ! -f "$MBEDTLS_TAR" ]; then
    echo "[+] Downloading mbedTLS ${MBEDTLS_VERSION}..."
    mkdir -p "$KRATOS_DOWNLOADS"
    curl -L --progress-bar --retry 3 \
        -o "${MBEDTLS_TAR}.part" \
        "https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-${MBEDTLS_VERSION}/mbedtls-${MBEDTLS_VERSION}.tar.bz2"
    mv "${MBEDTLS_TAR}.part" "$MBEDTLS_TAR"
    echo "[✓] Downloaded."
fi

# Always extract fresh. Previously this only extracted if $SRC_DIR was
# missing, and relied on `make distclean` to reset state between runs —
# but that left stale .o files behind (built with old CFLAGS) that got
# silently re-archived into a "freshly timestamped" .a without ever being
# recompiled, and the `|| true` on distclean masked the failure. Wiping
# and re-extracting the source tree removes any possibility of that.
echo "[+] Extracting mbedTLS (fresh copy, discarding any previous build state)..."
rm -rf "$SRC_DIR"
mkdir -p "$KRATOS_SOURCES"
tar -xjf "$MBEDTLS_TAR" -C "$KRATOS_SOURCES"
echo "[✓] Extracted."

# Clean and create work directory
rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR"

# Build using Makefile (mbedTLS supports plain make without cmake)
echo "[+] Compiling mbedTLS (static libraries)..."
cd "$SRC_DIR"

# The build runs directly inside $SRC_DIR (not $WORK_DIR), so re-running
# this script after changing CFLAGS would otherwise reuse objects from
# the previous run instead of starting clean. mbedTLS supports distclean;
# ignore failure on a pristine checkout where there's nothing to clean.
make distclean >/dev/null 2>&1 || true

# mbedTLS can be built with just make, specifying CC and AR.
# Threading support (MBEDTLS_THREADING_C/PTHREAD) is intentionally left
# out: kratos-fetch is a single-threaded CLI tool, so there's no shared
# RNG/entropy context across threads to protect, and enabling it would
# require -lpthread that isn't linked in build-fetch.sh / build-pkg.sh.
make -j"${KRATOS_JOBS:-$(nproc)}" \
    CC="$CC --sysroot=$SYSROOT" \
    AR="$AR" \
    CFLAGS="-O2 -fPIE -fstack-protector-strong -D_FORTIFY_SOURCE=2" \
    LDFLAGS="--sysroot=$SYSROOT" \
    lib

echo "[✓] mbedTLS compiled."

# Install into sysroot
echo "[+] Installing into sysroot..."
mkdir -p "$SYSROOT/usr/lib"
mkdir -p "$SYSROOT/usr/include"

cp library/libmbedtls.a    "$SYSROOT/usr/lib/"
cp library/libmbedcrypto.a "$SYSROOT/usr/lib/"
cp library/libmbedx509.a   "$SYSROOT/usr/lib/"

# ranlib the static libs
"$RANLIB" "$SYSROOT/usr/lib/libmbedtls.a"
"$RANLIB" "$SYSROOT/usr/lib/libmbedcrypto.a"
"$RANLIB" "$SYSROOT/usr/lib/libmbedx509.a"

cp -r include/mbedtls "$SYSROOT/usr/include/"
cp -r include/psa     "$SYSROOT/usr/include/"

echo "[✓] mbedTLS installed into sysroot."

echo
echo "Installed libraries:"
ls -lh "$SYSROOT/usr/lib/libmbed"*
echo
echo "[✓] mbedTLS build complete."
