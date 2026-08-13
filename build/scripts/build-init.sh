#!/usr/bin/env bash

# build-init.sh — Build KratosOS init system, shutdown tools, kratos-devd, kratos-net, login, and passwd

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../config/build.conf"

SYSROOT="$KRATOS_SYSROOT"
TOOLS="$KRATOS_TOOLS"
CC="$TOOLS/bin/$TARGET-gcc"

INIT_SRC="$KRATOS_ROOT/init/init.c"
SHUTDOWN_SRC="$KRATOS_ROOT/init/shutdown.c"
DEVD_SRC="$KRATOS_ROOT/init/kratos-devd.c"
NET_SRC="$KRATOS_ROOT/init/kratos-net.c"
LOGIN_SRC="$KRATOS_ROOT/init/login.c"
PASSWD_SRC="$KRATOS_ROOT/init/passwd.c"

INIT_OUT="$SYSROOT/sbin/init"
SHUTDOWN_OUT="$SYSROOT/sbin/shutdown"
DEVD_OUT="$SYSROOT/sbin/kratos-devd"
NET_OUT="$SYSROOT/sbin/kratos-net"
LOGIN_OUT="$SYSROOT/bin/login"
PASSWD_OUT="$SYSROOT/usr/bin/passwd"

echo "========================================"
echo "       KRATOSOS SYSTEM BUILD"
echo "========================================"
echo "  Target:  $TARGET"
echo "  Sysroot: $SYSROOT"
echo "  CC:      $CC"
echo

if [ ! -f "$CC" ]; then
    echo "[!] Cross-compiler not found: $CC"
    exit 1
fi

mkdir -p "$SYSROOT/bin"
mkdir -p "$SYSROOT/sbin"
mkdir -p "$SYSROOT/usr/bin"

echo "[+] Compiling /sbin/init (modular PID 1)..."
"$CC" \
    --sysroot="$SYSROOT" \
    -O2 \
    -Wall \
    -fstack-protector-strong \
    -D_FORTIFY_SOURCE=2 \
    -Wextra \
    -std=gnu11 \
    -I"$KRATOS_ROOT/init" \
    -o "$INIT_OUT" \
    "$KRATOS_ROOT/init/init.c" \
    "$KRATOS_ROOT/init/mount.c" \
    "$KRATOS_ROOT/init/services.c" \
    "$KRATOS_ROOT/init/signals.c" \
    "$KRATOS_ROOT/init/tty.c" \
    -fPIE -pie \
    -Wl,-z,relro,-z,now
echo "[✓] init compiled."

echo "[+] Compiling /sbin/shutdown..."
"$CC" \
    --sysroot="$SYSROOT" \
    -O2 \
    -Wall \
    -fstack-protector-strong \
    -D_FORTIFY_SOURCE=2 \
    -Wextra \
    -std=gnu11 \
    -o "$SHUTDOWN_OUT" \
    "$SHUTDOWN_SRC" \
    -fPIE -pie \
    -Wl,-z,relro,-z,now
echo "[✓] shutdown compiled."

echo "[+] Compiling /sbin/kratos-devd..."
"$CC" \
    --sysroot="$SYSROOT" \
    -O2 \
    -Wall \
    -fstack-protector-strong \
    -D_FORTIFY_SOURCE=2 \
    -Wextra \
    -std=gnu11 \
    -o "$DEVD_OUT" \
    "$DEVD_SRC" \
    -fPIE -pie \
    -Wl,-z,relro,-z,now
echo "[✓] kratos-devd compiled."

echo "[+] Compiling /sbin/kratos-net..."
"$CC" \
    --sysroot="$SYSROOT" \
    -O2 \
    -Wall \
    -fstack-protector-strong \
    -D_FORTIFY_SOURCE=2 \
    -Wextra \
    -std=gnu11 \
    -o "$NET_OUT" \
    "$NET_SRC" \
    -fPIE -pie \
    -Wl,-z,relro,-z,now
echo "[✓] kratos-net compiled."

echo "[+] Compiling /bin/login..."
"$CC" \
    --sysroot="$SYSROOT" \
    -O2 \
    -Wall \
    -fstack-protector-strong \
    -D_FORTIFY_SOURCE=2 \
    -Wextra \
    -std=gnu11 \
    -o "$LOGIN_OUT" \
    "$LOGIN_SRC" \
    "$KRATOS_ROOT/init/kratos-crypt.c" \
    -fPIE -pie \
    -Wl,-z,relro,-z,now
echo "[✓] login compiled."

echo "[+] Compiling /usr/bin/passwd..."
"$CC" \
    --sysroot="$SYSROOT" \
    -O2 \
    -Wall \
    -fstack-protector-strong \
    -D_FORTIFY_SOURCE=2 \
    -Wextra \
    -std=gnu11 \
    -o "$PASSWD_OUT" \
    "$PASSWD_SRC" \
    "$KRATOS_ROOT/init/kratos-crypt.c" \
    -fPIE -pie \
    -Wl,-z,relro,-z,now
