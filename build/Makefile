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
SRC = $(wildcard src/*.c)
OUT = minicontainer

all: $(OUT)

$(OUT): $(SRC) src/common.h
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

clean:
	rm -f $(OUT) bomb breakout

.PHONY: all tools deploy clean
