#!/usr/bin/env bash
# build-firmware.sh — Install essential GPU and hardware firmware for KratosOS
#
# Why this exists:
#   Built-in DRM drivers (i915, amdgpu, nouveau) require external firmware
#   blobs to initialize hardware acceleration or even to display anything
#   beyond basic modesetting on many modern systems. Without these in
#   /lib/firmware, the kernel may hang or fail to initialize the display
#   at the "legacy bootconsole disabled" transition.
#
# Parrot OS / Debian Live style: include a representative subset of
# linux-firmware to balance compatibility and ISO size.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../config/build.conf"
source "$SCRIPT_DIR/../config/versions.conf"

SYSROOT="$KRATOS_SYSROOT"
FIRMWARE_DIR="$SYSROOT/lib/firmware"
DOWNLOAD_DIR="$KRATOS_DOWNLOADS/firmware"

echo "========================================"
echo "      KRATOSOS FIRMWARE INSTALL"
echo "========================================"
echo "  Sysroot: $SYSROOT"
echo

mkdir -p "$FIRMWARE_DIR"
mkdir -p "$DOWNLOAD_DIR"

# ── Download linux-firmware ───────────────────────────────────────────
# We use the kernel.org git archive for the latest stable blobs.
FIRMWARE_VERSION="20250211" # Update as needed
ARCHIVE="$DOWNLOAD_DIR/linux-firmware-$FIRMWARE_VERSION.tar.gz"

if [ ! -f "$ARCHIVE" ]; then
    echo "[+] Downloading linux-firmware $FIRMWARE_VERSION..."
    curl -L "https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/snapshot/linux-firmware-$FIRMWARE_VERSION.tar.gz" \
         -o "$ARCHIVE"
else
    echo "[~] linux-firmware archive already present."
fi

# ── Extract Subset ────────────────────────────────────────────────────
EXTRACT_DIR="$KRATOS_WORK/linux-firmware-extract"
mkdir -p "$EXTRACT_DIR"

if [ ! -d "$EXTRACT_DIR/linux-firmware-$FIRMWARE_VERSION" ]; then
    echo "[+] Extracting firmware archive..."
    tar -xf "$ARCHIVE" -C "$EXTRACT_DIR"
fi

FW_SRC="$EXTRACT_DIR/linux-firmware-$FIRMWARE_VERSION"

# ── Install Essential Blobs ───────────────────────────────────────────
# Following Parrot OS / Debian Live patterns for "standard" GPU support.
echo "[+] Installing GPU firmware blobs..."

# 1. Intel (i915/xe)
echo "    - Intel i915..."
mkdir -p "$FIRMWARE_DIR/i915"
cp -r "$FW_SRC/i915/"* "$FIRMWARE_DIR/i915/" 2>/dev/null || true

# 2. AMD (amdgpu/radeon)
echo "    - AMD amdgpu/radeon..."
mkdir -p "$FIRMWARE_DIR/amdgpu"
mkdir -p "$FIRMWARE_DIR/radeon"
cp -r "$FW_SRC/amdgpu/"* "$FIRMWARE_DIR/amdgpu/" 2>/dev/null || true
cp -r "$FW_SRC/radeon/"* "$FIRMWARE_DIR/radeon/" 2>/dev/null || true

# 3. Nvidia (nouveau - signed firmware for newer cards)
echo "    - Nvidia nouveau..."
mkdir -p "$FIRMWARE_DIR/nouveau"
cp -r "$FW_SRC/nouveau/"* "$FIRMWARE_DIR/nouveau/" 2>/dev/null || true

# 4. Storage & Network (optional but recommended for a "Parrot-like" live feel)
echo "    - Basic storage/NIC firmware..."
for d in intel-ucode amd-ucode rtl_nic; do
    if [ -d "$FW_SRC/$d" ]; then
        mkdir -p "$FIRMWARE_DIR/$d"
        cp -r "$FW_SRC/$d/"* "$FIRMWARE_DIR/$d/"
    fi
done

echo "[✓] Firmware installed successfully."
echo "    Total /lib/firmware size: $(du -sh "$FIRMWARE_DIR" | cut -f1)"
