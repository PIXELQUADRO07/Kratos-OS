#!/usr/bin/env bash
# install-packages.sh — Install binary packages from repository into sysroot
#
# This script builds a host-native version of 'kpm' (kratos) and uses it
# to populate the sysroot with pre-built binary packages (Xorg, Xfce, etc.)
# from the KratosOS-Packages repository.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../config/build.conf"
source "$SCRIPT_DIR/../config/versions.conf"

SYSROOT="$KRATOS_SYSROOT"
HOST_KPM="$KRATOS_WORK/kpm-host/kratos"
KPM_SRC_DIR="$KRATOS_ROOT/pkg"

echo "========================================"
echo "    KRATOSOS PACKAGE INJECTION"
echo "========================================"
echo "  Target Sysroot: $SYSROOT"
echo

# 1. Build Host-Native KPM (Kratos Package Manager)
# We need kpm on the host to manage the sysroot's packages during build time.
if [ ! -f "$HOST_KPM" ]; then
    echo "[+] Building host-native KPM..."

    # Ensure we have a host-native mbedtls 3.x to avoid conflicts with host version (e.g. Arch's 4.x)
    MBEDTLS_HOST_INSTALL="$KRATOS_WORK/mbedtls-host-install"
    if [ ! -d "$MBEDTLS_HOST_INSTALL" ]; then
        echo "[+] Building host-native mbedtls ${MBEDTLS_VERSION}..."
        MBEDTLS_HOST_BUILD="$KRATOS_WORK/mbedtls-host-build"
        rm -rf "$MBEDTLS_HOST_BUILD"
        mkdir -p "$MBEDTLS_HOST_BUILD"
        cd "$MBEDTLS_HOST_BUILD"
        cmake "$KRATOS_SOURCES/mbedtls-${MBEDTLS_VERSION}" \
            -DCMAKE_INSTALL_PREFIX="$MBEDTLS_HOST_INSTALL" \
            -DCMAKE_INSTALL_LIBDIR=lib \
            -DENABLE_TESTING=OFF \
            -DENABLE_PROGRAMS=OFF \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_C_FLAGS="-Wno-error=unterminated-string-initialization -Wno-unterminated-string-initialization"
        make -j"$(nproc)" install
        cd "$SCRIPT_DIR"
    fi

    mkdir -p "$(dirname "$HOST_KPM")"

    # We use the host compiler and our built host mbedtls
    gcc -O2 -Wall -std=gnu11 -DHOST_BUILD \
        -I"$KPM_SRC_DIR" \
        -I"$MBEDTLS_HOST_INSTALL/include" \
        -o "$HOST_KPM" \
        "$KPM_SRC_DIR/kratos-pkg.c" \
        "$KPM_SRC_DIR/kratos-repo.c" \
        "$KPM_SRC_DIR/kratos-sign.c" \
        "$KPM_SRC_DIR/kratos-tar.c" \
        "$KPM_SRC_DIR/kratos-sha256.c" \
        "$KPM_SRC_DIR/kratos-deps.c" \
        "$KPM_SRC_DIR/kratos-json.c" \
        -L"$MBEDTLS_HOST_INSTALL/lib" \
        -lmbedtls -lmbedx509 -lmbedcrypto

    # Build host-native kratos-fetch
    gcc -O2 -Wall -std=gnu11 -DHOST_BUILD \
        -I"$MBEDTLS_HOST_INSTALL/include" \
        -o "$(dirname "$HOST_KPM")/kratos-fetch" \
        "$KPM_SRC_DIR/kratos-fetch.c" \
        -L"$MBEDTLS_HOST_INSTALL/lib" \
        -lmbedtls -lmbedx509 -lmbedcrypto

    # Create CLI symlink
    gcc -O2 -Wall -std=gnu11 -I"$KPM_SRC_DIR" \
        -o "$(dirname "$HOST_KPM")/kratos-cli" \
        "$KPM_SRC_DIR/kratos-cli.c"

    echo "[✓] Host KPM built: $HOST_KPM"
fi

