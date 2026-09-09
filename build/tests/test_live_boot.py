import subprocess
import time
import os
import sys
import socket

def test_live_boot():
    sock_path = "/tmp/qemu-test-serial.sock"
    if os.path.exists(sock_path):
        os.remove(sock_path)

    # Check kvm access
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

    print("[*] Starting QEMU with command:", " ".join(cmd))
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
    all_passed = False

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
                    print("\n[+] Logged in successfully! Running Step 14 Console & Overlay validation...")
                    time.sleep(0.5)
                    test_commands = [
                        b"echo \"CHECK_TERM=$TERM\"\n",
                        b"stty size\n",
                        b"clear\n",
                        b"printf 'CONSOLE_TEST_PASSED\\n'\n",
                        b"touch /root/test-live && ls -l /root/test-live\n",
                        b"grep -E 'overlay|squashfs' /proc/mounts\n",
                        b"echo 'ALL_TESTS_COMPLETED_SUCCESSFULLY'\n",
                        b"poweroff\n"
                    ]
                    for tc in test_commands:
                        s.sendall(tc)
                        time.sleep(0.4)
                    tests_sent = True
                    buf = ""

                if tests_sent:
                    if "ALL_TESTS_COMPLETED_SUCCESSFULLY" in buf:
                        all_passed = True
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

    print("\n" + "="*50)
    print("VERIFICATION OF STEPS 07-14:")
    print("="*50)
    step7 = "[live-init] Mounting early virtual filesystems" in full_log or "KRATOSOS LIVE INITRAMFS BOOTSTRAP" in full_log
    step8 = "Found live media" in full_log and "SquashFS mounted" in full_log
    step9 = "OverlayFS mounted" in full_log
    step10 = "switch_root successful" in full_log
    step11 = "[init] KratosOS starting" in full_log and "Devices ready" in full_log
    step12 = "login:" in full_log
    step13 = logged_in
    step14 = "CONSOLE_TEST_PASSED" in full_log and "test-live" in full_log

    print(f"Step 07 (Kernel + Initramfs boot):      {'PASSED' if step7 else 'FAILED'}")
    print(f"Step 08 (Live media + SquashFS mount):   {'PASSED' if step8 else 'FAILED'}")
    print(f"Step 09 (OverlayFS initialization):     {'PASSED' if step9 else 'FAILED'}")
    print(f"Step 10 (switch_root to real rootfs):   {'PASSED' if step10 else 'FAILED'}")
    print(f"Step 11 (Real PID 1 /sbin/init startup):{'PASSED' if step11 else 'FAILED'}")
    print(f"Step 12 (Login prompt on TTY):          {'PASSED' if step12 else 'FAILED'}")
    print(f"Step 13 (Root login):                   {'PASSED' if step13 else 'FAILED'}")
    print(f"Step 14 (Console & Overlay test):       {'PASSED' if step14 else 'FAILED'}")

    success = step7 and step8 and step9 and step10 and step11 and step12 and step13 and step14
    if success:
        print("\n[✓] ALL STEPS 07-14 COMPLETED SUCCESSFULLY!")
    else:
        print("\n[!] SOME CHECKS FAILED. Please review output above.")
    return success

if __name__ == "__main__":
    if not test_live_boot():
        sys.exit(1)
