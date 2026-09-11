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
mkdir -p "$SYSROOT/etc/skel"
mkdir -p "$SYSROOT/root"
mkdir -p "$SYSROOT/home/kratos-live"
mkdir -p "$SYSROOT/var/log"

# Fix directory permissions
chmod 1777 "$SYSROOT/tmp" 2>/dev/null || true
chmod 777 "$SYSROOT/var/lib/xkb" 2>/dev/null || true
chmod 777 "$SYSROOT/var/log" 2>/dev/null || true

# 2. Configure Xorg SUID bit and Xwrapper for rootless / seatless execution
if [ -f "$SYSROOT/usr/bin/Xorg" ]; then
    chmod 4755 "$SYSROOT/usr/bin/Xorg"
fi

cat > "$SYSROOT/etc/X11/Xwrapper.config" << 'EOF'
allowed_users = anybody
needs_root_rights = yes
EOF

# 3. Copy live X11 configurations
# Prefer config/live-new/ when the file exists there, fall back to config/live/ —
# same convention already used for start-live.sh and xorg.conf.d below.
XORG_CONF_SRC=""
if   [ -f "$KRATOS_ROOT/config/live-new/xorg.conf" ]; then XORG_CONF_SRC="$KRATOS_ROOT/config/live-new/xorg.conf"
elif [ -f "$KRATOS_ROOT/config/live/xorg.conf"     ]; then XORG_CONF_SRC="$KRATOS_ROOT/config/live/xorg.conf"
fi
if [ -n "$XORG_CONF_SRC" ]; then
    echo "[+] Installing /etc/X11/xorg.conf (from $(dirname "$XORG_CONF_SRC" | xargs basename))..."
    cp "$XORG_CONF_SRC" "$SYSROOT/etc/X11/xorg.conf"
fi

if [ -d "$KRATOS_ROOT/config/live-new/xorg.conf.d" ]; then
    echo "[+] Installing /etc/X11/xorg.conf.d snippets..."
    cp -r "$KRATOS_ROOT/config/live-new/xorg.conf.d/"* "$SYSROOT/etc/X11/xorg.conf.d/"
fi

XINITRC_SRC=""
if   [ -f "$KRATOS_ROOT/config/live-new/xinitrc" ]; then XINITRC_SRC="$KRATOS_ROOT/config/live-new/xinitrc"
elif [ -f "$KRATOS_ROOT/config/live/xinitrc"     ]; then XINITRC_SRC="$KRATOS_ROOT/config/live/xinitrc"
fi
if [ -n "$XINITRC_SRC" ]; then
    echo "[+] Installing /etc/live/xinitrc and default user xinitrc scripts (from $(dirname "$XINITRC_SRC" | xargs basename))..."
    cp "$XINITRC_SRC" "$SYSROOT/etc/live/xinitrc"
    chmod +x "$SYSROOT/etc/live/xinitrc"

    # Also overwrite the default 3-xterm xinitrc so startx always starts XFCE
    cp "$XINITRC_SRC" "$SYSROOT/etc/X11/xinit/xinitrc"
    chmod +x "$SYSROOT/etc/X11/xinit/xinitrc"

    cp "$XINITRC_SRC" "$SYSROOT/etc/skel/.xinitrc"
    cp "$XINITRC_SRC" "$SYSROOT/root/.xinitrc"
    cp "$XINITRC_SRC" "$SYSROOT/home/kratos-live/.xinitrc"
    chmod +x "$SYSROOT/etc/skel/.xinitrc" "$SYSROOT/root/.xinitrc" "$SYSROOT/home/kratos-live/.xinitrc"
fi

if [ -f "$KRATOS_ROOT/config/live-new/start-live.sh" ]; then
    echo "[+] Installing /etc/live/start-live.sh..."
    mkdir -p "$SYSROOT/etc/live"
    cp "$KRATOS_ROOT/config/live-new/start-live.sh" "$SYSROOT/etc/live/start-live.sh"
    chmod +x "$SYSROOT/etc/live/start-live.sh"
elif [ -f "$KRATOS_ROOT/config/live/start-live.sh" ]; then
    echo "[+] Installing /etc/live/start-live.sh..."
    mkdir -p "$SYSROOT/etc/live"
    cp "$KRATOS_ROOT/config/live/start-live.sh" "$SYSROOT/etc/live/start-live.sh"
    chmod +x "$SYSROOT/etc/live/start-live.sh"
fi

# 3b. Fix gdk-pixbuf, Mime database and GSettings in sysroot
echo "[+] Performing graphical stack fixups in sysroot..."

# Force GDK Pixbuf loaders cache fixup
SEARCH_PATHS=""
for d in "$SYSROOT/usr/lib" "$SYSROOT/usr/lib64"; do
    [ -d "$d" ] && SEARCH_PATHS="$SEARCH_PATHS $d"
done

if [ -n "$SEARCH_PATHS" ]; then
    find $SEARCH_PATHS -name "loaders.cache" 2>/dev/null | while read -r cache; do
        if grep -q '^"lib/' "$cache"; then
            echo "  -> Fixing relative paths in $cache"
            sed -i 's|^"lib/|"\/usr\/lib/|g' "$cache"
        fi
        if ! grep -q 'libpixbufloader-svg.so' "$cache"; then
            echo "  -> Adding SVG loader to $cache"
            cat >> "$cache" << 'EOF_SVG'
"/usr/lib/gdk-pixbuf-2.0/2.10.0/loaders/libpixbufloader-svg.so"
"svg" 6 "gdk-pixbuf" "Scalable Vector Graphics" "LGPL"
"image/svg+xml" "image/svg" "image/svg-xml" "image/vnd.adobe.svg+xml" "text/xml-svg" "image/svg+xml-compressed" ""
"svg" "svgz" "svg.gz" ""
" <svg" "*    " 100
" <!DOCTYPE svg" "*             " 100

EOF_SVG
        fi
    done
fi

# Update Mime database (requires host update-mime-database or using cross-tools)
# We try to use the one from sysroot via a simple wrapper if possible, or just warn
if [ -x "$SYSROOT/usr/bin/update-mime-database" ]; then
    echo "[+] Updating Shared Mime Info database..."
    # We use LD_LIBRARY_PATH to let the sysroot binary run on host if it's compatible,
    # but since it's likely cross-compiled, we might need a host-native version.
    # For now, we assume the host has it as it's a common build dependency.
    if command -v update-mime-database >/dev/null 2>&1; then
        update-mime-database "$SYSROOT/usr/share/mime" >/dev/null 2>&1 || true
    fi
fi

if [ -d "$SYSROOT/usr/share/glib-2.0/schemas" ]; then
    echo "[+] Compiling GSettings schemas..."
    if command -v glib-compile-schemas >/dev/null 2>&1; then
        glib-compile-schemas "$SYSROOT/usr/share/glib-2.0/schemas" >/dev/null 2>&1 || true
    fi
fi

# Update Icon Caches
echo "[+] Updating icon caches..."
for themedir in "$SYSROOT/usr/share/icons"/*; do
    if [ -d "$themedir" ] && [ -f "$themedir/index.theme" ]; then
        if command -v gtk-update-icon-cache >/dev/null 2>&1; then
            gtk-update-icon-cache -f -t "$themedir" >/dev/null 2>&1 || true
        fi
    fi
done

# 4. Add Live session rc.d service (Enabled for Live Graphical Boot)
mkdir -p "$SYSROOT/etc/rc.d"
rm -f "$SYSROOT/etc/rc.d/99-live.disabled"
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
