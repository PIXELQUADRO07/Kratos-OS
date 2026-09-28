# KratosOS

<div align="center">
<img width="568" height="468" alt="Grub-logo" src="https://github.com/user-attachments/assets/9c2e3523-5a12-4ab1-9f00-594cd0817043" />

</div>   

<p align="center">
  <b>An independent GNU/Linux distribution built from the ground up.</b>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/architecture-x86__64-blue">
  <img src="https://img.shields.io/badge/version-v1.5.1-orange">
  <img src="https://img.shields.io/badge/boot-UEFI%20%2F%20GPT-green">
  <img src="https://img.shields.io/badge/kernel-Linux%207.1.5-lightgrey">
  <img src="https://img.shields.io/badge/license-GPL--3.0-blue">
</p>

---

## 🌟 About KratosOS

**KratosOS** is an independent GNU/Linux operating system built from scratch without relying on Debian, Arch Linux, Ubuntu, Alpine, or any upstream base distribution.

Every foundational component is compiled from sources:
- **Dedicated Cross-Toolchain**: GCC 15.2.0, glibc 2.42, Binutils 2.45
- **Hardened Linux Kernel**: Linux 7.1.5 with native ext4, VFAT, and device support
- **Modular PID 1 Init System**: Custom native init engine with service management, TTY supervision, and zombie reaping
- **Native Device Management Daemon**: `kratos-devd` via Netlink `NETLINK_KOBJECT_UEVENT`
- **Native Userspace Network & DHCP Client**: `kratos-net`
- **Native Authentication & Password Cryptography**: `login`, `passwd`, `su`, `useradd`, `userdel`, `usermod`, `groupadd`, `groupdel` with standalone SHA-512crypt
- **Complete System Utilities**: Native `mount`/`umount`, `ps`, `kill`, `dmesg`, `free`, `df`, `hostname`, `id`, `groups`, `whoami`
- **Hardened Package Management Ecosystem (KPM)**: `kratos` CLI, `kratos-pkg` engine, `kratos-pack`, `kratos-fetch` HTTPS client with TLS, and `kratos-json` parser

---

## 📊 Current Status & Feature Matrix

### ✅ Implemented & Working

| Component | Subsystem | Description |
| :--- | :--- | :--- |
| **Toolchain** | Bootstrap | x86_64 cross-compiler (GCC 15.2.0, glibc 2.42, Binutils 2.45) |
| **Boot & Kernel** | UEFI / GPT | Linux 7.1.5 kernel, GRUB 2.14 EFI, GPT partitioning, UUID & PARTUUID auto-detection |
| **PID 1 Init** | Core Userspace | Modular PID 1, VFS mounting (`/proc`, `/sys`, `/dev`, `/run`), `/etc/fstab` parser, hostname config |
| **Process Control** | Supervision | Signal handling, zombie reaping, TTY supervision, shutdown/reboot/poweroff |
| **Device Manager** | `kratos-devd` | Netlink kernel uevent socket, coldplug discovery, dynamic `/dev`, group resolution via `/etc/group`, disk symlinks (`by-uuid`, `by-label`), auto-modprobe |
| **Networking** | `kratos-net` | Loopback (`127.0.0.1/8`), physical interface discovery, native DHCP client with XID validation, `/etc/resolv.conf` DNS generation |
| **Authentication** | Users & Security | `/bin/login`, `/usr/bin/passwd`, `/bin/su`, `/usr/sbin/useradd`, `/usr/sbin/userdel`, `/usr/sbin/usermod`, `/usr/sbin/groupadd`, `/usr/sbin/groupdel`, standalone SHA-512crypt |
| **System Utilities** | Core Userspace | Native `mount`/`umount`, `ps`, `kill`, `dmesg`, `free`, `df`, `hostname`, `id`, `groups`, `whoami` — all implemented from scratch |
| **Package Manager** | `kratos` / KPM | In-process safe tar extraction, Zip-Slip & traversal defense, SHA-256 integrity checksums, dependency solver & constraints, pre/post install hooks |
| **HTTPS Client** | `kratos-fetch` | Native HTTP/HTTPS network client using mbedTLS and CA certificate verification |
| **JSON Engine** | `kratos-json` | Zero-dependency recursive descent JSON parser for repository indexes |
| **Test Suite** | Testing & CI | Automated test suite (`make test`) covering cryptographic hashing, JSON parsing, dependency resolution, and security exploit defenses |
| **X11 / XFCE** | Desktop (Phase 4–5) | Modesetting Xorg, live `startx` session, XFCE with compositor disabled for QEMU/KMS |
| **Calamares** | Installer (Phase 6) | Graphical installer configuration (KPMCore); native `kratos-install` is still in development |

