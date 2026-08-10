# KratosOS

<img width="500" height="500" align=center alt="KRATOS_OS-removebg-preview" src="https://github.com/user-attachments/assets/c82503fe-3256-4bf6-b5ae-f0a4d458f300" />

<p align="center">
  <b>A GNU/Linux distribution built from the ground up.</b>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/architecture-x86__64-blue">
  <img src="https://img.shields.io/badge/status-early%20development-orange">
  <img src="https://img.shields.io/badge/boot-UEFI-green">
  <img src="https://img.shields.io/badge/kernel-Linux%207.1.5-lightgrey">
  <img src="https://img.shields.io/badge/license-GPL--3.0-blue">
</p>

---

## About

**KratosOS** is an independent GNU/Linux distribution built from the ground up.

The project does not use Debian, Arch Linux, Ubuntu or another distribution as its
base system. Instead, KratosOS builds its own toolchain, system root, kernel,
userspace and boot environment.

The goal is to create a complete, self-contained GNU/Linux system with its own
system infrastructure and package ecosystem.

> KratosOS is currently in early development and is not intended for daily use.

---

## Current Status

KratosOS is currently capable of booting a complete minimal userspace.

### Working

- [x] Custom cross-toolchain
- [x] x86_64 target
- [x] Custom sysroot
- [x] Linux kernel build
- [x] Linux kernel 7.1.5
- [x] glibc
- [x] Bash
- [x] GRUB 2.14
- [x] UEFI boot support
- [x] GPT disk image
- [x] Custom PID 1
- [x] Virtual filesystem mounting
- [x] `/etc/fstab`
- [x] `LABEL=` filesystem resolution
- [x] `UUID=` filesystem resolution
- [x] Hostname configuration
- [x] System initialization scripts
- [x] Service startup framework
- [x] Signal handling
- [x] Zombie process reaping
- [x] TTY supervision
- [x] Login
- [x] Bash shell
- [x] QEMU boot
- [x] Disk image generation

### In Development

- [ ] Native device manager (`kratos-devd`)
- [ ] Dynamic `/dev` management
- [ ] Coldplug / hotplug support
- [ ] Device permissions
- [ ] Disk symlinks (`by-uuid`, `by-label`)
- [ ] Native package manager
- [ ] Package repository infrastructure
- [ ] Networking stack/userspace
- [ ] User management
- [ ] Installer
- [ ] ISO generation
- [ ] Physical hardware testing

---

# Architecture

The current boot chain is approximately:

