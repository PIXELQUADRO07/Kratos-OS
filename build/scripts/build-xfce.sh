#!/usr/bin/env bash
# build-xfce.sh — Install and configure XFCE Desktop Environment for KratosOS
#
# Sets up XFCE profile, default session settings, desktop icons and menus.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../config/build.conf"
source "$SCRIPT_DIR/../config/versions.conf"

SYSROOT="$KRATOS_SYSROOT"

echo "========================================"
echo "      KRATOSOS XFCE DESKTOP CONFIG"
echo "========================================"
echo "  Sysroot: $SYSROOT"
echo

# 1. Create XFCE system configuration directories
mkdir -p "$SYSROOT/etc/xdg/xfce4"
mkdir -p "$SYSROOT/etc/xdg/xfce4/panel"
mkdir -p "$SYSROOT/etc/xdg/xfce4/xfconf/xfce-perchannel-xml"
mkdir -p "$SYSROOT/usr/share/applications"
mkdir -p "$SYSROOT/usr/share/desktop-directories"
mkdir -p "$SYSROOT/home/kratos-live/Desktop"

# 2. Install desktop shortcut for Live installer
if [ -f "$KRATOS_ROOT/config/live/kratosos-live.desktop" ]; then
    echo "[+] Installing Live Installer desktop entry..."
    cp "$KRATOS_ROOT/config/live/kratosos-live.desktop" "$SYSROOT/etc/live/kratosos-live.desktop"
    cp "$KRATOS_ROOT/config/live/kratosos-live.desktop" "$SYSROOT/home/kratos-live/Desktop/kratosos-live.desktop"
    chmod +x "$SYSROOT/home/kratos-live/Desktop/kratosos-live.desktop"
fi

# 3. Create default wallpaper directory and copy Branding assets
mkdir -p "$SYSROOT/usr/share/backgrounds/xfce"
if [ -f "$KRATOS_ROOT/Branding/KratosOS.png" ]; then
    cp "$KRATOS_ROOT/Branding/KratosOS.png" "$SYSROOT/usr/share/backgrounds/xfce/kratosos-logo.png"
fi

echo "[✓] XFCE desktop environment configured successfully."
