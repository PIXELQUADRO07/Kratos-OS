import subprocess
import time
import os
import sys
import socket

def test_live_xfce():
    sock_path = "/tmp/qemu-test-serial.sock"
    if os.path.exists(sock_path):
        os.remove(sock_path)

    kvm_args = ["-enable-kvm", "-cpu", "host"] if os.access("/dev/kvm", os.W_OK) else ["-cpu", "qemu64"]

    cmd = [
        "qemu-system-x86_64",
        "-m", "2G",
        "-cdrom", "build/images/kratosos.iso",
        "-boot", "d",
        *kvm_args,
        "-vga", "std",
        "-display", "none",
        "-serial", f"unix:{sock_path},server,nowait"
    ]

    print("[*] Starting QEMU for Live XFCE session test...")
    proc = subprocess.Popen(cmd)

    time.sleep(1)

    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    for _ in range(20):
        try:
            s.connect(sock_path)
            print("[*] Connected to serial socket!")
            break
        except Exception:
            time.sleep(0.5)

    s.settimeout(2.0)
    buf = ""
    full_log = ""
    start_time = time.time()
    logged_in = False
    tests_sent = False

    while time.time() - start_time < 120:
        try:
            data = s.recv(1024).decode('utf-8', errors='ignore')
            if data:
                sys.stdout.write(data)
                sys.stdout.flush()
                buf += data
                full_log += data

                if "login:" in buf and not logged_in:
                    print("\n[+] Found login prompt! Waiting 6 seconds for XFCE startup then logging in as 'root'...")
                    time.sleep(6)
                    s.sendall(b"root\n")
                    logged_in = True
                    buf = ""

                if logged_in and not tests_sent and ("#" in buf or "root@" in buf or "~#" in buf or "bash" in buf):
                    print("\n[+] Logged in! Collecting complete live logs and validating processes...")
                    time.sleep(1)
                    test_commands = [
                        b"echo '=== [LOG 1: PROCESS LIST (ps aux)] ==='\n",
                        b"ps aux\n",
                        b"echo '=== [LOG 2: /run/boot.log (RC Services)] ==='\n",
                        b"cat /run/boot.log 2>/dev/null || echo '(boot.log empty or not found)'\n",
                        b"echo '=== [LOG 3: /var/log/Xorg.start.log (Startx & Session)] ==='\n",
                        b"cat /var/log/Xorg.start.log 2>/dev/null || echo '(Xorg.start.log not found)'\n",
                        b"echo '=== [LOG 4: /var/log/Xorg.0.log (Xorg Drivers & Screens)] ==='\n",
                        b"grep -E 'EE|WW|modeset|bochs|vbox|vmw|card|Output' /var/log/Xorg.0.log 2>/dev/null || cat /var/log/Xorg.0.log\n",
                        b"echo '=== [LOG 5: /var/log/gdk-pixbuf-query-loaders.log] ==='\n",
                        b"cat /var/log/gdk-pixbuf-query-loaders.log 2>/dev/null || echo '(no gdk-pixbuf log)'\n",
                        b"echo '=== [LOG 6: .xsession-errors] ==='\n",
                        b"cat /home/kratos-live/.xsession-errors /root/.xsession-errors 2>/dev/null || echo '(no .xsession-errors)'\n",
                        b"echo '=== [LOG 7: GRAPHICS & INPUT DEVICES] ==='\n",
                        b"ls -la /dev/dri /dev/fb* /dev/input 2>/dev/null\n",
                        b"echo '=== [LOG 8: KERNEL DRM & HYPERVISOR DMESG] ==='\n",
                        b"dmesg | grep -E -i 'drm|bochs|vbox|vmw|fb0|modeset|input' | tail -n 30\n",
                        b"printf 'XFCE_VALIDATION_COMPLETED_SUCCESSFULLY\\n'\n",
                        b"poweroff\n"
                    ]
                    for tc in test_commands:
                        s.sendall(tc)
                        time.sleep(1.0)
                    tests_sent = True
                    buf = ""

                if tests_sent:
                    if "Power down" in buf or "reboot: System halted" in buf:
                        print("\n[+] System powered down cleanly.")
                        break
        except socket.timeout:
            continue
        except Exception as e:
            print(f"\n[!] Socket error: {e}")
            break

    s.close()
    try:
        if os.path.exists(sock_path):
            os.remove(sock_path)
    except OSError:
        pass
    try:
        proc.terminate()
        proc.wait(timeout=5)
    except Exception:
        proc.kill()

    has_xorg = "Xorg" in full_log
    has_dbus = "dbus-daemon" in full_log
    has_xfce = "xfce4-session" in full_log
    test_passed = "XFCE_VALIDATION_COMPLETED_SUCCESSFULLY" in full_log

    print("\n" + "="*50)
    print("LIVE XFCE TEST RESULTS:")
    print("="*50)
    print(f"Xorg server active:         {'PASSED' if has_xorg else 'FAILED'}")
    print(f"D-Bus system/session bus:   {'PASSED' if has_dbus else 'FAILED'}")
    print(f"XFCE session launched:      {'PASSED' if has_xfce else 'FAILED'}")
    print(f"Overall test sequence:      {'PASSED' if test_passed else 'FAILED'}")

    return has_xorg and has_dbus and has_xfce and test_passed

if __name__ == "__main__":
    if not test_live_xfce():
        sys.exit(1)