```text
UEFI
 │
 ▼
GRUB
 │
 ▼
Linux Kernel
 │
 ▼
/sbin/init
 │
 ├── Mount VFS
 │    ├── /proc
 │    ├── /sys
 │    ├── /dev
 │    ├── /dev/pts
 │    ├── /dev/shm
 │    ├── /run
 │    └── /tmp
 │
 ├── Parse /etc/fstab
 │
 ├── Configure hostname
 │
 ├── Execute /etc/rc.sysinit
 │
 ├── Start services
 │
 ├── Supervise TTYs
 │
 └── Start login
        │
        ▼
      Bash
Init System

KratosOS uses a custom native init system instead of relying on an external
init framework.

The PID 1 implementation is divided into separate components:

init/
├── init.h
├── init.c
├── mount.c
├── mount.h
├── services.c
├── services.h
├── signals.c
├── signals.h
├── tty.c
└── tty.h
Components
Component	Responsibility
init.c	PID 1 and main supervision loop
mount.c	VFS and filesystem mounting
services.c	hostname, startup scripts and services
signals.c	signal handling and zombie reaping
tty.c	TTY supervision and login
*.h	Shared interfaces

The init system currently provides:

automatic virtual filesystem mounting
/etc/fstab parsing
LABEL= and UUID= resolution
hostname configuration
early boot scripts
service startup
zombie reaping
shutdown/reboot handling
TTY supervision
login management
Device Management

KratosOS is designed to use a lightweight native device manager.

The planned daemon is:

kratos-devd

Instead of depending on a complete external userspace device-management
framework, KratosOS intends to process Linux kernel device events directly
through:

NETLINK_KOBJECT_UEVENT

Planned functionality includes:

coldplug device discovery
hotplug support
/dev node management
device permissions
disk identification
by-uuid symlinks
by-label symlinks
Build System

KratosOS uses a dedicated build environment and cross-toolchain.

The target currently is:

x86_64-kratos-linux-gnu

The build system produces a KratosOS sysroot containing the kernel, bootloader
and userspace.

Important directories:

build/
├── downloads/
├── sources/
├── work/
├── tools/
├── sysroot/
└── images/

The generated sysroot is the basis for the final filesystem image.

Kernel

KratosOS currently builds:

Linux 7.1.5

The kernel is configured for x86_64 and includes the functionality required by
the current minimal userspace.

Generated kernel files include:

/boot/vmlinuz
/boot/System.map
/boot/config-7.1.5
/lib/modules/7.1.5/
Bootloader

KratosOS currently uses:

GRUB 2.14

with:

x86_64 EFI
GPT
UEFI

The disk image contains:

┌──────────────────────────────┐
│ GPT                          │
├──────────────────────────────┤
│ EFI System Partition         │
│ ~256 MB                      │
├──────────────────────────────┤
│ KratosOS root filesystem     │
│ ~1.7 GB                      │
└──────────────────────────────┘
Running KratosOS
QEMU

The easiest way to test the current system is:

./run-qemu.sh

The current image boots successfully in QEMU and reaches the KratosOS login
environment.

Building the disk image

The build system can generate a GPT disk image:

sudo ./build/scripts/build-disk.sh

The resulting image is:

build/images/kratosos.img
Development

Clone the repository:

git clone <repository-url>
cd KratosOS

Build the required components using the scripts in:

build/scripts/

The project currently requires a Linux development environment with the
necessary host build tools.

Project Roadmap
Phase 0 — Toolchain
 Build system
 Binutils
 GCC cross compiler
 Target sysroot
Phase 1 — Base System
 glibc
 Linux kernel
 GRUB
 Bash
 Root filesystem
 GPT disk image
 UEFI boot
Phase 2 — Init
 PID 1
 VFS mounting
 /etc/fstab
 hostname
 startup scripts
 service framework
Phase 3 — Process & TTY Management
 signal handling
 zombie reaping
 TTY supervision
 login
 shutdown/reboot/halt
Phase 4 — Device Management
 kratos-devd
 netlink uevents
 coldplug
 hotplug
 dynamic /dev
 device permissions
 disk symlinks
Phase 5 — Package Management
 package format
 package database
 dependency resolution
 package installation
 package removal
 upgrades
 repository metadata
 cryptographic package signing
 native package repositories
Phase 6 — Networking
 network configuration
 DHCP
 DNS
 interface management
 network service
Phase 7 — Userspace
 user management
 groups
 permissions
 login improvements
 core utilities
 filesystem utilities
Phase 8 — Distribution
 ISO generator
 installer
 installation environment
 boot configuration
 release infrastructure
Phase 9 — Hardware
 physical hardware boot
 storage testing
 networking hardware
 graphics
 audio
 power management
Releases

Current development milestones:

Version	Status
v0.1.0	First bootable KratosOS system
v0.2.0	Modular init system, VFS, services and TTY management
v0.3.0	Planned native device management
v0.4.0	Planned package manager
Philosophy

KratosOS is built around a simple principle:

Build the system instead of inheriting the system.

Rather than starting from an existing distribution and replacing individual
components, KratosOS builds its own foundation progressively.

The project aims to keep the base system understandable, modular and
independent while still remaining compatible with the GNU/Linux ecosystem.

Contributing

KratosOS is currently an experimental development project.

Development is focused on the core system, build infrastructure and boot
process. Contributions, ideas and technical discussion are welcome.

License

KratosOS is released under the GNU General Public License v3.0.

See LICENSE for details.
