#!/usr/bin/env bash
# install-packages.sh — Install binary packages from repository into sysroot
#
# This script builds a host-native version of 'kpm' (kratos) and uses it
# to populate the sysroot with pre-built binary packages (Xorg, Xfce, etc.)
# from the KratosOS-Packages repository.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../config/build.conf"

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
    mkdir -p "$(dirname "$HOST_KPM")"

    # We use the host compiler and host mbedtls
    gcc -O2 -Wall -std=gnu11 -DHOST_BUILD \
        -I"$KPM_SRC_DIR" \
        -o "$HOST_KPM" \
        "$KPM_SRC_DIR/kratos-pkg.c" \
        "$KPM_SRC_DIR/kratos-repo.c" \
        "$KPM_SRC_DIR/kratos-sign.c" \
        "$KPM_SRC_DIR/kratos-tar.c" \
        "$KPM_SRC_DIR/kratos-sha256.c" \
        "$KPM_SRC_DIR/kratos-deps.c" \
        "$KPM_SRC_DIR/kratos-json.c" \
        "$KPM_SRC_DIR/kratos-fetch.c" \
        -lmbedtls -lmbedx509 -lmbedcrypto

    # Create CLI symlink
    gcc -O2 -Wall -std=gnu11 -I"$KPM_SRC_DIR" \
        -o "$(dirname "$HOST_KPM")/kratos-cli" \
        "$KPM_SRC_DIR/kratos-cli.c"

    echo "[✓] Host KPM built: $HOST_KPM"
fi

# 2. Prepare Sysroot for package installation
export KRATOS_SYSROOT="$SYSROOT"
mkdir -p "$SYSROOT/etc/kratos/repos.d"
mkdir -p "$SYSROOT/var/lib/kratos/db/packages"

# Copy the repository configuration from the etc skeleton if it doesn't exist yet
if [ ! -f "$SYSROOT/etc/kratos/repos.d/00-official.conf" ]; then
    echo "[+] Initializing repository configuration..."
    bash "$SCRIPT_DIR/create-etc-skeleton.sh"
fi

# 3. Update repository index
echo "[+] Updating package database..."
"$HOST_KPM" update

# 4. Define packages to install
# These are the essential packages for a working XFCE desktop environment.
# Note: if these are not in the repo, the script will warn/fail.
PACKAGES=(
    "networking"
    "utils"
    "libX11"
    "xorgproto"
    "libxcb"
    "freetype"
    "fontconfig"
    "glib"
    "pixman"
    # Future/Missing:
    # "xorg-server"
    # "xfce4-session"
    # "xfwm4"
    # "xfdesktop"
)

echo "[+] Installing target packages..."
for pkg in "${PACKAGES[@]}"; do
    echo "    -> Installing $pkg..."
    # --force to overwrite existing files (e.g. from etc skeleton)
    "$HOST_KPM" install --force "$pkg" || echo "    [!] Warning: Failed to install $pkg (might be missing in repo)"
done

echo
echo "[✓] Package injection complete."
