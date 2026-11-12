# Makefile — build minicontainer tu src/*.c (1 binary static)
# Toolchain mac dinh: musl noi bo cua Buildroot (da build o phase 1).
# Doi toolchain: make CC=aarch64-linux-gnu-gcc STRIP=x
TOOLCHAIN ?= $(CURDIR)/buildroot/output/host/bin/aarch64-buildroot-linux-musl-
# NOTE: make tu dinh nghia CC=cc nen ?= khong tac dung — phai kiem origin
ifeq ($(origin CC),default)
CC := $(TOOLCHAIN)gcc
endif
STRIP ?= $(TOOLCHAIN)strip
CFLAGS ?= -static -O2 -Wall
SRC = $(filter-out src/seccomp_strict_%.c,$(wildcard src/*.c))
OUT = minicontainer

# Detect target architecture from compiler (for seccomp filter generation)
# Cross-compile: aarch64-linux-musl-gcc -> aarch64
# Native: gcc -> x86_64 (or whatever host is)
TARGET_ARCH := $(shell $(CC) -dumpmachine 2>/dev/null | cut -d- -f1)
ifeq ($(TARGET_ARCH),)
    TARGET_ARCH := $(shell uname -m)
endif

# Map compiler architecture to our seccomp architecture names
SECCOMP_ARCH := $(TARGET_ARCH)
ifeq ($(TARGET_ARCH),aarch64)
    SECCOMP_ARCH := aarch64
endif
ifeq ($(TARGET_ARCH),x86_64)
    SECCOMP_ARCH := x86_64
endif
ifeq ($(TARGET_ARCH),arm)
    SECCOMP_ARCH := arm
endif
ifeq ($(TARGET_ARCH),riscv64)
    SECCOMP_ARCH := riscv64
endif
ifeq ($(TARGET_ARCH),i386)
    SECCOMP_ARCH := i386
endif
ifeq ($(TARGET_ARCH),i686)
    SECCOMP_ARCH := i386
endif

# Generated strict seccomp filter file
SECCOMP_STRICT_SRC := src/seccomp_strict_$(SECCOMP_ARCH).c

all: $(OUT)

# Generate strict seccomp filter before building
$(SECCOMP_STRICT_SRC): generate_seccomp.py
	python3 generate_seccomp.py $(SECCOMP_ARCH) $(SECCOMP_STRICT_SRC)

$(OUT): $(SRC) src/common.h $(SECCOMP_STRICT_SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC)
	$(STRIP) $@
	ls -la $@

# Tool test (bomb/breakout) cho /opt/rootfs
tools: bomb.c breakout.c
	$(CC) $(CFLAGS) -o bomb bomb.c
	$(CC) $(CFLAGS) -o breakout breakout.c
	$(STRIP) bomb breakout

# Deploy len box: make deploy BOX=<IP_BOX> (can SSHPASS env, recommend SSH key)
deploy: $(OUT)
	@if [ -n "$(SSHPASS_FILE)" ]; then sshpass -p "$$(cat $(SSHPASS_FILE))" scp -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $(OUT) root@$(BOX):/usr/local/bin/; \
	elif [ -n "$(SSHPASS)" ]; then echo "Warning: passing SSHPASS on command line is visible in ps; use SSHPASS_FILE instead."; \
	sshpass -p "$(SSHPASS)" scp -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $(OUT) root@$(BOX):/usr/local/bin/; \
	else scp -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $(OUT) root@$(BOX):/usr/local/bin/; fi

# Regenerate seccomp filter for current target
seccomp-gen: $(SECCOMP_STRICT_SRC)

clean:
	rm -f $(OUT) bomb breakout $(SECCOMP_STRICT_SRC)

.PHONY: all tools deploy clean seccomp-gen