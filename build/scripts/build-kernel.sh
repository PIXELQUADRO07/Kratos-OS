#!/usr/bin/env bash
# build-kernel.sh — Cross-compile the Linux kernel for KratosOS (Phase 3, step 1)
#
# Produces:
#   sysroot/boot/vmlinuz-<version>   — compressed kernel image (bzImage)
#   sysroot/boot/System.map-<version>
#   sysroot/boot/config-<version>
#   sysroot/lib/modules/<version>/   — loadable kernel modules

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../config/build.conf"
source "$SCRIPT_DIR/../config/versions.conf"

PACKAGE="linux"
VERSION="$LINUX_VERSION"
ARCHIVE="$KRATOS_DOWNLOADS/$PACKAGE-$VERSION.tar.xz"
SOURCE_DIR="$KRATOS_SOURCES/$PACKAGE-$VERSION"
BOOT_DIR="$KRATOS_SYSROOT/boot"
JOBS="${KRATOS_JOBS:-$(nproc)}"

# Kernel is built in-source (Linux doesn't support out-of-tree well via KBUILD_OUTPUT
# without extra plumbing). We use a separate output dir via O=.
KBUILD_DIR="$KRATOS_WORK/linux-build"

echo "========================================"
echo "      KRATOSOS LINUX KERNEL $VERSION"
echo "========================================"
echo "  Target:   $TARGET"
echo "  Arch:     x86_64"
echo "  Sysroot:  $KRATOS_SYSROOT"
echo "  Jobs:     $JOBS"
echo

mkdir -p "$KRATOS_DOWNLOADS" "$KRATOS_SOURCES" "$KRATOS_WORK" "$BOOT_DIR"

# ── Download ──────────────────────────────────────────────────────────
if [ ! -f "$ARCHIVE" ]; then
    echo "[+] Downloading Linux $VERSION..."
    curl -L "https://cdn.kernel.org/pub/linux/kernel/v7.x/linux-$VERSION.tar.xz" \
         -o "$ARCHIVE"
else
    echo "[~] Linux $VERSION archive already present."
fi

# ── Extract ───────────────────────────────────────────────────────────
if [ ! -d "$SOURCE_DIR" ]; then
    echo "[+] Extracting Linux kernel (this may take a while)..."
    tar -xf "$ARCHIVE" -C "$KRATOS_SOURCES"
else
    echo "[~] Linux $VERSION already extracted."
fi

# ── Clean source tree if an in-source build was previously done ───────────────
if [ -f "$SOURCE_DIR/include/config/auto.conf" ] || [ -f "$SOURCE_DIR/.config" ]; then
    echo "[+] Cleaning in-source artifacts in kernel source tree..."
    make -C "$SOURCE_DIR" ARCH=x86_64 mrproper 2>/dev/null || true
fi

# ── Prepare output directory ──────────────────────────────────────────
mkdir -p "$KBUILD_DIR"

# Common cross-compilation variables
KMAKE=(
    make
    -C "$SOURCE_DIR"
    O="$KBUILD_DIR"
    ARCH=x86_64
    CROSS_COMPILE="${KRATOS_TOOLS}/bin/${TARGET}-"
    -j"$JOBS"
)

# ── Generate a sane default config ───────────────────────────────────
if [ ! -f "$KBUILD_DIR/.config" ]; then
    echo "[+] Generating x86_64 defconfig..."
    "${KMAKE[@]}" defconfig
else
    echo "[~] Kernel .config already present — using existing config."
fi

# ── Tweak and resolve config ──────────────────────────────────────────
echo "[+] Applying KratosOS kernel configuration tweaks..."
cat >> "$KBUILD_DIR/.config" << EOF
CONFIG_EFI_STUB=y
CONFIG_DEVTMPFS=y
CONFIG_DEVTMPFS_MOUNT=y
CONFIG_EXT4_FS=y
CONFIG_VFAT_FS=y
CONFIG_NLS_CODEPAGE_437=y
CONFIG_NLS_ISO8859_1=y
CONFIG_PRINTK=y
CONFIG_TTY=y
CONFIG_SERIAL_8250=y
CONFIG_SERIAL_8250_CONSOLE=y
CONFIG_VIRTIO=y
CONFIG_VIRTIO_PCI=y
CONFIG_VIRTIO_PCI_LEGACY=y
CONFIG_VIRTIO_BLK=y
CONFIG_VIRTIO_MENU=y
CONFIG_BLK_DEV_INITRD=y
CONFIG_RD_GZIP=y
CONFIG_SQUASHFS=y
CONFIG_SQUASHFS_ZSTD=y
CONFIG_SQUASHFS_XZ=y
CONFIG_OVERLAY_FS=y
CONFIG_ISO9660_FS=y
CONFIG_BLK_DEV_LOOP=y
CONFIG_FB=y
CONFIG_FB_EFI=y
CONFIG_FB_SIMPLE=y
CONFIG_SYSFB=y
CONFIG_SYSFB_SIMPLEFB=y
CONFIG_DRM=y
CONFIG_DRM_SIMPLEDRM=y
CONFIG_DRM_VIRTIO_GPU=y
CONFIG_FRAMEBUFFER_CONSOLE=y
CONFIG_LOGO=y
CONFIG_LOGO_LINUX_CLUT224=y
CONFIG_FB_CONSOLE_DEFERRED_TAKEOVER=y
CONFIG_FB_VESA=y
CONFIG_ACPI_VIDEO=y
CONFIG_BACKLIGHT_CLASS_DEVICE=y
CONFIG_DRM_I915=y
CONFIG_DRM_AMDGPU=y
CONFIG_DRM_RADEON=y
CONFIG_DRM_NOUVEAU=y
CONFIG_DRM_AST=y
CONFIG_DRM_MGAG200=y
CONFIG_DRM_QXL=y
CONFIG_DRM_BOCHS=y
CONFIG_DRM_VMWGFX=y
EOF

# Resolve any new symbols and dependencies
"${KMAKE[@]}" olddefconfig

# ── Build ─────────────────────────────────────────────────────────────
echo "[+] Building kernel bzImage + modules ($JOBS jobs)..."
"${KMAKE[@]}" bzImage modules

# ── Install ───────────────────────────────────────────────────────────
echo "[+] Installing kernel into sysroot..."

# bzImage
BZIMAGE="$KBUILD_DIR/arch/x86/boot/bzImage"
install -vm644 "$BZIMAGE"                          "$BOOT_DIR/vmlinuz-$VERSION"
install -vm644 "$KBUILD_DIR/System.map"            "$BOOT_DIR/System.map-$VERSION"
install -vm644 "$KBUILD_DIR/.config"               "$BOOT_DIR/config-$VERSION"

# Convenience symlink for bootloader
ln -sfv "vmlinuz-$VERSION"   "$BOOT_DIR/vmlinuz"
ln -sfv "System.map-$VERSION" "$BOOT_DIR/System.map"

# Modules
echo "[+] Installing kernel modules..."
"${KMAKE[@]}" modules_install INSTALL_MOD_PATH="$KRATOS_SYSROOT"

echo
echo "[✓] Linux kernel $VERSION built and installed."
echo "    bzImage:  $BOOT_DIR/vmlinuz-$VERSION"
echo "    Modules:  $KRATOS_SYSROOT/lib/modules/$VERSION/"
echo
echo "    Size: $(du -sh "$BOOT_DIR/vmlinuz-$VERSION" | cut -f1)"
