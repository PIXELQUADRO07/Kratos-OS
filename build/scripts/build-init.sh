#!/usr/bin/env bash

# build-init.sh — Build KratosOS init system, shutdown tools, kratos-devd, kratos-net, login, and passwd

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../config/build.conf"

SYSROOT="$KRATOS_SYSROOT"
TOOLS="$KRATOS_TOOLS"
CC="$TOOLS/bin/$TARGET-gcc"

INIT_SRC="$KRATOS_ROOT/init/init.c"
SHUTDOWN_SRC="$KRATOS_ROOT/init/shutdown.c"
DEVD_SRC="$KRATOS_ROOT/init/kratos-devd.c"
NET_SRC="$KRATOS_ROOT/init/kratos-net.c"
LOGIN_SRC="$KRATOS_ROOT/init/login.c"
PASSWD_SRC="$KRATOS_ROOT/init/passwd.c"

INIT_OUT="$SYSROOT/sbin/init"
SHUTDOWN_OUT="$SYSROOT/sbin/shutdown"
DEVD_OUT="$SYSROOT/sbin/kratos-devd"
NET_OUT="$SYSROOT/sbin/kratos-net"
LOGIN_OUT="$SYSROOT/bin/login"
PASSWD_OUT="$SYSROOT/usr/bin/passwd"

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

mkdir -p "$SYSROOT/bin"
mkdir -p "$SYSROOT/sbin"
mkdir -p "$SYSROOT/usr/bin"

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

echo "[+] Compiling /sbin/kratos-net..."
"$CC" \
    --sysroot="$SYSROOT" \
    -O2 \
    -Wall \
    -Wextra \
    -std=gnu11 \
    -o "$NET_OUT" \
    "$NET_SRC"
echo "[✓] kratos-net compiled."

echo "[+] Compiling /bin/login..."
"$CC" \
    --sysroot="$SYSROOT" \
    -O2 \
    -Wall \
    -Wextra \
    -std=gnu11 \
    -o "$LOGIN_OUT" \
    "$LOGIN_SRC" \
    "$KRATOS_ROOT/init/kratos-crypt.c"
echo "[✓] login compiled."

echo "[+] Compiling /usr/bin/passwd..."
"$CC" \
    --sysroot="$SYSROOT" \
    -O2 \
    -Wall \
    -Wextra \
    -std=gnu11 \
    -o "$PASSWD_OUT" \
    "$PASSWD_SRC" \
    "$KRATOS_ROOT/init/kratos-crypt.c"
echo "[✓] passwd compiled."

echo "[+] Creating symlinks for reboot, poweroff, halt..."
ln -sf shutdown "$SYSROOT/sbin/reboot"
ln -sf shutdown "$SYSROOT/sbin/poweroff"
ln -sf shutdown "$SYSROOT/sbin/halt"

echo "[✓] Symlinks created."
echo
echo "Installed binaries:"
ls -lh "$INIT_OUT" "$SHUTDOWN_OUT" "$DEVD_OUT" "$NET_OUT" "$LOGIN_OUT" "$PASSWD_OUT"
ls -la "$SYSROOT/sbin/reboot" "$SYSROOT/sbin/poweroff" "$SYSROOT/sbin/halt"

echo
echo "[✓] KratosOS system tools & authentication built successfully."
