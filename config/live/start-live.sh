#!/bin/bash
# /etc/live/start-live.sh — Live Environment Initialization and Graphical Boot
#

echo "[Live] Initializing KratosOS Live Environment..."

LIVE_USER="kratos-live"
LIVE_HOME="/home/kratos-live"
LIVE_UID=1000

# 1. Hardware Wait Loop (Parrot OS style)
# Real hardware / VirtualBox can be slower at initializing DRM/KMS drivers.
echo "[Live] Waiting for graphics device..."
READY=0
for i in $(seq 1 15); do
    if [ -e /dev/dri/card0 ] || [ -e /dev/fb0 ]; then
        echo "[Live] Graphics device ready."
        READY=1
        break
    fi
    sleep 1
done

if [ "$READY" -eq 0 ]; then
    echo "[Live] Warning: no graphics device detected after 15s. X might fall back to VESA/FBDEV."
fi

# 2. Configure system permissions and directories for X11 & D-Bus
chmod 1777 /tmp 2>/dev/null || true
mkdir -p /var/log /var/lib/xkb /etc/X11 /etc/X11/xorg.conf.d
chmod 777 /var/log /var/lib/xkb 2>/dev/null || true
chmod 4755 /usr/bin/Xorg 2>/dev/null || true

cat > /etc/X11/Xwrapper.config << 'EOF'
allowed_users = anybody
needs_root_rights = yes
EOF

mkdir -p /run/dbus /run/user/0 "/run/user/$LIVE_UID"
chown 18:18 /run/dbus 2>/dev/null || true

if id "$LIVE_USER" >/dev/null 2>&1; then
    SESSION_USER="$LIVE_USER"
    SESSION_HOME="$LIVE_HOME"
    SESSION_RUNTIME="/run/user/$LIVE_UID"
    mkdir -p "$SESSION_HOME" "$SESSION_HOME/Desktop" "$SESSION_RUNTIME"
    chown -R "$LIVE_USER:$LIVE_USER" "$SESSION_HOME" "$SESSION_RUNTIME" 2>/dev/null || true
    chmod 700 "$SESSION_RUNTIME"
else
    echo "[Live] User $LIVE_USER not found, falling back to root."
    SESSION_USER="root"
    SESSION_HOME="/root"
    SESSION_RUNTIME="/run/user/0"
    mkdir -p "$SESSION_HOME" "$SESSION_HOME/Desktop" "$SESSION_RUNTIME"
    chmod 700 "$SESSION_RUNTIME"
fi

export XDG_RUNTIME_DIR="$SESSION_RUNTIME"

if command -v udevd >/dev/null 2>&1 && command -v udevadm >/dev/null 2>&1; then
    echo "[Live] Starting udev device enumeration..."
    mkdir -p /run/udev
    if [ ! -S /run/udev/control ]; then
        udevd --daemon >/var/log/udevd.log 2>&1 || true
    fi
    udevadm trigger --action=add >/dev/null 2>&1 || true
    udevadm settle >/dev/null 2>&1 || true
fi

if command -v dbus-daemon >/dev/null 2>&1 && [ ! -e /run/dbus/system_bus_socket ]; then
    echo "[Live] Starting system D-Bus daemon..."
    dbus-daemon --system --fork 2>/dev/null || true
    sleep 1
fi

