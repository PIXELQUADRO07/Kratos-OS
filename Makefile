# KratosOS — Root Makefile
#
# Convenience wrapper around the build scripts.
# Run `make help` to see all available targets.
#
# Quick start:
#   make all            # full build (all phases, skips already-built stages)
#   make all CLEAN=1    # wipe stamps and rebuild everything
#
# Individual phases:
#   make phase1         # Phase 1: toolchain bootstrap
#   make phase2         # Phase 2: userspace base
#   make phase3         # Phase 3: kernel + GRUB + init + disk image
#
# Individual targets:
#   make kernel         # build Linux kernel
#   make grub           # build GRUB EFI
#   make etc            # create /etc skeleton
#   make init           # build init, shutdown, devd, login, passwd
#   make pkg            # build kpm package manager
#   make disk           # create bootable disk image (requires sudo)
#
# Cleanup:
#   make clean          # remove work/ tools/ sysroot/ (keep downloads + stamps)
#   make distclean      # remove everything including downloads and stamps

SHELL         := /bin/bash
SCRIPTS       := build/scripts
MAKEFLAGS     += --no-print-directory

# Export KRATOS_JOBS so sub-scripts pick it up (default: nproc)
export KRATOS_JOBS ?= $(shell nproc)

.PHONY: help all test \
        phase1 phase2 phase3 \
        toolchain verify verify-phase2 download \
        linux-headers binutils gcc-pass1 glibc-bootstrap libgcc glibc gcc-pass2 \
        ncurses readline bash coreutils grep sed gawk findutils \
        diffutils tar gzip xz bzip2 file-cmd \
        kernel grub etc init pkg disk image \
        mbedtls ca-certs fetch \
        clean distclean stamps-clean

# ─────────────────────────────────────────────
# Default: show help
# ─────────────────────────────────────────────
help:
	@echo ""
	@echo "  KratosOS Build System"
	@echo "  ─────────────────────────────────────────"
	@echo "  make all            Full build — all phases (incremental)"
	@echo "  make all CLEAN=1    Full rebuild — wipe stamps first"
	@echo ""
	@echo "  Phase targets:"
	@echo "  make phase1         Phase 1: toolchain bootstrap"
	@echo "  make phase2         Phase 2: userspace base"
	@echo "  make phase3         Phase 3: kernel + GRUB + init + disk"
	@echo ""
	@echo "  Phase 1 individual stages:"
	@echo "  make linux-headers  make binutils     make gcc-pass1"
	@echo "  make glibc-bootstrap make libgcc      make glibc"
	@echo "  make gcc-pass2"
	@echo ""
	@echo "  Phase 2 individual packages:"
	@echo "  make ncurses   make readline   make bash   make coreutils"
	@echo "  make grep      make sed        make gawk   make findutils"
	@echo "  make diffutils make tar        make gzip   make xz"
	@echo "  make bzip2     make file-cmd"
	@echo ""
	@echo "  Phase 3 individual targets:"
	@echo "  make kernel    make grub    make etc   make init"
	@echo "  make pkg       make disk"
	@echo ""
	@echo "  Utilities:"
	@echo "  make test           Run automated test suite (security, pkg, json, crypt)"
	@echo "  make verify         Run Phase 1 toolchain verification"
	@echo "  make verify-phase2  Run Phase 2 userspace verification"
	@echo "  make download       Download all source tarballs"
	@echo "  make stamps-clean   Clear all incremental build stamps"
	@echo ""
	@echo "  Cleanup:"
	@echo "  make clean          Remove build artifacts (keep downloads + stamps)"
	@echo "  make distclean      Remove everything including downloads and stamps"
	@echo ""
	@echo "  Options:"
	@echo "  KRATOS_JOBS=N       Parallel make jobs (default: nproc=$(shell nproc))"
	@echo "  CLEAN=1             Wipe all stamps before building (with make all)"
	@echo ""

# ─────────────────────────────────────────────
# Test suite
# ─────────────────────────────────────────────
test:
	@echo "========================================="
	@echo "    KratosOS Automated Test Suite"
	@echo "========================================="
	@mkdir -p build/tests/bin
	@echo "[+] Compiling and running test-crypt..."
	@gcc -Wall -Wextra -std=gnu11 -Iinit init/kratos-crypt.c build/tests/test-crypt.c -o build/tests/bin/test-crypt
	@./build/tests/bin/test-crypt
	@echo ""
	@echo "[+] Compiling and running test-json..."
	@gcc -Wall -Wextra -std=gnu11 -Ipkg pkg/kratos-json.c build/tests/test-json.c -o build/tests/bin/test-json
	@./build/tests/bin/test-json
	@echo ""
	@echo "[+] Compiling and running test-deps..."
	@gcc -Wall -Wextra -std=gnu11 -Ipkg pkg/kratos-deps.c build/tests/test-deps.c -o build/tests/bin/test-deps
	@./build/tests/bin/test-deps
	@echo ""
	@echo "[+] Compiling and running test-repo..."
	@gcc -Wall -Wextra -std=gnu11 -Ipkg pkg/kratos-repo.c pkg/kratos-json.c pkg/kratos-sha256.c pkg/kratos-deps.c pkg/kratos-tar.c build/tests/test-repo.c -o build/tests/bin/test-repo
	@./build/tests/bin/test-repo
	@echo ""
	@echo "[+] Compiling and running test-pkg-security..."
	@gcc -Wall -Wextra -std=gnu11 -Ipkg pkg/kratos-tar.c pkg/kratos-sha256.c build/tests/test-pkg-security.c -o build/tests/bin/test-pkg-security
	@./build/tests/bin/test-pkg-security
	@echo ""
	@echo "[✓] All test suites completed successfully."
	@echo ""

