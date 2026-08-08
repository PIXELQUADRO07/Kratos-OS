#!/usr/bin/env bash

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SYSROOT="$PROJECT_ROOT/build/sysroot"
ETC="$SYSROOT/etc"

echo "================================"
echo "   KRATOSOS /etc SKELETON"
echo "================================"
echo

mkdir -p "$ETC"

echo "[+] Creating /etc/passwd..."

cat > "$ETC/passwd" <<'EOF'
root:x:0:0:root:/root:/bin/bash
EOF

echo "[+] Creating /etc/group..."

cat > "$ETC/group" <<'EOF'
root:x:0:
EOF

echo "[+] Creating /etc/shadow..."

cat > "$ETC/shadow" <<'EOF'
root:*:19700:0:99999:7:::
EOF

chmod 600 "$ETC/shadow"

echo "[+] Creating /etc/shells..."

cat > "$ETC/shells" <<'EOF'
/bin/sh
/bin/bash
EOF

echo "[+] Creating /etc/hostname..."

cat > "$ETC/hostname" <<'EOF'
kratos
EOF

echo "[+] Creating /etc/fstab..."

cat > "$ETC/fstab" <<'EOF'
# KratosOS filesystem table

proc        /proc       proc        defaults        0 0
sysfs       /sys        sysfs       defaults        0 0
devtmpfs    /dev        devtmpfs    defaults        0 0
EOF

echo "[+] Creating /etc/os-release..."

cat > "$ETC/os-release" <<'EOF'
NAME="KratosOS"
ID=kratos
VERSION="0.1.0"
VERSION_ID="0.1.0"
PRETTY_NAME="KratosOS 0.1.0"
HOME_URL="https://kratosos.org"
EOF

echo "[+] Creating /etc/profile..."

cat > "$ETC/profile" <<'EOF'
export PATH=/bin:/sbin:/usr/bin:/usr/sbin
export HOME=/root
export SHELL=/bin/bash
EOF

echo
echo "[+] /etc skeleton created successfully."
echo
echo "Files:"
find "$ETC" -maxdepth 1 -type f -printf "  %f\n" | sort
