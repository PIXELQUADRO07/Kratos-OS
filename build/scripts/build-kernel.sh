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

    # Enable a few extras useful for a real system
    echo "[+] Tweaking config..."
    # Make sure EFI stub, serial console, devtmpfs and ext4 are on.
    #
    # IMPORTANT: this must be the *absolute* path to scripts/config inside
    # the kernel source tree. The rest of this script never `cd`s into
    # $SOURCE_DIR (it uses `make -C "$SOURCE_DIR"` throughout instead), so
    # a bare relative "scripts/config" resolves against whatever directory
    # the caller invoked this script from — almost never the kernel tree —
    # and silently fails every time under the `2>/dev/null || true` below.
    # When that happens this whole --enable block becomes a no-op and you
    # only find out at boot time, if defconfig's own defaults happen not
    # to cover you.
    "$SOURCE_DIR/scripts/config" --file "$KBUILD_DIR/.config" \
        --enable  CONFIG_EFI_STUB         \
        --enable  CONFIG_DEVTMPFS         \
        --enable  CONFIG_DEVTMPFS_MOUNT   \
        --enable  CONFIG_EXT4_FS          \
        --enable  CONFIG_VFAT_FS          \
        --enable  CONFIG_NLS_CODEPAGE_437 \
        --enable  CONFIG_NLS_ISO8859_1    \
        --enable  CONFIG_PRINTK           \
        --enable  CONFIG_TTY              \
        --enable  CONFIG_SERIAL_8250      \
        --enable  CONFIG_SERIAL_8250_CONSOLE \
        --enable  CONFIG_VIRTIO           \
        --enable  CONFIG_VIRTIO_PCI       \
        --enable  CONFIG_VIRTIO_PCI_LEGACY \
        --enable  CONFIG_VIRTIO_BLK       \
        --enable  CONFIG_VIRTIO_MENU      \
        2>/dev/null || true  # tolerate older trees without scripts/config

    # x86_64 defconfig builds VIRTIO_BLK/VIRTIO_PCI as modules (=m) by
    # default. There is no initramfs in this boot chain and init.c never
    # loads kernel modules, so if these stay as modules the kernel simply
    # cannot see /dev/vda when QEMU is run with -drive if=virtio, and it
    # panics with "VFS: Unable to mount root fs". They must be built-in.
    # scripts/config --enable only sets bool/tristate options to 'y' when
    # possible; force it explicitly in case a tristate default resists:
    sed -i \
        -e 's/^CONFIG_VIRTIO_PCI=m/CONFIG_VIRTIO_PCI=y/' \
        -e 's/^CONFIG_VIRTIO_BLK=m/CONFIG_VIRTIO_BLK=y/' \
        -e 's/^CONFIG_VIRTIO=m/CONFIG_VIRTIO=y/' \
        "$KBUILD_DIR/.config" 2>/dev/null || true

    # Resolve any new symbols introduced by our changes
    "${KMAKE[@]}" olddefconfig
else
    echo "[~] Kernel .config already present — skipping defconfig."
fi

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