### 🛠️ In Active Development

- [ ] Standalone system installer (`kratos-install`)
- [ ] Physical bare-metal hardware compatibility testing

### ✅ Recently Completed

- [x] Package signing and verification (Ed25519 via mbedTLS), keygen and sign-repo tools
- [x] Bootable ISO image generation (`make iso`) with GRUB Live CD and initramfs
- [x] Remote package repository synchronization (`kratos update`, `kratos search`, `kratos upgrade`)
- [x] Repository index format (`index.json`) with HTTPS fetch and local cache
- [x] Multi-repository support (`/etc/kratos/repos.d/`)
- [x] Distro version centralized in the top-level `VERSION` file (README, os-release, GRUB, Calamares)
- [x] Live ISO graphical session (`make iso`) and optional graphical disk (`make desktop-disk`)
- [x] Calamares graphical installer config as the current install path until `kratos-install` ships

### Installation Bundles (Prefixes)

KratosOS supports metapackages to install sets of related tools in one command:

- `all`: Full installation of all recommended packages.
- `core`: Essential system utilities, toolchain, and monitoring tools.
- `networking`: Networking tools and connectivity utilities.
- `cybersecurity`: Penetration testing, forensics, and privacy tools.
- `development`: Editors, compilers, runtimes, and container tools.
- `productivity`: Multimedia, graphics, and communication apps.
- `top10`: The most essential packages for a functional system.

---

## 🏗️ System Architecture

```text
                                 ┌────────────────────────┐
                                 │     UEFI Firmware      │
                                 └───────────┬────────────┘
                                             │
                                             ▼
                                 ┌────────────────────────┐
                                 │       GRUB 2.14        │
                                 └───────────┬────────────┘
                                             │
                                             ▼
                                 ┌────────────────────────┐
                                 │   Linux Kernel 7.1.5   │
                                 └───────────┬────────────┘
                                             │
                                             ▼
                                 ┌────────────────────────┐
                                 │   /sbin/init (PID 1)   │
                                 └─────┬──────┬──────┬────┘
                                       │      │      │
          ┌────────────────────────────┘      │      └────────────────────────────┐
          ▼                                   ▼                                   ▼
┌──────────────────┐               ┌──────────────────┐               ┌──────────────────┐
│ Virtual FS Mount │               │  kratos-devd     │               │  kratos-net      │
│ /proc, /sys,     │               │  Device Daemon   │               │  Network & DHCP  │
│ /dev, /run, /tmp │               │  (Netlink)       │               │  Auto-Config     │
└──────────────────┘               └──────────────────┘               └──────────────────┘
                                              │
                                              ▼
                                   ┌──────────────────┐
                                   │ TTY Supervision  │
                                   │ /bin/login       │
                                   └──────────┬───────┘
                                              │
                                              ▼
                                   ┌──────────────────┐
                                   │  GNU Bash Shell  │
                                   └──────────────────┘
```

---

## 📦 Kratos Package Manager (KPM)

KratosOS features a secure, standalone native package manager:

```text
kpkg archive
   │
   ├── metadata       (Format v2, dependencies, conflicts, provides, ABI)
   ├── manifest       (Tracked file list)
   ├── checksums      (SHA-256 hashes for all archive components)
   ├── payload.tar.gz (Gzip payload extracted via in-process safe tar)
   └── hooks/         (pre-install, post-install, pre-remove, post-remove)
```

### CLI Usage

```bash
# Install a package with dependency and checksum verification
kratos install bash

# Install an installation bundle (prefix)
kratos install networking

# Verify installed package files against DB manifest
kratos verify bash

# Inspect package details
kratos info bash

# List all installed packages
kratos list

# Remove an installed package and execute removal hooks
kratos remove package-name
```

### Security Defenses Built-In

1. **In-Process Tar Extractor**: Replaces external `system("tar")` with strict POSIX ustar parsing.
2. **Zip-Slip & Path Traversal Mitigation**: Disallows any entries containing `..`, leading `/`, or attempting to escape the target sysroot.
3. **Symlink Escape Protection**: Symlink targets pointing outside the destination root are rejected.
4. **Device Node Protection**: Rejects block/char device creation in unprivileged payloads.
5. **SHA-256 Integrity Verification**: Cryptographic validation of metadata, manifests, and payloads.