if command -v gdk-pixbuf-query-loaders >/dev/null 2>&1; then
    echo "[Live] Updating gdk-pixbuf loader cache..."

    GDK_PIXBUF_LIBDIR=""
    for d in /usr/lib64/gdk-pixbuf-2.0/* /usr/lib/gdk-pixbuf-2.0/*; do
        if [ -d "$d" ] && [ -d "$d/loaders" ]; then
            GDK_PIXBUF_LIBDIR="$d"
            break
        fi
    done

    if [ -n "$GDK_PIXBUF_LIBDIR" ]; then
        export GDK_PIXBUF_MODULEDIR="$GDK_PIXBUF_LIBDIR/loaders"
        export GDK_PIXBUF_MODULE_FILE="$GDK_PIXBUF_LIBDIR/loaders.cache"
        mkdir -p "$GDK_PIXBUF_MODULEDIR" 2>/dev/null || true

        if ! gdk-pixbuf-query-loaders > "$GDK_PIXBUF_MODULE_FILE" 2>/var/log/gdk-pixbuf-query-loaders.log; then
            echo "[Live] Warning: could not regenerate gdk-pixbuf loader cache" >> /var/log/gdk-pixbuf-query-loaders.log 2>&1 || true
        fi
    else
        if ! gdk-pixbuf-query-loaders --update-cache >/var/log/gdk-pixbuf-query-loaders.log 2>&1; then
            echo "[Live] Warning: could not update gdk-pixbuf loader cache" >> /var/log/gdk-pixbuf-query-loaders.log 2>&1 || true
        fi
    fi
fi

# 3. Setup session home environment and synchronize xinitrc across all profiles
echo "[Live] Preparing $SESSION_USER desktop..."
for dest in /etc/X11/xinit/xinitrc /root/.xinitrc "$SESSION_HOME/.xinitrc" /etc/skel/.xinitrc; do
    mkdir -p "$(dirname "$dest")"
    cp -f /etc/live/xinitrc "$dest" 2>/dev/null || true
    chmod +x "$dest" 2>/dev/null || true
done

if [ -f /etc/live/kratosos-live.desktop ]; then
    cp -f /etc/live/kratosos-live.desktop "$SESSION_HOME/Desktop/" 2>/dev/null || true
    chmod +x "$SESSION_HOME/Desktop/kratosos-live.desktop" 2>/dev/null || true
    cp -f /etc/live/kratosos-live.desktop "/root/Desktop/" 2>/dev/null || true
    chmod +x "/root/Desktop/kratosos-live.desktop" 2>/dev/null || true
fi

chown -R "$SESSION_USER:$SESSION_USER" "$SESSION_HOME" 2>/dev/null || true

# 4. Launch X11 GUI
if command -v startx >/dev/null 2>&1; then
    echo "[Live] Starting graphical XFCE session as $SESSION_USER..."
    mkdir -p /var/log
    echo "[Live] Invoking startx..." >> /var/log/Xorg.start.log

    TARGET_VT=7
    if [ -x /sbin/kratos-vtswitch ]; then
        /sbin/kratos-vtswitch "$TARGET_VT" || echo "[Live] vtswitch failed, X might stay invisible" >> /var/log/Xorg.start.log
    fi

    STARTX_CMD="export HOME=$SESSION_HOME USER=$SESSION_USER LOGNAME=$SESSION_USER XDG_RUNTIME_DIR=$SESSION_RUNTIME XDG_SESSION_TYPE=x11; exec startx /etc/live/xinitrc -- vt$TARGET_VT -novtswitch -keeptty -logverbose 6"
    TARGET_TTY="/dev/tty$TARGET_VT"

    STARTX_RC=1
    if [ -c "$TARGET_TTY" ] && command -v setsid >/dev/null 2>&1; then
        if [ "$SESSION_USER" != "root" ]; then
            setsid --ctty --wait su - "$SESSION_USER" -c "$STARTX_CMD" <"$TARGET_TTY" >>/var/log/Xorg.start.log 2>&1
        else
            setsid --ctty --wait /bin/bash -c "$STARTX_CMD" <"$TARGET_TTY" >>/var/log/Xorg.start.log 2>&1
        fi
        STARTX_RC=$?
    elif [ "$SESSION_USER" != "root" ]; then
        echo "[Live] Cannot establish controlling TTY $TARGET_TTY" >> /var/log/Xorg.start.log
        su - "$SESSION_USER" -c "$STARTX_CMD" >>/var/log/Xorg.start.log 2>&1
        STARTX_RC=$?
    fi

    # Fallback to root X session if unprivileged startx failed or if root session was selected
    if [ "$STARTX_RC" -ne 0 ]; then
        echo "[Live] Starting/falling back to root X session..." >> /var/log/Xorg.start.log
        SESSION_USER="root"
        SESSION_HOME="/root"
        SESSION_RUNTIME="/run/user/0"
        mkdir -p "$SESSION_HOME" "$SESSION_HOME/Desktop" "$SESSION_RUNTIME"
        chmod 700 "$SESSION_RUNTIME"
        STARTX_CMD="export HOME=$SESSION_HOME USER=$SESSION_USER LOGNAME=$SESSION_USER XDG_RUNTIME_DIR=$SESSION_RUNTIME XDG_SESSION_TYPE=x11; exec startx /etc/live/xinitrc -- vt$TARGET_VT -novtswitch -keeptty -logverbose 6"
        if [ -c "$TARGET_TTY" ] && command -v setsid >/dev/null 2>&1; then
            setsid --ctty --wait /bin/bash -c "$STARTX_CMD" <"$TARGET_TTY" >>/var/log/Xorg.start.log 2>&1
        else
            eval "$STARTX_CMD" >>/var/log/Xorg.start.log 2>&1
        fi
        STARTX_RC=$?
    fi

    if [ "$STARTX_RC" -ne 0 ]; then
        echo "[Live] ERROR: startx exited $STARTX_RC. Checking logs..."
        for log in /var/log/Xorg.0.log /var/log/Xorg.start.log; do
            if [ -f "$log" ]; then
                echo "[Live] --- $log ---"
                cat "$log"
            fi
        done
    fi
else
    echo "[Live] ERROR: 'startx' not found. Graphical session cannot start."
    echo "[Live] Please ensure 'xorg-server' and 'xinit' packages are installed in the sysroot."
fi
