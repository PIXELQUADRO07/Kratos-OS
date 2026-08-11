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

# setuid root: passwd.c refuses to run unless getuid()==0, and login.c drops
# privileges to the logging-in user's UID before exec'ing their shell — so
# without this bit, any non-root user invoking passwd to change their own
# password would run as their own (non-root) UID and be rejected outright.
# Harmless to skip today (only root exists in /etc/passwd so far), but
# required as soon as real user accounts are added.
#
# NOTE: this stage intentionally runs unprivileged (STAGE_SUDO=no in
# build-all-phase3.sh), so we can only set the mode bits here — the file
# is still owned by the build user at this point. The setuid bit only
# grants *root* privilege once the file is actually owned by root, which
# happens later in build-disk.sh (Step 6b, "Normalizing ownership to
# root:root") when the sysroot is copied into the final image under sudo.
# Setting the mode here and the ownership there together are both required;
# neither alone is enough.
echo "[+] Setting setuid bit on /usr/bin/passwd (ownership finalized to root later, in build-disk.sh)..."
chmod 4755 "$PASSWD_OUT"
echo "[✓] passwd mode set to setuid (rwsr-xr-x); will be root:root in the final image."

echo "[+] Creating symlinks for reboot, poweroff, halt..."
ln -sf shutdown "$SYSROOT/sbin/reboot"
ln -sf shutdown "$SYSROOT/sbin/poweroff"
ln -sf shutdown "$SYSROOT/sbin/halt"

echo "[✓] Symlinks created."
echo
echo "Installed binaries:"
ls -lh "$INIT_OUT" "$SHUTDOWN_OUT" "$DEVD_OUT" "$NET_OUT" "$LOGIN_OUT" "$PASSWD_OUT"
ls -la "$SYSROOT/sbin/reboot" "$SYSROOT/sbin/poweroff" "$SYSROOT/sbin/halt"

echo
echo "[✓] KratosOS system tools & authentication built successfully."
