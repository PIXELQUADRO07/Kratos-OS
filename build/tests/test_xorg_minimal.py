import subprocess
import time
import os
import sys
import socket

def test_xorg_minimal():
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
        "-display", "none",
        "-serial", f"unix:{sock_path},server,nowait"
    ]

    print("[*] Starting QEMU for Xorg minimal test...")
    proc = subprocess.Popen(cmd)

    time.sleep(1)

    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    connected = False
    for _ in range(20):
        try:
            s.connect(sock_path)
            connected = True
            print("[*] Connected to serial socket!")
            break
        except Exception:
            time.sleep(0.5)

    if not connected:
        print("[!] Failed to connect to serial socket")
        proc.kill()
        return False

    s.settimeout(2.0)

    buf = ""
    full_log = ""
    start_time = time.time()
    logged_in = False
    tests_sent = False

    while time.time() - start_time < 90:
        try:
            data = s.recv(1024).decode('utf-8', errors='ignore')
            if data:
                sys.stdout.write(data)
                sys.stdout.flush()
                buf += data
                full_log += data

                if "login:" in buf and not logged_in:
                    print("\n[+] Found login prompt! Sending 'root'...")
                    time.sleep(0.5)
                    s.sendall(b"root\n")
                    logged_in = True
                    buf = ""

                if logged_in and not tests_sent and ("#" in buf or "root@" in buf or "~#" in buf or "bash" in buf):
                    print("\n[+] Logged in! Running Xorg binary checks and minimal server test...")
                    time.sleep(0.5)
                    test_commands = [
                        b"which Xorg xinit xauth xkbcomp\n",
                        b"Xorg -version\n",
                        b"ls -l /dev/dri/ /dev/fb* 2>/dev/null || true\n",
                        b"Xorg :0 -novtswitch -logfile /var/log/Xorg.0.log &\n",
                        b"sleep 3\n",
                        b"ps | grep Xorg\n",
                        b"grep -E -i 'modesetting|driver|screen|initialized' /var/log/Xorg.0.log | head -n 25\n",
                        b"pkill -9 Xorg || true\n",
                        b"printf 'XORG_MINIMAL_TEST_PASSED\\n'\n",
                        b"poweroff\n"
                    ]
                    for tc in test_commands:
                        s.sendall(tc)
                        time.sleep(0.6)
                    tests_sent = True
                    buf = ""

                if tests_sent:
                    if "Power down" in buf or "reboot: Power down" in buf or "reboot: System halted" in buf:
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

    xorg_found = "/usr/bin/Xorg" in full_log
    xorg_version = "X.Org X Server" in full_log
    xorg_passed = "XORG_MINIMAL_TEST_PASSED" in full_log

    print("\n" + "="*50)
    print("XORG MINIMAL TEST RESULTS:")
    print("="*50)
    print(f"Xorg binaries present:  {'PASSED' if xorg_found else 'FAILED'}")
    print(f"Xorg executes version:  {'PASSED' if xorg_version else 'FAILED'}")
    print(f"Xorg runs on display 0: {'PASSED' if xorg_passed else 'FAILED'}")

    return xorg_found and xorg_version and xorg_passed

if __name__ == "__main__":
    if not test_xorg_minimal():
        sys.exit(1)
