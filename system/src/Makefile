# ProperOS 25.0.0 Mobile · 原生组件
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

# .jspk 运行时额外带 Wayland 客户端后端（能作为窗口跑在 Plasma Mobile 里）
JSPK_EXTRA := gfx/wl.c gfx/proto/xdg-shell-protocol.c
SYSROOT ?= /root/build/rootfs-arm64
CFLAGS_DYN := $(filter-out -static,$(CFLAGS)) -DPROPEROS_WAYLAND
ifeq ($(ARCH),arm64)
# 动态链接必须对着目标 rootfs 的 glibc 编译，否则会生成依赖构建机
# GLIBC_2.38+ 的二进制，在 Debian 12（2.36）上直接跑不起来。
CFLAGS_DYN += --sysroot=$(SYSROOT)
# 构建机上通常还装着 Ubuntu 的交叉 libc（/usr/aarch64-linux-gnu，2.39），
# 它的头与库都排在 sysroot 前面，会把二进制绑到 2.39 的符号（__isoc23_*），
# 拿到 Debian 12（2.36）上直接跑不起来。所以显式关掉默认搜索路径，
# 只认目标 rootfs 的头与库。
GCCINC := $(shell $(CC) -print-file-name=include)
CFLAGS_DYN += -nostdinc -isystem $(GCCINC) \
              -isystem $(SYSROOT)/usr/include/aarch64-linux-gnu \
              -isystem $(SYSROOT)/usr/include
WL_LIB := -L$(SYSROOT)/usr/lib/aarch64-linux-gnu -lwayland-client
else
WL_LIB := -lwayland-client
endif

TARGETS := $(BIN)/proloader $(BIN)/stboot $(BIN)/properd $(BIN)/papp \
           $(BIN)/ppkg $(BIN)/properos-bootctl $(BIN)/properos-shell \
           $(BIN)/properos-setup $(BIN)/properos-app

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

JSPKSRC := jspk/jspk.c jspk/zip.c jspk/inflate.c jspk/json.c jspk/native.c jspk/term.c

$(BIN)/properos-app: $(JSPKSRC) $(GFXSRC) $(JSPK_EXTRA) gfx/font_atlas.h $(COMMON) | $(BIN)
	$(CC) $(CFLAGS_DYN) $(GFXINC) -Ijspk $(INCS) -o $@ 	    $(JSPKSRC) $(GFXSRC) $(JSPK_EXTRA) $(COMMON) -lm $(WL_LIB)

# 打包系统应用（.ppkg）。从 25.0.0 起应用以 ppkg 交付：payload 里是
# /opt/properos/apps/<名字>/ 与对应的 .desktop，安装即「把 payload 铺到 /」。
apps:
	python3 ../tools/ppkg-app.py all ../apps ../build/apps
	python3 ../tools/ppkg-app.py bundle ../apps ../build/apps/properos-apps-25.0.0.ppkg

install: all apps
	install -Dm644 ../build/apps/properos-apps-25.0.0.ppkg $(DESTDIR)/var/cache/properos/ppkg/
	rm -rf ../build/apps-unpack && mkdir -p ../build/apps-unpack
	tar -xzf ../build/apps/properos-apps-25.0.0.ppkg -C ../build/apps-unpack payload
	cp -a ../build/apps-unpack/payload/. $(DESTDIR)/
	install -Dm755 $(BIN)/proloader          $(DESTDIR)/usr/lib/properos/proloader
	install -Dm755 $(BIN)/stboot             $(DESTDIR)/usr/lib/properos/stboot
	install -Dm755 $(BIN)/properd            $(DESTDIR)/usr/sbin/properd
	install -Dm755 $(BIN)/papp               $(DESTDIR)/usr/bin/papp
	install -Dm755 $(BIN)/ppkg               $(DESTDIR)/usr/bin/ppkg
	install -Dm755 $(BIN)/properos-bootctl   $(DESTDIR)/usr/sbin/properos-bootctl
	install -Dm755 $(BIN)/properos-shell     $(DESTDIR)/usr/bin/properos-shell
	install -Dm755 $(BIN)/properos-setup     $(DESTDIR)/usr/local/sbin/properos-setup
	install -Dm755 $(BIN)/properos-app      $(DESTDIR)/usr/bin/properos-app
	install -Dm755 ../tools/mok-enroll.sh    $(DESTDIR)/usr/sbin/properos-mok-enroll
	install -Dm755 ppkg/deb2ppkg.sh          $(DESTDIR)/usr/lib/properos/deb2ppkg
	install -Dm755 ppkg/dpkg-shim.sh         $(DESTDIR)/usr/lib/properos/dpkg-shim
	install -d $(DESTDIR)/var/cache/properos/ppkg $(DESTDIR)/var/log/properos

clean:
	rm -rf build

.PHONY: all install clean apps
