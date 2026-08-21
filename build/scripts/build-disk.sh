#!/usr/bin/env bash

# build-disk.sh — Create a bootable KratosOS EFI disk image
#
# Boot chain:
#   UEFI → EFI/BOOT/BOOTX64.EFI (GRUB) → /boot/grub/grub.cfg → /boot/vmlinuz → /sbin/init
#
# Partition layout:
#   p1  256 MiB  FAT32   EFI System Partition  → GRUB EFI binary (BOOTX64.EFI)
#   p2  rest     ext4    Linux root             → sysroot + /boot/grub/grub.cfg
#
# Usage: sudo ./build-disk.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

source "$SCRIPT_DIR/../config/build.conf"
source "$SCRIPT_DIR/../config/versions.conf"

# ------------------------------------------------------------
# Paths
# ------------------------------------------------------------

SYSROOT="$KRATOS_SYSROOT"
IMAGE_DIR="$KRATOS_ROOT/build/images"
IMAGE="$IMAGE_DIR/kratosos.img"

IMAGE_SIZE_MB=2048
ESP_SIZE_MB=256

# ------------------------------------------------------------
# State variables — initialized immediately so cleanup() is
# safe under set -u even if the script fails early.
# ------------------------------------------------------------

LOOPDEV=""
MNT_ROOT=""

# ------------------------------------------------------------
# Cleanup
# ------------------------------------------------------------

cleanup() {
    local rc=$?
    set +e

    echo
    echo "[+] Cleanup..."

    # The ESP is always mounted at $MNT_ROOT/boot/efi.
    # Unmount it first, before unmounting MNT_ROOT.
    if [ -n "$MNT_ROOT" ] && mountpoint -q "$MNT_ROOT/boot/efi" 2>/dev/null; then
        echo "[+] Unmounting EFI partition..."
        umount "$MNT_ROOT/boot/efi"
    fi

    if [ -n "$MNT_ROOT" ] && mountpoint -q "$MNT_ROOT" 2>/dev/null; then
        echo "[+] Unmounting root partition..."
        umount "$MNT_ROOT"
    fi

    if [ -n "$MNT_ROOT" ] && [ -d "$MNT_ROOT" ]; then
        rm -rf "$MNT_ROOT"
    fi

    if [ -n "$LOOPDEV" ]; then
        losetup -d "$LOOPDEV" 2>/dev/null || true
    fi

    if [ $rc -ne 0 ]; then
        echo
        echo "[!] build-disk.sh FAILED (exit $rc)."
    fi
}

trap cleanup EXIT

# ------------------------------------------------------------
# Must run as root
# ------------------------------------------------------------

if [ "$(id -u)" -ne 0 ]; then
    echo "[!] This script must be run as root (or with sudo)."
    exit 1
fi

# ------------------------------------------------------------
# Banner
# ------------------------------------------------------------

echo "========================================"
echo "       KRATOSOS DISK IMAGE BUILD"
echo "========================================"
echo "  Image:   $IMAGE"
echo "  Sysroot: $SYSROOT"
echo "  Size:    ${IMAGE_SIZE_MB} MB"
echo "  ESP:     ${ESP_SIZE_MB} MB"
echo

# ------------------------------------------------------------
# Step 0: Check sysroot prerequisites
# ------------------------------------------------------------

echo "[Step 0] Checking sysroot prerequisites..."

REQUIRED_SYSROOT_FILES=(
    "$SYSROOT/boot/vmlinuz"
    "$SYSROOT/sbin/init"
    "$SYSROOT/bin/bash"
    "$SYSROOT/lib64/ld-linux-x86-64.so.2"
    "$SYSROOT/usr/lib/grub/x86_64-efi/normal.mod"
    "$SYSROOT/usr/lib/grub/x86_64-efi/linux.mod"
    "$SYSROOT/usr/lib/grub/x86_64-efi/part_gpt.mod"
)

for path in "${REQUIRED_SYSROOT_FILES[@]}"; do
    if [ ! -e "$path" ]; then
        echo "[!] Missing required sysroot file: $path"
        exit 1
    fi
done

echo "[✓] Sysroot prerequisites OK."

# ------------------------------------------------------------
# Step 0b: Check host tools
# ------------------------------------------------------------

echo "[+] Checking host tools..."

REQUIRED_TOOLS=(
    truncate parted losetup
    mkfs.fat mkfs.ext4
    mount umount
    grub-install
    blkid
)

for tool in "${REQUIRED_TOOLS[@]}"; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "[!] Required host tool not found: $tool"
        exit 1
    fi
