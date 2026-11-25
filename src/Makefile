# ProperOS 2.5 Mobile · 原生组件
# 用法：make ARCH=arm64  /  make install DESTDIR=/root/build/rootfs-arm64

ARCH ?= x86_64
ifeq ($(ARCH),arm64)
CROSS ?= aarch64-linux-gnu-
else
CROSS ?=
endif
CC      := $(CROSS)gcc
STRIP   := $(CROSS)strip

CFLAGS  := -O2 -static -std=gnu11 -Wall -Wextra -Wno-unused-parameter \
           -D_GNU_SOURCE -DPROPEROS_ARCH=\"$(ARCH)\"
LDFLAGS := -static

BIN := build/$(ARCH)
COMMON := common/util.c common/sha256.c
INCS   := -Icommon

TARGETS := $(BIN)/proloader $(BIN)/stboot $(BIN)/properd $(BIN)/papp \
           $(BIN)/ppkg $(BIN)/properos-bootctl $(BIN)/properos-shell

all: $(TARGETS)

$(BIN):
	mkdir -p $@

$(BIN)/proloader: proloader/proloader.c $(COMMON) | $(BIN)
	$(CC) $(CFLAGS) $(INCS) -o $@ $^ $(LDFLAGS)

$(BIN)/stboot: stboot/stboot.c $(COMMON) | $(BIN)
	$(CC) $(CFLAGS) $(INCS) -o $@ $^ $(LDFLAGS)

$(BIN)/properd: properd/properd.c $(COMMON) | $(BIN)
	$(CC) $(CFLAGS) $(INCS) -o $@ $^ $(LDFLAGS)

$(BIN)/papp: papp/papp.c $(COMMON) | $(BIN)
	$(CC) $(CFLAGS) $(INCS) -o $@ $^ $(LDFLAGS)

$(BIN)/ppkg: ppkg/ppkg.c $(COMMON) | $(BIN)
	$(CC) $(CFLAGS) $(INCS) -o $@ $^ $(LDFLAGS)

$(BIN)/properos-bootctl: bootctl/bootctl.c $(COMMON) | $(BIN)
	$(CC) $(CFLAGS) $(INCS) -o $@ $^ $(LDFLAGS)

$(BIN)/properos-shell: shell/shell.c $(COMMON) shell/font_atlas.h | $(BIN)
	$(CC) $(CFLAGS) $(INCS) -o $@ shell/shell.c $(COMMON) $(LDFLAGS)

install: all
	install -Dm755 $(BIN)/proloader          $(DESTDIR)/usr/lib/properos/proloader
	install -Dm755 $(BIN)/stboot             $(DESTDIR)/usr/lib/properos/stboot
	install -Dm755 $(BIN)/properd            $(DESTDIR)/usr/sbin/properd
	install -Dm755 $(BIN)/papp               $(DESTDIR)/usr/bin/papp
	install -Dm755 $(BIN)/ppkg               $(DESTDIR)/usr/bin/ppkg
	install -Dm755 $(BIN)/properos-bootctl   $(DESTDIR)/usr/sbin/properos-bootctl
	install -Dm755 $(BIN)/properos-shell     $(DESTDIR)/usr/bin/properos-shell

clean:
	rm -rf build

.PHONY: all install clean
