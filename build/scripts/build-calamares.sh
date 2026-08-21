#!/usr/bin/env bash
# build-calamares.sh — Install and configure Calamares Installer for KratosOS

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../config/build.conf"
source "$SCRIPT_DIR/../config/versions.conf"

SYSROOT="$KRATOS_SYSROOT"

echo "========================================"
echo "    KRATOSOS CALAMARES INSTALLER"
echo "========================================"
echo "  Sysroot: $SYSROOT"
echo

# 1. Setup Calamares configuration directories
mkdir -p "$SYSROOT/etc/calamares/modules"
mkdir -p "$SYSROOT/etc/calamares/branding/kratosos"
mkdir -p "$SYSROOT/usr/share/calamares"

# 2. Copy Calamares configuration files
if [ -d "$KRATOS_ROOT/config/calamares" ]; then
    echo "[+] Installing Calamares configuration files..."
    cp -r "$KRATOS_ROOT/config/calamares/"* "$SYSROOT/etc/calamares/"
fi

# 3. Copy branding logo to Calamares branding directory
if [ -f "$KRATOS_ROOT/Branding/KratosOS.png" ]; then
    cp "$KRATOS_ROOT/Branding/KratosOS.png" "$SYSROOT/etc/calamares/branding/kratosos/logo.png"
    cp "$KRATOS_ROOT/Branding/KratosOS.png" "$SYSROOT/etc/calamares/branding/kratosos/welcome.png"
fi

# 4. Ensure polkit permission for running calamares
mkdir -p "$SYSROOT/usr/share/polkit-1/actions"
cat > "$SYSROOT/usr/share/polkit-1/actions/org.kratosos.calamares.policy" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE policyconfig PUBLIC
 "-//freedesktop//DTD PolicyKit Policy Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/PolicyKit/1/policyconfig.dtd">
<policyconfig>
  <vendor>KratosOS</vendor>
  <vendor_url>https://github.com/PIXELQUADRO07/Kratos-OS</vendor_url>
  <action id="org.kratosos.calamares.pkexec">
    <description>Run Calamares Installer</description>
    <message>Authentication is required to run the KratosOS Installer</message>
    <defaults>
      <allow_any>yes</allow_any>
      <allow_inactive>yes</allow_inactive>
      <allow_active>yes</allow_active>
    </defaults>
    <annotate key="org.freedesktop.policykit.exec.path">/usr/bin/calamares</annotate>
    <annotate key="org.freedesktop.policykit.exec.allow_gui">true</annotate>
  </action>
</policyconfig>
EOF

echo "[✓] Calamares installer configured successfully."