done

echo "[✓] Host tools OK."
echo

# ------------------------------------------------------------
# Step 1: Create raw disk image
# ------------------------------------------------------------

echo "[Step 1] Creating raw disk image (${IMAGE_SIZE_MB} MB)..."

# ------------------------------------------------------------
# Step 0c: Detach stale loop devices from a previous run
#
# If a previous build-disk.sh run was interrupted (Ctrl-C, sudo
# timeout, crash) before its EXIT trap could run, a loop device
# can stay attached to the old kratosos.img — or to a now-deleted
# image file. losetup --find will then hand out a *different*
# loop device for the new image, but stale kernel/udev partition
# state from the old one can linger and cause blkid to report an
# inconsistent UUID later (root cause of "UUID in grub.cfg doesn't
# match any real partition" panics in QEMU). Clean these up first.
# ------------------------------------------------------------

echo "[+] Checking for stale loop devices from a previous build..."
STALE_LOOPS="$(losetup -a 2>/dev/null | grep -E "$(basename "$IMAGE")|\(deleted\)" | cut -d: -f1 || true)"
if [ -n "$STALE_LOOPS" ]; then
    for dev in $STALE_LOOPS; do
        echo "[+] Detaching stale loop device: $dev"
        losetup -d "$dev" 2>/dev/null || true
    done
else
    echo "[✓] No stale loop devices found."
fi

mkdir -p "$IMAGE_DIR"
if [ -n "${SUDO_USER:-}" ]; then
    chown -R "$SUDO_USER:$(id -gn "$SUDO_USER")" "$IMAGE_DIR" 2>/dev/null || true
fi
rm -f "$IMAGE"
truncate -s "${IMAGE_SIZE_MB}M" "$IMAGE"

echo "[✓] Image: $IMAGE"

# ------------------------------------------------------------
# Step 2: GPT + partitions
# ------------------------------------------------------------

echo
echo "[Step 2] Creating GPT partition table..."

parted -s "$IMAGE" mklabel gpt

# p1: EFI System Partition (FAT32, flagged esp)
parted -s "$IMAGE" \
    mkpart ESP fat32 \
    1MiB "${ESP_SIZE_MB}MiB"
parted -s "$IMAGE" set 1 esp on

# p2: Linux root (ext4)
parted -s "$IMAGE" \
    mkpart root ext4 \
    "${ESP_SIZE_MB}MiB" 100%

echo "[✓] Partition table:"
parted "$IMAGE" print

# ------------------------------------------------------------
# Step 3: Attach loop device
# ------------------------------------------------------------

echo
echo "[Step 3] Attaching loop device..."

LOOPDEV="$(losetup --find --show --partscan "$IMAGE")"
echo "[✓] Loop device: $LOOPDEV"

ESP_DEV="${LOOPDEV}p1"
ROOT_DEV="${LOOPDEV}p2"

# Wait for the kernel to register partition devices.
if command -v udevadm >/dev/null 2>&1; then
    udevadm settle 2>/dev/null || true
fi

# Poll until both partition block devices appear (max 10s).
WAIT=0
until [ -b "$ESP_DEV" ] && [ -b "$ROOT_DEV" ]; do
    sleep 0.5
    WAIT=$(( WAIT + 1 ))
    if [ "$WAIT" -ge 20 ]; then
        echo "[!] Timed out waiting for partition devices: $ESP_DEV  $ROOT_DEV"
        exit 1
    fi
done

echo "[✓] Partition devices ready:"
echo "    ESP:  $ESP_DEV"
echo "    Root: $ROOT_DEV"

# ------------------------------------------------------------
# Step 4: Format partitions
# ------------------------------------------------------------

echo
echo "[Step 4] Formatting partitions..."

mkfs.fat -F32 -n "KRATOSEFI" "$ESP_DEV"
echo "[✓] ESP formatted (FAT32, label KRATOSEFI)."

mkfs.ext4 -F -L "KratosOS" "$ROOT_DEV"
echo "[✓] Root formatted (ext4, label KratosOS)."

# ------------------------------------------------------------
# Step 5: Mount root partition
# ------------------------------------------------------------

echo
echo "[Step 5] Mounting root partition..."

MNT_ROOT="$(mktemp -d /tmp/kratos-disk-XXXXXX)"
mount "$ROOT_DEV" "$MNT_ROOT"
echo "[✓] Root mounted at $MNT_ROOT"

