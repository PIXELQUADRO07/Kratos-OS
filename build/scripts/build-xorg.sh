#!/usr/bin/env bash
# build-xorg.sh — Install and configure X11 Graphics Stack for KratosOS
#
# Copies X11 configurations, drivers and display server scripts into sysroot.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../config/build.conf"
source "$SCRIPT_DIR/../config/versions.conf"

SYSROOT="$KRATOS_SYSROOT"
TOOLS="$KRATOS_TOOLS"
CC="$TOOLS/bin/$TARGET-gcc"

VTSWITCH_SRC="$KRATOS_ROOT/init/kratos-vtswitch.c"
VTSWITCH_OUT="$SYSROOT/sbin/kratos-vtswitch"

echo "========================================"
echo "      KRATOSOS X11 STACK CONFIG"
echo "========================================"
echo "  Sysroot: $SYSROOT"
echo

# 0. Compile kratos-vtswitch — see kratos-vtswitch.c for why this exists:
#    Xorg cannot switch to its own VT when run as a non-root user without
#    systemd-logind (both true here), so start-live.sh needs a root-side
#    helper to do it instead.
if [ ! -f "$CC" ]; then
    echo "[!] Cross-compiler not found: $CC"
    exit 1
fi

if [ -f "$VTSWITCH_SRC" ]; then
    echo "[+] Compiling /sbin/kratos-vtswitch..."
    mkdir -p "$SYSROOT/sbin"
    "$CC" \
        --sysroot="$SYSROOT" \
        -O2 \
        -Wall \
        -Wextra \
        -std=gnu11 \
        -fstack-protector-strong \
        -D_FORTIFY_SOURCE=2 \
        -fPIE \
        -o "$VTSWITCH_OUT" \
        "$VTSWITCH_SRC" \
        -pie \
        -Wl,-z,relro,-z,now
    echo "[✓] kratos-vtswitch compiled."
fi

# 1. Ensure required X11 directories exist in sysroot
mkdir -p "$SYSROOT/etc/X11/xorg.conf.d"
mkdir -p "$SYSROOT/etc/X11/xinit"
mkdir -p "$SYSROOT/usr/share/X11/xkb"
mkdir -p "$SYSROOT/var/lib/xkb"
mkdir -p "$SYSROOT/etc/live"

# 2. Copy live X11 configurations
if [ -f "$KRATOS_ROOT/config/live/xorg.conf" ]; then
    echo "[+] Installing /etc/X11/xorg.conf..."
    cp "$KRATOS_ROOT/config/live/xorg.conf" "$SYSROOT/etc/X11/xorg.conf"
fi

if [ -f "$KRATOS_ROOT/config/live/xinitrc" ]; then
    echo "[+] Installing /etc/live/xinitrc..."
    cp "$KRATOS_ROOT/config/live/xinitrc" "$SYSROOT/etc/live/xinitrc"
    chmod +x "$SYSROOT/etc/live/xinitrc"
fi

if [ -f "$KRATOS_ROOT/config/live/start-live.sh" ]; then
    echo "[+] Installing /etc/live/start-live.sh..."
    cp "$KRATOS_ROOT/config/live/start-live.sh" "$SYSROOT/etc/live/start-live.sh"
    chmod +x "$SYSROOT/etc/live/start-live.sh"
fi

# 3. Add Live session rc.d service to launch live environment on boot
mkdir -p "$SYSROOT/etc/rc.d"
cat > "$SYSROOT/etc/rc.d/99-live" <<'EOF'
#!/bin/bash
# /etc/rc.d/99-live — Launch Live graphical session if in Live boot mode

if grep -q "kratos.live" /proc/cmdline; then
    if [ -x /etc/live/start-live.sh ]; then
        echo "[Live] KratosOS Live parameter detected, starting X11..."
        /etc/live/start-live.sh &
    fi
fi
EOF
chmod +x "$SYSROOT/etc/rc.d/99-live"

echo "[✓] X11 environment configured successfully."