echo "[✓] passwd compiled."

USERADD_SRC="$KRATOS_ROOT/init/useradd.c"
USERDEL_SRC="$KRATOS_ROOT/init/userdel.c"
GROUPADD_SRC="$KRATOS_ROOT/init/groupadd.c"
GROUPDEL_SRC="$KRATOS_ROOT/init/groupdel.c"
SU_SRC="$KRATOS_ROOT/init/su.c"

USERADD_OUT="$SYSROOT/usr/sbin/useradd"
USERDEL_OUT="$SYSROOT/usr/sbin/userdel"
GROUPADD_OUT="$SYSROOT/usr/sbin/groupadd"
GROUPDEL_OUT="$SYSROOT/usr/sbin/groupdel"
SU_OUT="$SYSROOT/bin/su"

echo "[+] Compiling /usr/sbin/useradd..."
"$CC" \
    --sysroot="$SYSROOT" \
    -O2 \
    -Wall \
    -fstack-protector-strong \
    -D_FORTIFY_SOURCE=2 \
    -Wextra \
    -std=gnu11 \
    -o "$USERADD_OUT" \
    "$USERADD_SRC" \
    -fPIE -pie \
    -Wl,-z,relro,-z,now
echo "[✓] useradd compiled."

echo "[+] Compiling /usr/sbin/userdel..."
"$CC" \
    --sysroot="$SYSROOT" \
    -O2 \
    -Wall \
    -fstack-protector-strong \
    -D_FORTIFY_SOURCE=2 \
    -Wextra \
    -std=gnu11 \
    -o "$USERDEL_OUT" \
    "$USERDEL_SRC" \
    -fPIE -pie \
    -Wl,-z,relro,-z,now
echo "[✓] userdel compiled."

echo "[+] Compiling /usr/sbin/groupadd..."
"$CC" \
    --sysroot="$SYSROOT" \
    -O2 \
    -Wall \
    -fstack-protector-strong \
    -D_FORTIFY_SOURCE=2 \
    -Wextra \
    -std=gnu11 \
    -o "$GROUPADD_OUT" \
    "$GROUPADD_SRC" \
    -fPIE -pie \
    -Wl,-z,relro,-z,now
echo "[✓] groupadd compiled."

echo "[+] Compiling /usr/sbin/groupdel..."
"$CC" \
    --sysroot="$SYSROOT" \
    -O2 \
    -Wall \
    -fstack-protector-strong \
    -D_FORTIFY_SOURCE=2 \
    -Wextra \
    -std=gnu11 \
    -o "$GROUPDEL_OUT" \
    "$GROUPDEL_SRC" \
    -fPIE -pie \
    -Wl,-z,relro,-z,now
echo "[✓] groupdel compiled."

echo "[+] Compiling /bin/su..."
"$CC" \
    --sysroot="$SYSROOT" \
    -O2 \
    -Wall \
    -fstack-protector-strong \
    -D_FORTIFY_SOURCE=2 \
    -Wextra \
    -std=gnu11 \
    -o "$SU_OUT" \
    "$SU_SRC" \
    "$KRATOS_ROOT/init/kratos-crypt.c" \
    -fPIE -pie \
    -Wl,-z,relro,-z,now
echo "[✓] su compiled."

echo "[+] Setting setuid bit on /usr/bin/passwd and /bin/su..."
chmod 4755 "$PASSWD_OUT"
chmod 4755 "$SU_OUT"
echo "[✓] setuid modes set on passwd and su."

# ── Phase 1 System Utilities ──

USERMOD_SRC="$KRATOS_ROOT/init/usermod.c"
GROUPS_SRC="$KRATOS_ROOT/init/groups.c"
ID_SRC="$KRATOS_ROOT/init/id.c"
WHOAMI_SRC="$KRATOS_ROOT/init/whoami.c"
HOSTNAME_SRC="$KRATOS_ROOT/init/hostname.c"
MOUNT_SRC="$KRATOS_ROOT/init/kratos-mount.c"
PS_SRC="$KRATOS_ROOT/init/ps.c"
KILL_SRC="$KRATOS_ROOT/init/kill.c"
DMESG_SRC="$KRATOS_ROOT/init/dmesg.c"
FREE_SRC="$KRATOS_ROOT/init/free.c"
DF_SRC="$KRATOS_ROOT/init/df.c"

