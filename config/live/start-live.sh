#!/bin/bash
# /etc/live/start-live.sh — Live Environment Initialization and Graphical Boot

echo "[Live] Initializing KratosOS Live Environment..."

# 1. Ensure /run/dbus directory and system dbus are available
mkdir -p /run/dbus /run/user/1000
chown 18:18 /run/dbus 2>/dev/null || true
chown kratos-live:kratos-live /run/user/1000 2>/dev/null || true

if command -v dbus-daemon >/dev/null 2>&1 && [ ! -e /run/dbus/system_bus_socket ]; then
    echo "[Live] Starting system D-Bus daemon..."
    dbus-daemon --system --fork 2>/dev/null || true
fi

# 2. Setup user home environment
if [ -d /home/kratos-live ]; then
    mkdir -p /home/kratos-live/Desktop
    if [ -f /etc/live/kratosos-live.desktop ]; then
        cp /etc/live/kratosos-live.desktop /home/kratos-live/Desktop/
        chmod +x /home/kratos-live/Desktop/kratosos-live.desktop
    fi
    cp /etc/live/xinitrc /home/kratos-live/.xinitrc 2>/dev/null || true
    chmod +x /home/kratos-live/.xinitrc
    chown -R kratos-live:kratos-live /home/kratos-live 2>/dev/null || true
fi

# 3. Launch X11 GUI as kratos-live user on VT7
#
# Xorg normally switches the active VT to itself once it finishes
# initializing — but that requires CAP_SYS_TTY_CONFIG (effectively root)
# or a running systemd-logind session to grant it. KratosOS has neither:
# X is started as the unprivileged kratos-live user below, and there is
# no logind. So Xorg's own self-switch silently never happens: X ends
# up running correctly but invisibly on VT7, while the console just
# looks frozen on whatever was last printed — indistinguishable, from
# the screen alone, from the machine actually hanging.
#
# Fix: switch to VT7 explicitly, as root, via kratos-vtswitch, BEFORE
# starting X — so by the time Xorg starts, VT7 is already the active
# one and Xorg doesn't need a switch permission it doesn't have. If X
# still doesn't come up within the timeout, switch back to the console
# VT (1 — the VT login.c autologins kratos-live on) and print Xorg's
# own log, which it always writes to disk regardless of what's
# happening on the terminal, so a real failure is visible instead of
# just a blank/stuck-looking screen.
if command -v startx >/dev/null 2>&1; then
    HAVE_VTSWITCH=0
    if command -v kratos-vtswitch >/dev/null 2>&1; then
        HAVE_VTSWITCH=1
        echo "[Live] Switching to VT7 before starting X..."
        kratos-vtswitch 7 || echo "[Live] Warning: could not switch to VT7 (continuing anyway)."
    else
        echo "[Live] Warning: kratos-vtswitch not found — X may start invisibly."
    fi

    echo "[Live] Starting graphical XFCE session..."
    su - kratos-live -c "startx /etc/live/xinitrc -- vt7" &

    READY=0
    for i in $(seq 1 30); do
        if [ -e /tmp/.X11-unix/X0 ]; then
            READY=1
            break
        fi
        sleep 1
    done

    if [ "$READY" -eq 1 ]; then
        echo "[Live] X server is up."
    else
        echo "[Live] X server did NOT come up within 30s."
        if [ "$HAVE_VTSWITCH" -eq 1 ]; then
            echo "[Live] Switching back to VT1..."
            kratos-vtswitch 1 || true
        fi
        echo "[Live] Xorg log (if any):"
        for log in /home/kratos-live/.local/share/xorg/Xorg.0.log /var/log/Xorg.0.log; do
            if [ -f "$log" ]; then
                echo "[Live] --- $log ---"
                tail -n 40 "$log"
                break
            fi
        done
    fi
fi
