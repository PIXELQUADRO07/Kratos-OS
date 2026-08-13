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

echo "[+] Creating symlinks for reboot, poweroff, halt..."
ln -sf shutdown "$SYSROOT/sbin/reboot"
ln -sf shutdown "$SYSROOT/sbin/poweroff"
ln -sf shutdown "$SYSROOT/sbin/halt"

echo "[✓] Symlinks created."
echo
echo "Installed binaries:"
ls -lh "$INIT_OUT" "$SHUTDOWN_OUT" "$DEVD_OUT" "$NET_OUT" "$LOGIN_OUT" "$PASSWD_OUT" "$SU_OUT"
ls -lh "$USERADD_OUT" "$USERDEL_OUT" "$GROUPADD_OUT" "$GROUPDEL_OUT"
ls -la "$SYSROOT/sbin/reboot" "$SYSROOT/sbin/poweroff" "$SYSROOT/sbin/halt"

echo
echo "[✓] KratosOS system tools & authentication built successfully."
