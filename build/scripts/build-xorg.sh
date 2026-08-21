#!/usr/bin/env bash
# build-xorg.sh — Install and configure X11 Graphics Stack for KratosOS
#
# Copies X11 configurations, drivers and display server scripts into sysroot.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../config/build.conf"
source "$SCRIPT_DIR/../config/versions.conf"

SYSROOT="$KRATOS_SYSROOT"

echo "========================================"
echo "      KRATOSOS X11 STACK CONFIG"
echo "========================================"
echo "  Sysroot: $SYSROOT"
echo

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

if [ -x /etc/live/start-live.sh ]; then
    /etc/live/start-live.sh &
fi
EOF
chmod +x "$SYSROOT/etc/rc.d/99-live"

echo "[✓] X11 environment configured successfully."
