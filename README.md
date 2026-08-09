<div align="center">

<img width="500" height="500" alt="KRATOS_OS-removebg-preview" src="https://github.com/user-attachments/assets/955f5d47-345d-4f06-b463-4e11f301607b" />


# KratosOS

### A custom GNU/Linux operating system built from the ground up.

**KratosOS 0.1.0 — First Bootable Release**

</div>

---

## About

KratosOS is a custom GNU/Linux operating system project built from
source with its own build system, cross-toolchain, system initialization
and userspace infrastructure.

The project aims to build a complete and independent operating system
environment while keeping the system modular, transparent and
controllable at every layer.

KratosOS is currently under active development.

---

## Current Status

### v0.1.0 — First Bootable System ✅

KratosOS has successfully reached its first complete boot in QEMU.

Current boot chain:

```text
UEFI
 ↓
GRUB
 ↓
Linux Kernel 7.1.5
 ↓
/sbin/init
 ↓
Virtual Filesystems
 ↓
KratosOS Userspace
 ↓
/bin/bash
 ↓
Root Shell

The current system is capable of:

Booting through UEFI/GRUB
Loading the Linux kernel
Starting the native KratosOS PID 1
Mounting initial virtual filesystems
Starting Bash
Entering the KratosOS userspace as root
Running as a standalone disk image under QEMU
Features
Build System
Custom KratosOS build system
x86_64 cross-compilation
Dedicated KratosOS sysroot
Reproducible component builds
Automated disk image generation
Kernel
Linux 7.1.5
EFI stub support
devtmpfs
devtmpfs automatic mounting
EXT4
VFAT
Serial console support
Bootloader
GRUB 2.14
UEFI boot
GPT partition layout
EFI System Partition
KratosOS-specific GRUB configuration
Userspace
glibc
Bash
Native /sbin/init
/etc system skeleton
/etc/fstab
/etc/rc.sysinit
/etc/rc.d/
shutdown/reboot/halt utilities
System Architecture
                    ┌─────────────┐
                    │    UEFI     │
                    └──────┬──────┘
                           │
                    ┌──────▼──────┐
                    │    GRUB     │
                    └──────┬──────┘
                           │
                    ┌──────▼──────┐
                    │    Linux    │
                    │   Kernel    │
                    └──────┬──────┘
                           │
                    ┌──────▼──────┐
                    │    init     │
                    │    PID 1    │
                    └──────┬──────┘
                           │
              ┌────────────┼────────────┐
              │            │            │
           /proc         /sys          /dev
              │            │            │
              └────────────┼────────────┘
                           │
                    ┌──────▼──────┐
                    │  Userspace  │
                    │    Bash     │
                    └─────────────┘
Build Requirements

A Linux build environment is currently required.

Main tools:

GCC
GNU Make
GNU Binutils
Bash
cURL
GNU tar
GRUB tools
parted
mkfs.ext4
mkfs.fat
QEMU
KVM (recommended)
Building

Clone the repository:

git clone https://github.com/<your-user>/KratosOS.git
cd KratosOS

Build the required components using the KratosOS build system.

The generated system is placed inside:

build/sysroot/

The bootable disk image is generated at:

build/images/kratosos.img
Running with QEMU

KratosOS can currently be tested using QEMU.

Example:

sudo qemu-system-x86_64 \
    -enable-kvm \
    -m 2G \
    -smp 2 \
    -drive file=build/images/kratosos.img,format=raw \
    -nographic

The current serial console configuration allows the system to be
tested directly from the terminal.

Project Structure
KratosOS/
├── build/
│   ├── config/
│   ├── scripts/
│   ├── sources/
│   ├── tools/
│   ├── work/
│   └── sysroot/
│
├── init/
│   ├── init.c
│   └── shutdown.c
│
├── kernel/
│
├── packages/
│
├── scripts/
│
└── README.md
Roadmap
v0.1.0 — First Bootable System ✅
 Cross-toolchain
 GCC
 Binutils
 glibc
 Sysroot
 Linux kernel
 GRUB EFI
 GPT disk image
 Native PID 1
 Bash
 QEMU boot
 Root shell
v0.2.0 — Device Management
 kratos-devd
 Netlink NETLINK_KOBJECT_UEVENT
 Coldplug
 Hotplug
 Dynamic /dev
 Device permissions
 /dev/disk/by-uuid
 /dev/disk/by-label
v0.3.0 — Core Userspace
 Core utilities
 util-linux
 kmod
 Process utilities
 Compression utilities
 Initramfs
v0.4.0 — Networking
 Loopback configuration
 Ethernet
 DHCP
 DNS
 Routing
 Network daemon
v0.5.0 — Package Manager
 KratosOS package format
 Package database
 Package installation
 Package removal
 Dependency resolution
 Package verification
 Local repository
 Remote repository
Future
 Hardware testing
 User management
 Security model
 Package signing
 Graphical environment
 Installer
 Secure Boot
