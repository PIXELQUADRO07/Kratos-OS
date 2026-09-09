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

# If whiskermenu was installed to /usr/lib64, ensure a copy/symlink exists in /usr/lib
if [ -f "$SYSROOT/usr/lib64/xfce4/panel/plugins/libwhiskermenu.so" ] && [ ! -f "$SYSROOT/usr/lib/xfce4/panel/plugins/libwhiskermenu.so" ]; then
    mkdir -p "$SYSROOT/usr/lib/xfce4/panel/plugins"
    cp -f "$SYSROOT/usr/lib64/xfce4/panel/plugins/libwhiskermenu.so" "$SYSROOT/usr/lib/xfce4/panel/plugins/libwhiskermenu.so" 2>/dev/null || true
fi

# Ensure XFCE panel config only contains essential plugins
MENU_PLUGIN="applicationsmenu"
if [ -f "$SYSROOT/usr/share/xfce4/panel/plugins/whiskermenu.desktop" ] && \
   { [ -f "$SYSROOT/usr/lib/xfce4/panel/plugins/libwhiskermenu.so" ] || [ -f "$SYSROOT/usr/lib64/xfce4/panel/plugins/libwhiskermenu.so" ]; }; then
    MENU_PLUGIN="whiskermenu"
fi

PANEL_CONFIG="$SYSROOT/etc/xdg/xfce4/panel/default.xml"
rm -f "$PANEL_CONFIG"
cat > "$PANEL_CONFIG" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<channel name="xfce4-panel" version="1.0">
  <property name="panels" type="array">
    <value type="int" value="1"/>
  </property>
  <property name="panel-1" type="empty">
    <property name="position" type="string" value="p=8;x=0;y=0"/>
    <property name="length" type="uint" value="100"/>
    <property name="position-locked" type="bool" value="true"/>
    <property name="size" type="uint" value="30"/>
    <property name="plugin-ids" type="array">
      <value type="int" value="1"/>
      <value type="int" value="2"/>
      <value type="int" value="3"/>
      <value type="int" value="4"/>
    </property>
  </property>
  <property name="plugins" type="empty">
    <property name="plugin-1" type="string" value="${MENU_PLUGIN}"/>
    <property name="plugin-2" type="string" value="tasklist"/>
    <property name="plugin-3" type="string" value="systray"/>
    <property name="plugin-4" type="string" value="clock"/>
  </property>
</channel>
EOF

# Create minimal Xfconf channel files required for a functional session
XFCONF_DIR="$SYSROOT/etc/xdg/xfce4/xfconf/xfce-perchannel-xml"
mkdir -p "$XFCONF_DIR"

# Keep xfce4-panel.xml synchronized with default.xml
rm -f "$XFCONF_DIR/xfce4-panel.xml"
cp -f "$PANEL_CONFIG" "$XFCONF_DIR/xfce4-panel.xml"

rm -f "$XFCONF_DIR/xfwm4.xml"
cat > "$XFCONF_DIR/xfwm4.xml" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<channel name="xfwm4" version="1.0">
  <property name="general" type="empty">
    <property name="use_compositing" type="bool" value="false"/>
  </property>
</channel>
EOF

# 4. Set default wallpaper and desktop settings via Xfconf
DESKTOP_CONFIG="$SYSROOT/etc/xdg/xfce4/xfconf/xfce-perchannel-xml/xfce4-desktop.xml"
echo "[+] Configuring default XFCE desktop settings..."
mkdir -p "$(dirname "$DESKTOP_CONFIG")"
rm -f "$DESKTOP_CONFIG"
cat > "$DESKTOP_CONFIG" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<channel name="xfce4-desktop" version="1.0">
  <property name="backdrop" type="empty">
    <property name="screen0" type="empty">
      <property name="monitor0" type="empty">
        <property name="image-path" type="string" value="/usr/share/backgrounds/xfce/kratosos-logo.png"/>
        <property name="image-style" type="int" value="5"/>
        <property name="last-image" type="string" value="/usr/share/backgrounds/xfce/kratosos-logo.png"/>
      </property>
    </property>
  </property>
  <property name="desktop-icons" type="empty">
    <property name="file-icons" type="empty">
      <property name="show-home" type="bool" value="true"/>
      <property name="show-trash" type="bool" value="false"/>
      <property name="show-filesystem" type="bool" value="true"/>
      <property name="show-removable" type="bool" value="true"/>
    </property>
  </property>
</channel>
EOF

# 5. Populate user profiles with initial XFCE/xfconf configuration
for profile_dir in "$SYSROOT/etc/skel" "$SYSROOT/home/kratos-live" "$SYSROOT/root"; do
    mkdir -p "$profile_dir/.config/xfce4/xfconf/xfce-perchannel-xml"
    cp -rf "$XFCONF_DIR"/* "$profile_dir/.config/xfce4/xfconf/xfce-perchannel-xml/" 2>/dev/null || true
done

# 6. Pre-generate fontconfig cache in sysroot for fast GTK/XFCE startup
if command -v fc-cache >/dev/null 2>&1; then
    echo "[+] Pre-generating fontconfig cache in sysroot..."
    fc-cache -s -f -y "$SYSROOT" 2>/dev/null || true
fi

echo "[✓] XFCE desktop environment configured successfully."
