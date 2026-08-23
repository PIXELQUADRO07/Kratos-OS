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

# Build scripts/config if needed (lives in the output dir with O=).
"${KMAKE[@]}" scripts/config >/dev/null

kconfig() {
    "$KBUILD_DIR/scripts/config" --file "$KBUILD_DIR/.config" "$@"
}

# Live-boot filesystems and block layer: builtin so initramfs need not
# insmod them. Joliet is required for long ISO filenames (rootfs.squashfs).
kconfig --enable EFI_STUB
kconfig --enable DEVTMPFS
kconfig --enable DEVTMPFS_MOUNT
kconfig --enable EXT4_FS
kconfig --enable VFAT_FS
kconfig --enable NLS_CODEPAGE_437
kconfig --enable NLS_ISO8859_1
kconfig --enable PRINTK
kconfig --enable TTY
kconfig --enable SERIAL_8250
kconfig --enable SERIAL_8250_CONSOLE
kconfig --enable VIRTIO
kconfig --enable VIRTIO_PCI
kconfig --enable VIRTIO_PCI_LEGACY
kconfig --enable VIRTIO_BLK
kconfig --enable VIRTIO_SCSI
kconfig --enable SCSI_VIRTIO
kconfig --enable VIRTIO_MENU
kconfig --enable BLK_DEV_INITRD
kconfig --enable RD_GZIP
kconfig --enable SQUASHFS
kconfig --enable SQUASHFS_ZSTD
kconfig --enable SQUASHFS_XZ
kconfig --enable OVERLAY_FS
kconfig --enable ISO9660_FS
kconfig --enable JOLIET
kconfig --enable BLK_DEV_LOOP
kconfig --enable SCSI
kconfig --enable BLK_DEV_SD
kconfig --enable BLK_DEV_SR
kconfig --enable ATA
kconfig --enable SATA_AHCI
kconfig --enable USB
kconfig --enable USB_XHCI_HCD
kconfig --enable USB_EHCI_HCD
kconfig --enable USB_STORAGE
kconfig --enable FB
kconfig --enable FB_EFI
kconfig --enable FB_SIMPLE
kconfig --enable SYSFB
kconfig --enable SYSFB_SIMPLEFB
kconfig --enable DRM
kconfig --enable DRM_SIMPLEDRM
kconfig --enable DRM_VIRTIO_GPU
kconfig --enable DRM_BOCHS
kconfig --enable FRAMEBUFFER_CONSOLE
kconfig --enable LOGO
kconfig --enable LOGO_LINUX_CLUT224
kconfig --enable FB_CONSOLE_DEFERRED_TAKEOVER
kconfig --enable FB_VESA
kconfig --enable ACPI_VIDEO
kconfig --enable BACKLIGHT_CLASS_DEVICE

# Heavy DRM drivers: modules, firmware lives in the squashfs not the ramdisk.
kconfig --module DRM_I915
kconfig --module DRM_AMDGPU
kconfig --module DRM_RADEON
kconfig --module DRM_NOUVEAU
kconfig --module DRM_AST
kconfig --module DRM_MGAG200
kconfig --module DRM_QXL
kconfig --module DRM_VMWGFX

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
