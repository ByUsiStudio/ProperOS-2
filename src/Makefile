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
           -Wno-misleading-indentation -Wno-format-truncation \
           -DPROPEROS_ARCH=\"$(ARCH)\"
LDFLAGS := -static -lm

BIN := build/$(ARCH)
COMMON := common/util.c common/sha256.c
INCS   := -Icommon

# 图形与控件层：安装器与桌面共用
GFXSRC := gfx/gfx.c gfx/ui.c gfx/input.c
GFXINC := -Igfx

TARGETS := $(BIN)/proloader $(BIN)/stboot $(BIN)/properd $(BIN)/papp \
           $(BIN)/ppkg $(BIN)/properos-bootctl $(BIN)/properos-shell \
           $(BIN)/properos-setup $(BIN)/properos-jspk

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

$(BIN)/properos-shell: shell/shell.c $(GFXSRC) gfx/font_atlas.h jspk/zip.c jspk/inflate.c jspk/json.c $(COMMON) | $(BIN)
	$(CC) $(CFLAGS) $(GFXINC) -Ijspk $(INCS) -o $@ shell/shell.c $(GFXSRC) jspk/zip.c jspk/inflate.c jspk/json.c $(COMMON) $(LDFLAGS)

$(BIN)/properos-setup: ui/installer.c $(GFXSRC) gfx/font_atlas.h $(COMMON) | $(BIN)
	$(CC) $(CFLAGS) $(GFXINC) $(INCS) -o $@ ui/installer.c $(GFXSRC) $(COMMON) $(LDFLAGS)

JSPKSRC := jspk/jspk.c jspk/zip.c jspk/inflate.c jspk/json.c

$(BIN)/properos-jspk: $(JSPKSRC) $(GFXSRC) gfx/font_atlas.h $(COMMON) | $(BIN)
	$(CC) $(CFLAGS) $(GFXINC) -Ijspk $(INCS) -o $@ $(JSPKSRC) $(GFXSRC) $(COMMON) $(LDFLAGS)

# 打包系统应用（.jspk），安装时一起部署到 /usr/share/properos/apps
apps:
	python3 ../tools/jspk-pack.py --all

install: all apps
	install -d $(DESTDIR)/usr/share/properos/apps
	install -m644 ../build/apps/*.jspk $(DESTDIR)/usr/share/properos/apps/
	install -Dm755 $(BIN)/proloader          $(DESTDIR)/usr/lib/properos/proloader
	install -Dm755 $(BIN)/stboot             $(DESTDIR)/usr/lib/properos/stboot
	install -Dm755 $(BIN)/properd            $(DESTDIR)/usr/sbin/properd
	install -Dm755 $(BIN)/papp               $(DESTDIR)/usr/bin/papp
	install -Dm755 $(BIN)/ppkg               $(DESTDIR)/usr/bin/ppkg
	install -Dm755 $(BIN)/properos-bootctl   $(DESTDIR)/usr/sbin/properos-bootctl
	install -Dm755 $(BIN)/properos-shell     $(DESTDIR)/usr/bin/properos-shell
	install -Dm755 $(BIN)/properos-setup     $(DESTDIR)/usr/local/sbin/properos-setup
	install -Dm755 $(BIN)/properos-jspk      $(DESTDIR)/usr/bin/properos-jspk
	install -Dm755 ../tools/mok-enroll.sh    $(DESTDIR)/usr/sbin/properos-mok-enroll

clean:
	rm -rf build

.PHONY: all install clean apps
