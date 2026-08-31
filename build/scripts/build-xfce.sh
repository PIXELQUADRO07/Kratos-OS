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
mkdir -p "$SYSROOT/root/Desktop"
mkdir -p "$SYSROOT/home/kratos-live/Desktop"
mkdir -p "$SYSROOT/etc/skel/Desktop"

# 2. Install desktop shortcut for Live installer
if [ -f "$KRATOS_ROOT/config/live/kratosos-live.desktop" ]; then
    echo "[+] Installing Live Installer desktop entry..."
    cp -f "$KRATOS_ROOT/config/live/kratosos-live.desktop" "$SYSROOT/etc/live/kratosos-live.desktop"
    cp -f "$KRATOS_ROOT/config/live/kratosos-live.desktop" "$SYSROOT/root/Desktop/kratosos-live.desktop"
    cp -f "$KRATOS_ROOT/config/live/kratosos-live.desktop" "$SYSROOT/home/kratos-live/Desktop/kratosos-live.desktop"
    cp -f "$KRATOS_ROOT/config/live/kratosos-live.desktop" "$SYSROOT/etc/skel/Desktop/kratosos-live.desktop"
    chmod +x "$SYSROOT/root/Desktop/kratosos-live.desktop" 2>/dev/null || true
    chmod +x "$SYSROOT/home/kratos-live/Desktop/kratosos-live.desktop" 2>/dev/null || true
    chmod +x "$SYSROOT/etc/skel/Desktop/kratosos-live.desktop" 2>/dev/null || true
fi

# 3. Create default wallpaper directory and copy Branding assets
mkdir -p "$SYSROOT/usr/share/backgrounds/xfce"
if [ -f "$KRATOS_ROOT/Branding/KratosOS.png" ]; then
    cp -f "$KRATOS_ROOT/Branding/KratosOS.png" "$SYSROOT/usr/share/backgrounds/xfce/kratosos-logo.png"
fi

# Keep the default panel limited to plugins shipped in the base image.
PANEL_CONFIG="$SYSROOT/etc/xdg/xfce4/panel/default.xml"
if [ -f "$PANEL_CONFIG" ]; then
    sed -i \
        -e '/<value type="int" value="8"\/>/d' \
        -e '/<value type="int" value="9"\/>/d' \
        -e '/<value type="int" value="10"\/>/d' \
        -e '/<property name="plugin-9" type="string" value="power-manager-plugin"\/>/d' \
        -e '/<property name="plugin-10" type="string" value="notification-plugin"\/>/d' \
        "$PANEL_CONFIG"
    perl -0pi -e 's/\n    <property name="plugin-8" type="string" value="pulseaudio">.*?\n    <property name="plugin-9"/\n    <property name="plugin-9"/s' "$PANEL_CONFIG"
fi

echo "[✓] XFCE desktop environment configured successfully."
