#!/usr/bin/env bash

# build-init.sh — Build KratosOS init system, shutdown tools, and kratos-devd

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../config/build.conf"

SYSROOT="$KRATOS_SYSROOT"
TOOLS="$KRATOS_TOOLS"
CC="$TOOLS/bin/$TARGET-gcc"

INIT_SRC="$KRATOS_ROOT/init/init.c"
SHUTDOWN_SRC="$KRATOS_ROOT/init/shutdown.c"
DEVD_SRC="$KRATOS_ROOT/init/kratos-devd.c"

INIT_OUT="$SYSROOT/sbin/init"
SHUTDOWN_OUT="$SYSROOT/sbin/shutdown"
DEVD_OUT="$SYSROOT/sbin/kratos-devd"

echo "========================================"
echo "       KRATOSOS SYSTEM BUILD"
echo "========================================"
echo "  Target:  $TARGET"
echo "  Sysroot: $SYSROOT"
echo "  CC:      $CC"
echo

if [ ! -f "$CC" ]; then
    echo "[!] Cross-compiler not found: $CC"
    exit 1
fi

mkdir -p "$SYSROOT/sbin"

echo "[+] Compiling /sbin/init..."
"$CC" \
    --sysroot="$SYSROOT" \
    -O2 \
    -Wall \
    -Wextra \
    -std=gnu11 \
    -o "$INIT_OUT" \
    "$INIT_SRC"

echo "[✓] init compiled."

echo "[+] Compiling /sbin/shutdown..."
"$CC" \
    --sysroot="$SYSROOT" \
    -O2 \
    -Wall \
    -Wextra \
    -std=gnu11 \
    -o "$SHUTDOWN_OUT" \
    "$SHUTDOWN_SRC"

echo "[✓] shutdown compiled."

echo "[+] Compiling /sbin/kratos-devd..."
"$CC" \
    --sysroot="$SYSROOT" \
    -O2 \
    -Wall \
    -Wextra \
    -std=gnu11 \
    -o "$DEVD_OUT" \
    "$DEVD_SRC"

echo "[✓] kratos-devd compiled."

echo "[+] Creating symlinks for reboot, poweroff, halt..."
ln -sf shutdown "$SYSROOT/sbin/reboot"
ln -sf shutdown "$SYSROOT/sbin/poweroff"
ln -sf shutdown "$SYSROOT/sbin/halt"

echo "[✓] Symlinks created."
echo
echo "Installed binaries:"
ls -lh "$SYSROOT/sbin/init" "$SYSROOT/sbin/shutdown" "$SYSROOT/sbin/kratos-devd"
ls -la "$SYSROOT/sbin/reboot" "$SYSROOT/sbin/poweroff" "$SYSROOT/sbin/halt"

echo
echo "[✓] KratosOS init & devd built successfully."
