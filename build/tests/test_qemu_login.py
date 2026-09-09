import subprocess
import time
import os
import sys

def test_qemu_login():
    cmd = [
        "qemu-system-x86_64",
        "-m", "2G",
        "-cdrom", "build/images/kratosos.iso",
        "-boot", "d",
        "-enable-kvm",
        "-cpu", "host",
        "-display", "none",
        "-serial", "pty"
    ]
    
    # We can also use a socket or pipe for serial
    # Socket is very reliable and clean!
    sock_path = "/tmp/qemu-test-serial.sock"
    if os.path.exists(sock_path):
        os.remove(sock_path)
        
    cmd = [
        "qemu-system-x86_64",
        "-m", "2G",
        "-cdrom", "build/images/kratosos.iso",
        "-boot", "d",
        "-enable-kvm",
        "-cpu", "host",
        "-display", "none",
        "-serial", f"unix:{sock_path},server,nowait"
    ]
    
    print("[*] Starting QEMU process...")
    proc = subprocess.Popen(cmd)
    
    time.sleep(1)
    
    import socket
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    for _ in range(10):
        try:
            s.connect(sock_path)
            print("[*] Connected to serial socket!")
            break
        except Exception:
            time.sleep(0.5)
            
    s.settimeout(2.0)
    
    buf = ""
    start_time = time.time()
    logged_in = False
    
    while time.time() - start_time < 60:
        try:
            data = s.recv(1024).decode('utf-8', errors='ignore')
            if data:
                sys.stdout.write(data)
                sys.stdout.flush()
                buf += data
                
                if "login:" in buf and not logged_in:
                    print("\n[+] Found login prompt! Sending 'root'...")
                    time.sleep(0.5)
                    s.sendall(b"root\n")
                    logged_in = True
                    buf = ""
                    
                if logged_in and ("#" in buf or "root@" in buf or "~#" in buf or "bash" in buf):
                    print("\n[+] Logged in! Running tests...")
                    time.sleep(0.5)
                    
                    commands = [
                        b"ls -la /run/dbus/\n",
                        b"ps | grep dbus-daemon\n",
                        b"cat ~/.xinitrc\n",
                        b"startx &\n",
                        b"sleep 6\n",
                        b"ps | grep -E 'Xorg|xfce|xfwm|panel|desktop|dbus'\n",
                        b"grep -i -E 'modesetting|screen|libinput' /var/log/Xorg.0.log | head -n 15\n",
                        b"printf 'XFCE_SESSION_LAUNCH_SUCCESS\\n'\n",
                        b"pkill -f xfce4 || true\n",
                        b"pkill -f Xorg || true\n",
                        b"poweroff\n"
                    ]

                    for c in commands:
                        s.sendall(c)
                        time.sleep(0.5)
                        
                    # Wait for execution and poweroff
                    while time.time() - start_time < 90:
                        try:
                            d = s.recv(1024).decode('utf-8', errors='ignore')
                            if d:
                                sys.stdout.write(d)
                                sys.stdout.flush()
                                if "Power down" in d or "reboot: System halted" in d:
                                    break
                        except socket.timeout:
                            continue
                    break
        except socket.timeout:
            continue
            
    # Read remaining output
    try:
        while True:
            data = s.recv(1024).decode('utf-8', errors='ignore')
            if not data:
                break
            sys.stdout.write(data)
            sys.stdout.flush()
    except Exception:
        pass
        
    s.close()
    try:
        if os.path.exists(sock_path):
            os.remove(sock_path)
    except OSError:
        pass
        
    try:
        proc.terminate()
        proc.wait(timeout=3)
    except Exception:
        pass
    print("\n[*] QEMU test finished cleanly.")

if __name__ == "__main__":
    test_qemu_login()