USERMOD_OUT="$SYSROOT/usr/sbin/usermod"
GROUPS_OUT="$SYSROOT/usr/bin/groups"
ID_OUT="$SYSROOT/usr/bin/id"
WHOAMI_OUT="$SYSROOT/usr/bin/whoami"
HOSTNAME_OUT="$SYSROOT/usr/bin/hostname"
MOUNT_OUT="$SYSROOT/bin/mount"
UMOUNT_OUT="$SYSROOT/bin/umount"
PS_OUT="$SYSROOT/bin/ps"
KILL_OUT="$SYSROOT/bin/kill"
DMESG_OUT="$SYSROOT/bin/dmesg"
FREE_OUT="$SYSROOT/usr/bin/free"
DF_OUT="$SYSROOT/usr/bin/df"

mkdir -p "$SYSROOT/usr/sbin"

HARDEN_FLAGS="-O2 -Wall -fstack-protector-strong -D_FORTIFY_SOURCE=2 -Wextra -std=gnu11 -fPIE -pie -Wl,-z,relro,-z,now"

echo "[+] Compiling /usr/sbin/usermod..."
"$CC" --sysroot="$SYSROOT" $HARDEN_FLAGS -o "$USERMOD_OUT" "$USERMOD_SRC"
echo "[✓] usermod compiled."

echo "[+] Compiling /usr/bin/groups..."
"$CC" --sysroot="$SYSROOT" $HARDEN_FLAGS -o "$GROUPS_OUT" "$GROUPS_SRC"
echo "[✓] groups compiled."

echo "[+] Compiling /usr/bin/id..."
"$CC" --sysroot="$SYSROOT" $HARDEN_FLAGS -o "$ID_OUT" "$ID_SRC"
echo "[✓] id compiled."

echo "[+] Compiling /usr/bin/whoami..."
"$CC" --sysroot="$SYSROOT" $HARDEN_FLAGS -o "$WHOAMI_OUT" "$WHOAMI_SRC"
echo "[✓] whoami compiled."

echo "[+] Compiling /usr/bin/hostname..."
"$CC" --sysroot="$SYSROOT" $HARDEN_FLAGS -o "$HOSTNAME_OUT" "$HOSTNAME_SRC"
echo "[✓] hostname compiled."

echo "[+] Compiling /bin/mount & /bin/umount..."
"$CC" --sysroot="$SYSROOT" $HARDEN_FLAGS -o "$MOUNT_OUT" "$MOUNT_SRC"
ln -sf mount "$SYSROOT/bin/umount"
echo "[✓] mount/umount compiled."

echo "[+] Compiling /bin/ps..."
"$CC" --sysroot="$SYSROOT" $HARDEN_FLAGS -o "$PS_OUT" "$PS_SRC"
echo "[✓] ps compiled."

echo "[+] Compiling /bin/kill..."
"$CC" --sysroot="$SYSROOT" $HARDEN_FLAGS -o "$KILL_OUT" "$KILL_SRC"
echo "[✓] kill compiled."

echo "[+] Compiling /bin/dmesg..."
"$CC" --sysroot="$SYSROOT" $HARDEN_FLAGS -o "$DMESG_OUT" "$DMESG_SRC"
echo "[✓] dmesg compiled."

echo "[+] Compiling /usr/bin/free..."
"$CC" --sysroot="$SYSROOT" $HARDEN_FLAGS -o "$FREE_OUT" "$FREE_SRC"
echo "[✓] free compiled."

echo "[+] Compiling /usr/bin/df..."
"$CC" --sysroot="$SYSROOT" $HARDEN_FLAGS -o "$DF_OUT" "$DF_SRC"
echo "[✓] df compiled."

echo "[+] Creating symlinks for reboot, poweroff, halt..."
ln -sf shutdown "$SYSROOT/sbin/reboot"
ln -sf shutdown "$SYSROOT/sbin/poweroff"
ln -sf shutdown "$SYSROOT/sbin/halt"

echo "[✓] Symlinks created."
echo
echo "Installed binaries:"
ls -lh "$INIT_OUT" "$SHUTDOWN_OUT" "$DEVD_OUT" "$NET_OUT" "$LOGIN_OUT" "$PASSWD_OUT" "$SU_OUT"
ls -lh "$USERADD_OUT" "$USERDEL_OUT" "$GROUPADD_OUT" "$GROUPDEL_OUT" "$USERMOD_OUT"
ls -lh "$GROUPS_OUT" "$ID_OUT" "$WHOAMI_OUT" "$HOSTNAME_OUT"
ls -lh "$MOUNT_OUT" "$PS_OUT" "$KILL_OUT" "$DMESG_OUT" "$FREE_OUT" "$DF_OUT"
ls -la "$SYSROOT/sbin/reboot" "$SYSROOT/sbin/poweroff" "$SYSROOT/sbin/halt" "$SYSROOT/bin/umount"

echo
echo "[✓] KratosOS system tools & authentication built successfully."