# ------------------------------------------------------------
# Step 6: Copy sysroot → root partition
#
# We exclude:
#   /boot/efi/     — this is just a mount-point for the ESP;
#                    it must remain an empty directory on the root ext4.
#   /boot/grub/grub.cfg — we generate a fresh one with the real UUID
#                    in Step 10; the sysroot copy (if any) may have
#                    a stale hardcoded UUID.
# ------------------------------------------------------------

echo
echo "[Step 6] Copying sysroot to root partition..."

if command -v rsync >/dev/null 2>&1; then
    rsync -aHAX \
        --exclude='/boot/efi/' \
        --exclude='/boot/grub/grub.cfg' \
        "$SYSROOT/" "$MNT_ROOT/"
else
    # cp -a does not support exclude, so copy then clean up.
    cp -a "$SYSROOT/." "$MNT_ROOT/"
    rm -f  "$MNT_ROOT/boot/grub/grub.cfg"
    # Remove any stale EFI contents so the directory is clean.
    rm -rf "$MNT_ROOT/boot/efi/"*
fi

echo "[✓] Sysroot copied."

# ------------------------------------------------------------
# Step 6b: Normalize ownership to root:root
#
# $SYSROOT is built by an unprivileged user (build-init.sh, build-pkg.sh,
# etc. intentionally do NOT run as root — see STAGE_SUDO in
# build-all-phase3.sh). rsync -a / cp -a faithfully replicate that
# non-root ownership onto the image, since this script (running as root)
# has permission to chown to anything, including the build user's own
# UID/GID. Left uncorrected, EVERY file in the shipped image — not just
# passwd — would be owned by the build user, not root.
#
# This matters beyond cosmetics: a setuid-root bit (see /usr/bin/passwd
# in build-init.sh) only grants root privilege if the file's *owner* is
# actually root — setuid on a file owned by UID 1000 elevates to UID
# 1000, not to root. So without this step, passwd's setuid bit would be
# silently meaningless in the shipped image even though it looks correct
# in `ls -l` output on the sysroot.
# ------------------------------------------------------------

echo
echo "[Step 6b] Normalizing ownership to root:root..."
chown -R root:root "$MNT_ROOT"
echo "[✓] Ownership normalized."

# ------------------------------------------------------------
# Step 7: Create required directories in the root partition
# ------------------------------------------------------------

echo
echo "[Step 7] Preparing directory structure..."

# Mount-point for the ESP — must exist, stays empty on root ext4.
mkdir -p "$MNT_ROOT/boot/efi"

# GRUB config directory.
mkdir -p "$MNT_ROOT/boot/grub"

echo "[✓] Directories ready."

# ------------------------------------------------------------
# Step 8: Mount EFI System Partition
# ------------------------------------------------------------

echo
echo "[Step 8] Mounting EFI partition..."

mount "$ESP_DEV" "$MNT_ROOT/boot/efi"
echo "[✓] ESP mounted at $MNT_ROOT/boot/efi"

# ------------------------------------------------------------
# Step 9: Install GRUB into the ESP
#
# --target=x86_64-efi
#     Build an EFI application.
#
# --efi-directory="$MNT_ROOT/boot/efi"
#     Where the ESP is currently mounted.
#     grub-install places the EFI binary here:
#       EFI/BOOT/BOOTX64.EFI   (because of --removable)
#
# --boot-directory="$MNT_ROOT/boot"
#     Where /boot/grub/x86_64-efi/ module tree will be written.
#     This directory lives on the root ext4 partition.
#
# --removable
#     Writes EFI/BOOT/BOOTX64.EFI (the fallback path recognized by
#     all UEFI firmware even without NVRAM entries).
#
# --no-nvram
#     Do NOT modify the host machine's UEFI NVRAM.
#
# --modules=...
#     Embed these modules into the GRUB core image so the bootloader
#     can always find its own configuration file even before /boot/grub
#     is loaded.
# ------------------------------------------------------------

echo
echo "[Step 9] Installing GRUB EFI bootloader..."

grub-install \
    --target=x86_64-efi \
    --efi-directory="$MNT_ROOT/boot/efi" \
    --boot-directory="$MNT_ROOT/boot" \
    --removable \
    --no-nvram \
    --recheck \
    --modules="part_gpt ext2 linux normal echo search search_fs_uuid gfxterm png font all_video gfxmenu" \
    2>&1 | sed 's/^/    /'

echo "[✓] GRUB installed."

# ------------------------------------------------------------
# Step 10: Detect root filesystem UUID
# ------------------------------------------------------------

echo
echo "[Step 10] Detecting root UUID..."

# Sync all pending writes and flush udev before probing.
sync
if command -v udevadm >/dev/null 2>&1; then
    udevadm settle 2>/dev/null || true