---

## 🚀 Quick Start & Building

### Host Requirements

KratosOS can be built on any Linux x86_64 host. The build system requires the following tools; the host distribution doesn't matter — only the resulting commands do.

#### 1. Install host dependencies

```bash
# Automatic install (detects your distro):
sudo ./build/scripts/install-host-deps.sh

# Or via make:
make host-deps
```

Supported package managers:

| Distribution | Package Manager |
|---|---|
| Arch / Manjaro / EndeavourOS | `pacman` |
| Fedora | `dnf` |
| Debian / Ubuntu *(Phase 2, not yet fully tested)* | `apt` |

#### 2. Verify host compatibility

```bash
# Check all required tools are present:
./build/scripts/check-host-deps.sh

# Or via make / build.sh:
make check-host
./build.sh --check-host
```

Expected output on a ready host:

```
KratosOS Host Compatibility Check
==================================
  Host OS:      Fedora Linux 44
  Architecture: x86_64

  Command                       Status
  -------                       ------
  [✓] bash        [✓] gcc         [✓] g++
  [✓] make        [✓] bison       [✓] flex
  ...
  [✓] qemu-system-x86_64

  [✓] Host is compatible with KratosOS build system.
```

#### 3. Build

```bash
# Full build — all phases, incremental, quiet progress bar:
make all

# Full verbose output (legacy behaviour):
make all VERBOSE=1

# Rebuild everything from scratch:
make all CLEAN=1
```

Build output in quiet mode (default) shows a live progress bar:

```
  KratosOS — Full Build  (41 stages)
  Jobs: 16  |  Stamps: build/.stamps

  [████████████░░░░░░░░░░░░] 12/41   29%  gcc-pass2  GCC pass 2 (C + C++ + libstdc++)
```

Full script output is always saved to `build/build.log`.

### Individual Build Targets

```bash
make phase1         # Bootstrap toolchain (binutils, gcc, glibc)
make phase2         # Base userspace (bash, coreutils, sed, grep, tar, etc.)
make phase3         # Kernel, GRUB, init, KPM, fetch, console disk image
make phase4         # X11 stack configuration (does not compile Xorg)
make phase5         # XFCE desktop configuration
make phase6         # Calamares installer configuration
make inject-pkgs    # Install Xorg/XFCE binary packages into the sysroot
make desktop-disk   # inject-pkgs (desktop required) + phases 4–6 + disk
make iso            # Live ISO (injects desktop packages; GRUB kratos.live)

make init           # Build init, devd, net, login, passwd, su, useradd + all system utilities
make pkg            # Build kratos, kratos-pkg, kratos-pack
make disk           # Generate bootable GPT disk image (build/images/kratosos.img)
```

The distro version is the single line in [`VERSION`](VERSION). Keep the README badge, GRUB menus, `/etc/os-release`, and Calamares branding in sync with that file (build scripts substitute it).

---

## 🖥️ Running in QEMU

Test the generated disk image in QEMU with UEFI firmware:

```bash
./run-qemu.sh                      # serial console, disk image
./run-qemu.sh --graphic            # VGA window (virtio-vga)
./run-qemu.sh --iso --graphic      # Live ISO + XFCE
```

---

## 🧪 Automated Testing (`make test`)

KratosOS includes a built-in automated test suite verifying core native systems:

```bash
make test
```

Tests included:
- **`test-crypt`**: SHA-512crypt password hashing and Drepper test vectors
- **`test-json`**: JSON parser validation, surrogate pair UTF-8, escape sequences, depth bounds
- **`test-deps`**: Version comparator (`>=`, `<=`, `!=`), dependency graph solver, conflict detector
- **`test-repo`**: Repository client loading, package search, multi-repo index parsing
- **`test-sign`**: Ed25519 signature generation and verification (host stub)
- **`test-pkg-security`**: Path traversal exploits (`../`), device node injection, symlink escapes, SHA-256 verification

CI on GitHub runs `make test` on push and pull requests to `main`, in addition to the Flawfinder static scan.

---

## 📜 License

KratosOS is free software released under the **GNU General Public License v3.0**. See the [LICENSE](LICENSE) file for details.
