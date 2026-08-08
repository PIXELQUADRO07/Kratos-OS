# KratosOS — Phase 1 Build Guide

> This document describes how to reproduce the Phase 1 toolchain bootstrap from a clean host system.

## Overview

Phase 1 builds a **cross-compilation toolchain** that runs on the host (x86-64 Linux) and produces binaries for the KratosOS target triplet `x86_64-kratos-linux-gnu`.

```
Host compiler
    └─▶ Binutils (cross as/ld/ar)
    └─▶ GCC pass 1 (C only, no libc)
            └─▶ Linux kernel headers (sysroot)
            └─▶ Glibc bootstrap (crt*.o + stub libc.so)
                    └─▶ libgcc (shared runtime)
                            └─▶ Glibc full (complete libc in sysroot)
                                    └─▶ GCC pass 2 (C + C++ + libstdc++)
                                            └─▶ verify-toolchain.sh ✓
```

## Host Requirements

| Tool | Minimum version | Check |
|---|---|---|
| GCC or Clang | 11+ | `gcc --version` |
| GNU Make | 4.0+ | `make --version` |
| Bash | 5.0+ | `bash --version` |
| curl | any | `curl --version` |
| tar, xz, gzip | any | — |
| sha256sum | any | `sha256sum --version` |
| Python 3 | 3.8+ (GCC prereq) | `python3 --version` |

**Optional** (for `--run` binary execution):
- `qemu-x86_64-static` — install via `apt install qemu-user-static` or similar

## Quick Start

```bash
# 1. Clone the repository
git clone https://github.com/PIXELQUADRO07/KratosOS.git
cd KratosOS

# 2. Full Phase 1 (takes ~1–3 hours depending on hardware)
make phase1

# 3. Or, step by step:
make download       # download all sources
make linux-headers  # install kernel headers into sysroot
make binutils       # build cross assembler/linker
make gcc-pass1      # build minimal C cross-compiler
make glibc          # build C library (bootstrap + full)
make gcc-pass2      # build full C + C++ cross-compiler
make verify         # smoke-test the toolchain
```

### Parallel jobs

```bash
# Use 8 parallel jobs
KRATOS_JOBS=8 make phase1
```

## Directory Layout After Phase 1

```
build/
├── config/
│   ├── build.conf        # environment variables (TARGET, paths)
│   └── versions.conf     # source package versions
├── downloads/            # (gitignored) downloaded tarballs
│   ├── linux-7.1.5.tar.xz
│   ├── binutils-2.45.tar.xz
│   ├── gcc-15.2.0.tar.xz
│   └── glibc-2.42.tar.xz
├── sources/              # (gitignored) extracted source trees
├── work/                 # (gitignored) out-of-tree build directories
├── tools/                # (gitignored) cross-toolchain installation
│   └── bin/
│       ├── x86_64-kratos-linux-gnu-gcc
│       ├── x86_64-kratos-linux-gnu-g++
│       ├── x86_64-kratos-linux-gnu-as
│       ├── x86_64-kratos-linux-gnu-ld
│       └── ...
└── sysroot/              # (gitignored) target root filesystem (headers + libs)
    └── usr/
        ├── include/      # Linux + glibc headers
        └── lib/          # libc.so, libc.a, crt*.o, libgcc_s.so
```

## Script Reference

| Script | Description |
|---|---|
| `build/scripts/download.sh` | Download all Phase 1 sources, verify SHA256 |
| `build/scripts/bootstrap.sh` | Create build directory structure |
| `build/scripts/install-linux-headers.sh` | Export Linux kernel userspace API headers |
| `build/scripts/build-binutils.sh` | Build cross binutils (as, ld, ar, nm, ...) |
| `build/scripts/build-gcc-pass1.sh` | Build GCC pass 1 (C only, no libc) |
| `build/scripts/build-glibc-bootstrap.sh` | Build glibc startup objects + stub libc |
| `build/scripts/build-libgcc.sh` | Build libgcc_s (shared GCC runtime) |
| `build/scripts/build-glibc.sh` | Build and install complete glibc |
| `build/scripts/build-gcc-pass2.sh` | Build GCC pass 2 (C + C++ + libstdc++) |
| `build/scripts/verify-toolchain.sh` | Smoke-test the complete toolchain |
| `build/scripts/build-all-phase1.sh` | Orchestrate all of the above in order |

## Why Two GCC Passes?

This is the classic **chicken-and-egg** problem of building a C library and compiler from scratch:

1. **GCC needs glibc** headers to build (for `stdio.h`, `stdlib.h`, etc.)
2. **glibc needs GCC** to compile (it uses GCC-specific extensions)

The solution is a three-step bootstrap:
- **Pass 1**: Build a minimal GCC with no libc support (`--disable-shared`, C only). This compiler can only produce simple code.
- **Glibc bootstrap**: Use pass 1 to compile just the C startup objects (`crt1.o`, `crti.o`, `crtn.o`) and a stub `libc.so`. This gives GCC something to link against.
- **libgcc**: With the stub libc available, build `libgcc_s.so` (the GCC shared runtime).
- **Glibc full**: With libgcc available, build the complete glibc.
- **Pass 2**: Build a full GCC with C, C++, POSIX threads, and libstdc++, using the real glibc.

## Troubleshooting

### `configure: error: C compiler cannot create executables`

The GCC pass 1 compiler is not in `PATH`. Ensure `build/config/build.conf` is sourced, or run via the Makefile.

### `ld: cannot find -lc`

The glibc bootstrap did not complete. Run `make glibc-bootstrap` then `make libgcc` before `make gcc-pass2`.

### Checksum mismatch in `download.sh`

The SHA256 entries in `download.sh` are placeholders until you run it once and fill them in. Run:

```bash
sha256sum build/downloads/*.tar.*
```

Then update the `SHA256` array in `build/scripts/download.sh`.

### Build fails on `make -j$(nproc)` for glibc

Some glibc versions have race conditions in their Makefile. Try:

```bash
KRATOS_JOBS=1 make glibc
```