fi

ROOT_UUID=""
if command -v findmnt >/dev/null 2>&1; then
    ROOT_UUID="$(findmnt -no UUID "$MNT_ROOT" 2>/dev/null || true)"
fi

if [ -z "$ROOT_UUID" ]; then
    ROOT_UUID="$(blkid -p -s UUID -o value "$ROOT_DEV")"
fi

if [ -z "$ROOT_UUID" ]; then
    ROOT_UUID="$(blkid -s UUID -o value "$ROOT_DEV")"
fi

if [ -z "$ROOT_UUID" ]; then
    echo "[!] Could not detect UUID of $ROOT_DEV"
    exit 1
fi

BLKID_UUID="$(blkid -p -s UUID -o value "$ROOT_DEV" 2>/dev/null || true)"
if [ -n "$BLKID_UUID" ] && [ "$BLKID_UUID" != "$ROOT_UUID" ]; then
    echo "[!] UUID MISMATCH detected:"
    echo "    findmnt (mounted fs): $ROOT_UUID"
    echo "    blkid -p (raw probe): $BLKID_UUID"
    echo "[!] Run 'sudo losetup -a' and detach leftover loop devices."
    exit 1
fi

ROOT_PARTUUID="$(blkid -s PARTUUID -o value "$ROOT_DEV" 2>/dev/null || blkid -p -s PART_ENTRY_UUID -o value "$ROOT_DEV" 2>/dev/null || true)"

if [ -z "$ROOT_PARTUUID" ]; then
    echo "[!] Could not detect PARTUUID of $ROOT_DEV"
    exit 1
fi

echo "[✓] Root PARTUUID: $ROOT_PARTUUID"

# ------------------------------------------------------------
# Step 11: Install Branding Logo & Custom /boot/grub/grub.cfg
# ------------------------------------------------------------

echo
echo "[Step 11] Installing Branding logo & generating custom /boot/grub/grub.cfg..."

BUILD_ID="$(date -u +%Y%m%dT%H%M%SZ)"
echo "[+] Build ID for this image: $BUILD_ID"

# 1. Install KratosOS Logo from /Branding into GRUB boot directory
mkdir -p "$MNT_ROOT/boot/grub/branding"
if [ -f "$KRATOS_ROOT/Branding/KratosOS.png" ]; then
    cp "$KRATOS_ROOT/Branding/KratosOS.png" "$MNT_ROOT/boot/grub/branding/KratosOS.png"
    echo "[✓] Branding logo installed: /boot/grub/branding/KratosOS.png"
fi

# 2. Process custom GRUB configuration template
GRUB_TEMPLATE="$KRATOS_ROOT/config/grub/grub.cfg.template"
if [ -f "$GRUB_TEMPLATE" ]; then
    sed -e "s/@ROOT_UUID@/${ROOT_UUID}/g" \
        -e "s/@ROOT_PARTUUID@/${ROOT_PARTUUID}/g" \
        -e "s/@BUILD_ID@/${BUILD_ID}/g" \
        "$GRUB_TEMPLATE" > "$MNT_ROOT/boot/grub/grub.cfg"
    echo "[✓] Custom grub.cfg generated from config/grub/grub.cfg.template"
else
    echo "[!] Template not found at $GRUB_TEMPLATE, generating default grub.cfg"
    cat > "$MNT_ROOT/boot/grub/grub.cfg" << GRUB_EOF
# KratosOS GRUB configuration
set timeout=5
set default=0

serial --speed=115200 --unit=0 --word=8 --parity=no --stop=1
terminal_input console serial
terminal_output console serial

menuentry "KratosOS 0.1.0 (PARTUUID)" {
    insmod part_gpt
    insmod ext2
    insmod linux
    search --no-floppy --fs-uuid --set=root ${ROOT_UUID}
    linux /boot/vmlinuz root=PARTUUID=${ROOT_PARTUUID} rw init=/sbin/init console=ttyS0,115200 loglevel=3 kratos.build=${BUILD_ID} quiet
}

menuentry "KratosOS 0.1.0 (VirtIO /dev/vda2)" {
    insmod part_gpt
    insmod ext2
    insmod linux
    search --no-floppy --fs-uuid --set=root ${ROOT_UUID}
    linux /boot/vmlinuz root=/dev/vda2 rw init=/sbin/init console=ttyS0,115200 loglevel=3 kratos.build=${BUILD_ID} quiet
}
GRUB_EOF
fi

echo "[✓] grub.cfg written."