# 2. Prepare Sysroot for package installation
export KRATOS_SYSROOT="$SYSROOT"
# Ensure host tools are in PATH so they can find each other (e.g. kratos-fetch)
export PATH="$(dirname "$HOST_KPM"):$PATH"
mkdir -p "$SYSROOT/etc/kratos/repos.d"
mkdir -p "$SYSROOT/var/lib/kratos/db/packages"

# Copy the repository configuration from the etc skeleton if it doesn't exist yet
if [ ! -f "$SYSROOT/etc/kratos/repos.d/00-official.conf" ]; then
    echo "[+] Initializing repository configuration..."
    bash "$SCRIPT_DIR/create-etc-skeleton.sh"
fi

# 3. Auto-detect local KratosOS-Packages repository or update via network
LOCAL_PACKAGES_REPO=""
for candidate in \
    "$KRATOS_ROOT/../KratosOS-Packages/repository/x86_64/stable" \
    "/home/ghost/Github/KratosOS-Packages/repository/x86_64/stable"; do
    if [ -d "$candidate/packages" ] && [ -f "$candidate/index.json" ]; then
        LOCAL_PACKAGES_REPO="$candidate"
        break
    fi
done

if [ -n "$LOCAL_PACKAGES_REPO" ]; then
    echo "[+] Using local KratosOS-Packages repository: $LOCAL_PACKAGES_REPO"
    mkdir -p "$SYSROOT/var/lib/kratos/repo-cache/kratos-official/packages"
    cp -f "$LOCAL_PACKAGES_REPO/index.json" "$SYSROOT/var/lib/kratos/repo-cache/kratos-official/index.json"
    cp -u "$LOCAL_PACKAGES_REPO/packages/"*.kpkg "$SYSROOT/var/lib/kratos/repo-cache/kratos-official/packages/" 2>/dev/null || true
else
    echo "[+] Updating package database from remote..."
    "$HOST_KPM" update || true
fi

# 4. Define packages to install
PACKAGES=(
    "networking"
    "utils"
    "xz"
    "libX11"
    "xorgproto"
    "libxcb"
    "freetype"
    "expat"
    "libxml2"
    "fontconfig"
    "glib"
    "pixman"
    "cairo"
    "pango"
    "atk"
    "gdk-pixbuf"
    "gtk+"
    "fribidi"
    "harfbuzz"
    "libpng"
    "libjpeg-turbo"
    "sqlite"
    "vim"
    "wget"
    "sudo"
)

OPTIONAL_PACKAGES=(
    "dbus"
    "libdrm"
    "mesa"
    "xorg-server"
    "xinit"
    "xauth"
    "xkbcomp"
    "libinput"
    "xf86-input-libinput"
    "xf86-video-vesa"
    "xf86-video-fbdev"
    "xkeyboard-config"
    "dejavu-fonts"
    "shared-mime-info"
    "hicolor-icon-theme"
    "libxfce4util"
    "xfconf"
    "libxfce4ui"
    "garcon"
    "exo"
    "xfce4-settings"
    "xfce4-session"
    "xfwm4"
    "xfdesktop"
    "xfce4-panel"
    "thunar"
    "xfce4-terminal"
)

echo "[+] Installing core repository packages..."
REQUIRED_PKGS="networking utils"
for pkg in "${PACKAGES[@]}"; do
    echo "    -> Installing $pkg..."
    # --force to overwrite existing files (e.g. from etc skeleton)
    if ! "$HOST_KPM" install --force "$pkg"; then
        if echo " $REQUIRED_PKGS " | grep -q " $pkg "; then
            echo "[!] Error: required package '$pkg' failed to install."
            exit 1
        fi
        echo "    [!] Warning: Failed to install $pkg (might be missing in repo)"
    fi
done

echo "[+] Checking optional desktop packages..."
for pkg in "${OPTIONAL_PACKAGES[@]}"; do
    if "$HOST_KPM" install --force "$pkg" 2>/dev/null; then
        echo "    -> Installed optional package: $pkg"
    fi
done

echo
echo "[✓] Package injection complete."
