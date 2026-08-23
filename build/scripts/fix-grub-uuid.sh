#!/usr/bin/env bash
# fix-grub-uuid.sh — Fix UUID mismatch in grub.cfg inside kratosos.img
# Usage: sudo ./fix-grub-uuid.sh

set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
    echo "[!] Run with sudo."
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../config/build.conf"

IMAGE="$KRATOS_ROOT/build/images/kratosos.img"

echo "[+] Image: $IMAGE"

if [ ! -f "$IMAGE" ]; then
    echo "[!] Image not found: $IMAGE"
    exit 1
fi

if [ ! -e /dev/loop-control ]; then
    echo "[+] Loading host loop kernel module..."
    modprobe loop 2>/dev/null || true
fi

if [ ! -e /dev/loop-control ]; then
    echo "[!] /dev/loop-control is missing — the host loop driver is not available."
    exit 1
fi

# Attach first, then scan partitions (combined --partscan can ENOENT).
LOOP=""
for _try in $(seq 1 10); do
    if LOOP="$(losetup --find --show "$IMAGE" 2>/dev/null)" && [ -n "$LOOP" ]; then
        break
    fi
    LOOP=""
    sleep 0.2
done

if [ -z "$LOOP" ]; then
    echo "[!] losetup failed to attach $IMAGE"
    ls -l /dev/loop-control /dev/loop[0-9]* 2>/dev/null || true
    losetup -a 2>/dev/null || true
    stat "$IMAGE" 2>/dev/null || true
    exit 1
fi

echo "[+] Loop: $LOOP"

losetup --partscan "$LOOP" 2>/dev/null || true
if command -v partx >/dev/null 2>&1; then
    partx --add "$LOOP" 2>/dev/null || true
fi
if command -v udevadm >/dev/null 2>&1; then
    udevadm settle 2>/dev/null || true
fi

ROOT_DEV="${LOOP}p2"

# Wait for device
sleep 1

# Get real UUID
REAL_UUID="$(blkid -s UUID -o value "$ROOT_DEV")"
echo "[+] Real UUID of root (p2): $REAL_UUID"

MNT="$(mktemp -d)"

cleanup() {
    set +e
    mountpoint -q "$MNT" && umount "$MNT"
    [ -d "$MNT" ] && rmdir "$MNT"
    losetup -d "$LOOP" 2>/dev/null || true
}
trap cleanup EXIT

mount "$ROOT_DEV" "$MNT"

echo
echo "=== Current grub.cfg ==="
cat "$MNT/boot/grub/grub.cfg"

# Write correct grub.cfg
cat > "$MNT/boot/grub/grub.cfg" << GRUB_EOF
# KratosOS GRUB configuration
# Root UUID: ${REAL_UUID}
set timeout=3
set default=0

menuentry "KratosOS 0.1.0" {
    insmod part_gpt
    insmod ext2
    insmod linux

    search --no-floppy --fs-uuid --set=root ${REAL_UUID}

    linux /boot/vmlinuz root=UUID=${REAL_UUID} rw console=ttyS0,115200 loglevel=3 quiet
}
GRUB_EOF

echo
echo "=== New grub.cfg ==="
cat "$MNT/boot/grub/grub.cfg"

sync

echo
echo "[✓] Done. UUID applied: $REAL_UUID"