# ─────────────────────────────────────────────
# Full build — all phases in order (incremental)
# ─────────────────────────────────────────────
all:
ifeq ($(CLEAN),1)
	@bash build.sh --clean
else
	@bash build.sh
endif

# ─────────────────────────────────────────────
# Phase 1 — Full toolchain bootstrap
# ─────────────────────────────────────────────
phase1 toolchain:
	@bash $(SCRIPTS)/build-all-phase1.sh

verify:
	@bash $(SCRIPTS)/verify-toolchain.sh

download:
	@bash $(SCRIPTS)/download.sh

# Phase 1 individual stages
linux-headers:
	@bash $(SCRIPTS)/install-linux-headers.sh

binutils:
	@bash $(SCRIPTS)/build-binutils.sh

gcc-pass1:
	@bash $(SCRIPTS)/build-gcc-pass1.sh

glibc-bootstrap:
	@bash $(SCRIPTS)/build-glibc-bootstrap.sh

libgcc:
	@bash $(SCRIPTS)/build-libgcc.sh

glibc:
	@bash $(SCRIPTS)/build-glibc.sh

gcc-pass2:
	@bash $(SCRIPTS)/build-gcc-pass2.sh

# ─────────────────────────────────────────────
# Phase 2 — Userspace base
# ─────────────────────────────────────────────
phase2:
	@bash $(SCRIPTS)/build-all-phase2.sh

verify-phase2:
	@bash $(SCRIPTS)/verify-phase2.sh

ncurses:
	@bash $(SCRIPTS)/build-ncurses.sh

readline:
	@bash $(SCRIPTS)/build-readline.sh

bash:
	@bash $(SCRIPTS)/build-bash.sh

coreutils:
	@bash $(SCRIPTS)/build-coreutils.sh

grep:
	@bash $(SCRIPTS)/build-grep.sh

sed:
	@bash $(SCRIPTS)/build-sed.sh

gawk:
	@bash $(SCRIPTS)/build-gawk.sh

findutils:
	@bash $(SCRIPTS)/build-findutils.sh

diffutils:
	@bash $(SCRIPTS)/build-diffutils.sh

tar:
	@bash $(SCRIPTS)/build-tar.sh

gzip:
	@bash $(SCRIPTS)/build-gzip.sh

xz:
	@bash $(SCRIPTS)/build-xz.sh

bzip2:
	@bash $(SCRIPTS)/build-bzip2.sh

file-cmd:
	@bash $(SCRIPTS)/build-file.sh

# ─────────────────────────────────────────────
# Phase 3 — Kernel, bootloader, init, disk image
# ─────────────────────────────────────────────
phase3: kernel grub mbedtls ca-certs etc init pkg fetch disk

kernel:
	@bash $(SCRIPTS)/build-kernel.sh

grub:
	@bash $(SCRIPTS)/build-grub.sh

etc:
	@bash $(SCRIPTS)/create-etc-skeleton.sh

init:
	@bash $(SCRIPTS)/build-init.sh

pkg:
	@bash $(SCRIPTS)/build-pkg.sh

mbedtls:
	@bash $(SCRIPTS)/build-mbedtls.sh

ca-certs:
	@bash $(SCRIPTS)/build-ca-certificates.sh

fetch:
	@bash $(SCRIPTS)/build-fetch.sh

# disk requires root — invoke via sudo automatically
disk image:
	@if [ "$$(id -u)" -ne 0 ]; then \
	    echo "[+] disk target requires root — invoking sudo..."; \
	    sudo bash $(SCRIPTS)/build-disk.sh; \
	else \
	    bash $(SCRIPTS)/build-disk.sh; \
	fi

# ─────────────────────────────────────────────
# Incremental stamps
# ─────────────────────────────────────────────
stamps-clean:
	@echo "[+] Removing build/.stamps/ ..."
	@rm -rf build/.stamps
	@echo "[✓] Stamps cleared."

# ─────────────────────────────────────────────
# Cleanup
# ─────────────────────────────────────────────
clean:
	@echo "[+] Removing build/work/, build/tools/, build/sysroot/, build/sources/ ..."
	@rm -rf build/work build/tools build/sysroot build/sources
	@echo "[✓] Clean done. Downloads and stamps preserved."

distclean: clean stamps-clean
	@echo "[+] Removing build/downloads/ ..."
	@rm -rf build/downloads
	@echo "[✓] Distclean done."
