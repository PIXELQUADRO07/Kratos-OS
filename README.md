# KratosOS

<p align="center">
  <img width="400" height="400" alt="KratosOS Logo" src="https://github.com/user-attachments/assets/c82503fe-3256-4bf6-b5ae-f0a4d458f300" />
</p>

<p align="center">
  <b>An independent GNU/Linux distribution built from the ground up.</b>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/architecture-x86__64-blue">
  <img src="https://img.shields.io/badge/version-v0.7.8.4-orange">
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

### 🛠️ In Active Development

- [ ] Bootable ISO image generation (`make iso`)
- [ ] Standalone system installer (`kratos-install`)
- [ ] Physical bare-metal hardware compatibility testing
- [ ] Package signing (Ed25519 via mbedTLS)

### ✅ Recently Completed

- [x] Remote package repository synchronization (`kratos update`, `kratos search`, `kratos upgrade`)
- [x] Repository index format (`index.json`) with HTTPS fetch and local cache
- [x] Multi-repository support (`/etc/kratos/repos.d/`)

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
kratos install package-1.0.0-1-x86_64.kpkg

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

### Prerequisites (Host System)

- Linux host (x86_64)
- `gcc`, `g++`, `make`, `bison`, `flex`, `texinfo`, `gawk`, `wget`, `qemu-system-x86_64`

### Full Build (Incremental)

```bash
# Build the entire OS from scratch (all phases)
make all

# Run automated unit and security tests
make test
```

### Individual Build Targets

```bash
make phase1         # Bootstrap toolchain (binutils, gcc, glibc)
make phase2         # Base userspace (bash, coreutils, sed, grep, tar, etc.)
make phase3         # Kernel, GRUB, init system, packages, disk image

make init           # Build init, devd, net, login, passwd, su, useradd + all system utilities
make pkg            # Build kratos, kratos-pkg, kratos-pack
make disk           # Generate bootable GPT disk image (build/images/kratosos.img)
```

---

## 🖥️ Running in QEMU

Test the generated disk image in QEMU with UEFI firmware:

```bash
./run-qemu.sh
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
- **`test-pkg-security`**: Path traversal exploits (`../`), device node injection, symlink escapes, SHA-256 verification

---

## 📜 License

KratosOS is free software released under the **GNU General Public License v3.0**. See the [LICENSE](LICENSE) file for details.