# ------------------------------------------------------------
# Step 11b: Self-check — verify the UUID we just wrote into
# grub.cfg actually matches the filesystem currently on ROOT_DEV.
# This is what would have caught the earlier "boots to a kernel
# panic because grub.cfg has a stale UUID" bug immediately, at
# build time, instead of at boot time in QEMU.
# ------------------------------------------------------------

echo
echo "[Step 11b] Verifying UUID consistency..."

RECHECK_UUID="$(blkid -p -s UUID -o value "$ROOT_DEV")"
if [ "$RECHECK_UUID" != "$ROOT_UUID" ]; then
    echo "[!] UUID MISMATCH DETECTED:"
    echo "    grub.cfg was written with: $ROOT_UUID"
    echo "    $ROOT_DEV actually reports: $RECHECK_UUID"
    echo "[!] This would produce an unbootable image. Aborting."
    echo "[!] This usually means a stale loop device from a previous"
    echo "    run was interfering. Run 'losetup -a' and detach any"
    echo "    devices pointing at old/deleted images, then retry."
    exit 1
fi

echo "[✓] UUID consistent: $ROOT_UUID"

# ------------------------------------------------------------
# Step 12: Verify all required boot chain files
# ------------------------------------------------------------

echo
echo "[Step 12] Verifying boot chain in image..."

VERIFY_PATHS=(
    "$MNT_ROOT/boot/vmlinuz"
    "$MNT_ROOT/boot/grub/grub.cfg"
    "$MNT_ROOT/boot/grub/x86_64-efi"
    "$MNT_ROOT/boot/efi/EFI/BOOT/BOOTX64.EFI"
    "$MNT_ROOT/sbin/init"
    "$MNT_ROOT/bin/bash"
    "$MNT_ROOT/lib64/ld-linux-x86-64.so.2"
    "$MNT_ROOT/etc/fstab"
    "$MNT_ROOT/etc/passwd"
)

ALL_OK=1
for path in "${VERIFY_PATHS[@]}"; do
    if [ -e "$path" ]; then
        printf "    [✓] %s\n" "${path#$MNT_ROOT}"
    else
        printf "    [✗] MISSING: %s\n" "${path#$MNT_ROOT}"
        ALL_OK=0
    fi
done

if [ "$ALL_OK" -ne 1 ]; then
    echo
    echo "[!] Verification FAILED — some required files are missing."
    exit 1
fi

echo "[✓] Boot chain complete."

# ------------------------------------------------------------
# Step 13: Informational dump
# ------------------------------------------------------------

echo
echo "[+] EFI partition contents:"
find "$MNT_ROOT/boot/efi" -type f -printf "    %P\n" 2>/dev/null || true

echo
echo "[+] GRUB module directory:"
find "$MNT_ROOT/boot/grub/x86_64-efi" -maxdepth 1 -name "*.mod" | wc -l | \
    xargs -I{} printf "    {} .mod files\n"

echo
echo "[+] grub.cfg:"
sed 's/^/    /' "$MNT_ROOT/boot/grub/grub.cfg"

# ------------------------------------------------------------
# Step 14: Sync and permissions
# ------------------------------------------------------------

echo
echo "[Step 14] Syncing filesystems..."

sync

if [ -n "${SUDO_USER:-}" ]; then
    chown "$SUDO_USER:$(id -gn "$SUDO_USER")" "$IMAGE"
fi

echo "[✓] Sync complete."

# ------------------------------------------------------------
# Final summary
# ------------------------------------------------------------

echo
echo "========================================"
echo "       KRATOSOS IMAGE READY"
echo "========================================"
echo
echo "  Image:      $IMAGE"
du -sh "$IMAGE" | awk '{print "  Size:       " $1}'
echo "  Root UUID:  $ROOT_UUID"
echo "  Build ID:   $BUILD_ID"
echo
echo "  Boot chain:"
echo "    UEFI firmware"
echo "    └── EFI/BOOT/BOOTX64.EFI  [ESP, FAT32]"
echo "    └── /boot/grub/grub.cfg   [root, ext4]"
echo "    └── /boot/vmlinuz         [kernel 7.1.5]"
echo "    └── /sbin/init"
echo "    └── /bin/bash"
echo
echo "  Test with QEMU + OVMF:"
echo
echo "    qemu-system-x86_64 \\"
echo "      -m 512M \\"
echo "      -drive file=\"$IMAGE\",format=raw,if=virtio \\"
echo "      -bios /usr/share/ovmf/OVMF.fd \\"
echo "      -nographic"
echo
echo "[✓] KratosOS disk image created successfully."
