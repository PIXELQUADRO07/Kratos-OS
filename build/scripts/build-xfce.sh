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

# Ensure XFCE panel config only contains essential plugins
# We'll generate a fresh default.xml with a known good set of plugins
PANEL_CONFIG="$SYSROOT/etc/xdg/xfce4/panel/default.xml"
cat > "$PANEL_CONFIG" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<channel name="xfce4-panel" version="1.0">
  <property name="panels" type="array">
    <value type="int" value="0"/>
  </property>
  <property name="plugins" type="array">
    <value type="int" value="1"/> <!-- whisker menu -->
    <value type="int" value="2"/> <!-- window buttons -->
    <value type="int" value="3"/> <!-- tasklist -->
    <value type="int" value="4"/> <!-- clock -->
    <value type="int" value="5"/> <!-- notification area -->
  </property>
</channel>
EOF

# Create minimal Xfconf channel files required for a functional session
XFCONF_DIR="$SYSROOT/etc/xdg/xfce4/xfconf/xfce-perchannel-xml"
mkdir -p "$XFCONF_DIR"

cat > "$XFCONF_DIR/xfce4-panel.xml" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<channel name="xfce4-panel" version="1.0"/>
EOF

cat > "$XFCONF_DIR/xfce4-desktop.xml" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<channel name="xfce4-desktop" version="1.0"/>
EOF

cat > "$XFCONF_DIR/xfce4-xsettings.xml" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<channel name="xfce4-xsettings" version="1.0"/>
EOF

cat > "$XFCONF_DIR/xfwm4.xml" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<channel name="xfwm4" version="1.0"/>
EOF

# 4. Set default wallpaper and desktop settings via Xfconf
DESKTOP_CONFIG="$SYSROOT/etc/xdg/xfce4/xfconf/xfce-perchannel-xml/xfce4-desktop.xml"
echo "[+] Configuring default XFCE desktop settings..."
mkdir -p "$(dirname "$DESKTOP_CONFIG")"
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

echo "[✓] XFCE desktop environment configured successfully."
