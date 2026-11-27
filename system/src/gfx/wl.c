/* ProperOS 25.0.0 Mobile · Wayland 输出后端实现
 *
 * 只依赖 libwayland-client + xdg-shell：没有 GTK/Qt，也不引入 EGL。
 * 帧缓冲走 wl_shm（软件光栅化），与 framebuffer 路径共用 gfx 的绘制结果。
 *
 * 开发者：ByUsi Studio · 北啊呢
 */
#define _GNU_SOURCE
#include "wl.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

#include "proto/xdg-shell-client.h"

#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif

struct wlctx {
    struct wl_display    *dpy;
    struct wl_registry   *reg;
    struct wl_compositor *comp;
    struct wl_shm        *shm;
    struct wl_seat       *seat;
    struct wl_pointer    *ptr;
    struct wl_touch      *touch;
    struct xdg_wm_base   *wm;
    struct wl_surface    *surf;
    struct xdg_surface   *xs;
    struct xdg_toplevel  *tl;
    struct wl_buffer     *buf;

    void  *pix;
    size_t pixlen;
    int    w, h;
    int    pw, ph;
    int    configured;
    int    closed;

    int    ptr_down;
    int    px, py;      /* pointer 位置（像素） */
};

/* 手势状态机复用的输入上下文 */
static struct wlctx *G;
static input_t       wl_in;
static idev_t        wl_dev;

/* listener 产生的事件先入队，poll 时再交给调用方 */
#define WQ_MAX 128
static in_event_t WQ[WQ_MAX];
static int        wq_n;

static void q_reset(void) { wq_n = 0; }

static void q_flush_to(in_event_t *out, int max, int *n)
{
    int k = wq_n < max ? wq_n : max;
    if (k > 0) {
        memcpy(out, WQ, (size_t)k * sizeof(in_event_t));
        *n = k;
    }
    if (wq_n > k) {
        memmove(WQ, WQ + k, (size_t)(wq_n - k) * sizeof(in_event_t));
        wq_n -= k;
    } else {
        wq_n = 0;
    }
}

int wl_active(void)
{
    return G != NULL;
}

/* ---------------- shm 缓冲 ---------------- */

static int shm_fd_create(size_t size)
{
    int fd = memfd_create("properos-wl", MFD_CLOEXEC);
    if (fd < 0) return -1;
    if (ftruncate(fd, (off_t)size) < 0) { close(fd); return -1; }
    return fd;
}

static int make_buffer(struct wlctx *c, int w, int h)
{
    int stride = w * 4;
    size_t size = (size_t)stride * (size_t)h;
    if (size == 0) return -1;

    int fd = shm_fd_create(size);
    if (fd < 0) return -1;

    void *p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) { close(fd); return -1; }

    struct wl_shm_pool *pool = wl_shm_create_pool(c->shm, fd, (int32_t)size);
    if (!pool) { munmap(p, size); close(fd); return -1; }

    if (c->buf) wl_buffer_destroy(c->buf);
    c->buf = wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);

    if (!c->buf) { munmap(p, size); return -1; }

    if (c->pix) munmap(c->pix, c->pixlen);
    c->pix = p;
    c->pixlen = size;
    c->w = w;
    c->h = h;
    return 0;
}

/* ---------------- xdg-shell ---------------- */

static void wm_ping(void *d, struct xdg_wm_base *wm, uint32_t serial)
{
    (void)d;
    xdg_wm_base_pong(wm, serial);
}

static const struct xdg_wm_base_listener wm_listener = { .ping = wm_ping };

static void tl_configure(void *d, struct xdg_toplevel *tl, int32_t w, int32_t h,
                         struct wl_array *states)
{
    (void)tl; (void)states;
    struct wlctx *c = d;
    if (w > 0 && h > 0) { c->pw = w; c->ph = h; }
}

static void tl_close(void *d, struct xdg_toplevel *tl)
{
    (void)tl;
    ((struct wlctx *)d)->closed = 1;
}

static void tl_bounds(void *d, struct xdg_toplevel *tl, int32_t w, int32_t h)
{
    (void)d; (void)tl; (void)w; (void)h;
}

static const struct xdg_toplevel_listener tl_listener = {
    .configure = tl_configure,
    .close = tl_close,
    .configure_bounds = tl_bounds,
};

static void xs_configure(void *d, struct xdg_surface *xs, uint32_t serial)
{
    struct wlctx *c = d;
    xdg_surface_ack_configure(xs, serial);
    c->configured = 1;
}

static const struct xdg_surface_listener xs_listener = { .configure = xs_configure };

/* ---------------- 输入 ---------------- */

static void ptr_enter(void *d, struct wl_pointer *p, uint32_t serial,
                      struct wl_surface *s, wl_fixed_t x, wl_fixed_t y)
{
    (void)d; (void)p; (void)serial; (void)s;
    G->px = (int)wl_fixed_to_double(x);
    G->py = (int)wl_fixed_to_double(y);
}

static void ptr_leave(void *d, struct wl_pointer *p, uint32_t serial,
                      struct wl_surface *s)
{
    (void)d; (void)p; (void)serial; (void)s;
    G->ptr_down = 0;
}

static void ptr_motion(void *d, struct wl_pointer *p, uint32_t t, wl_fixed_t x, wl_fixed_t y)
{
    (void)d; (void)p; (void)t;
    int nx = (int)wl_fixed_to_double(x);
    int ny = (int)wl_fixed_to_double(y);
    G->px = nx;
    G->py = ny;
    if (G->ptr_down)
        in_move(&wl_in, &wl_dev, nx, ny, WQ, &wq_n, WQ_MAX, in_now_ms());
}

static void ptr_button(void *d, struct wl_pointer *p, uint32_t serial,
                       uint32_t t, uint32_t button, uint32_t state)
{
    (void)d; (void)p; (void)serial; (void)t;
    if (button != BTN_LEFT) return;
    if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
        G->ptr_down = 1;
        in_press(&wl_in, &wl_dev, G->px, G->py, in_now_ms());
    } else {
        G->ptr_down = 0;
        in_release(&wl_in, &wl_dev, G->px, G->py, WQ, &wq_n, WQ_MAX, in_now_ms());
    }
}

static void ptr_axis(void *d, struct wl_pointer *p, uint32_t t, uint32_t axis, wl_fixed_t v)
{
    (void)d; (void)p; (void)t; (void)axis; (void)v;
}

static void ptr_frame(void *d, struct wl_pointer *p) { (void)d; (void)p; }
static void ptr_axis_source(void *d, struct wl_pointer *p, uint32_t s) { (void)d; (void)p; (void)s; }
static void ptr_axis_stop(void *d, struct wl_pointer *p, uint32_t t, uint32_t a) { (void)d; (void)p; (void)t; (void)a; }
static void ptr_axis_discrete(void *d, struct wl_pointer *p, uint32_t a, int32_t v) { (void)d; (void)p; (void)a; (void)v; }

static const struct wl_pointer_listener ptr_listener = {
    .enter = ptr_enter,
    .leave = ptr_leave,
    .motion = ptr_motion,
    .button = ptr_button,
    .axis = ptr_axis,
    .frame = ptr_frame,
    .axis_source = ptr_axis_source,
    .axis_stop = ptr_axis_stop,
    .axis_discrete = ptr_axis_discrete,
};

static void tch_down(void *d, struct wl_touch *t, uint32_t serial, uint32_t tm,
                     struct wl_surface *s, int32_t id, wl_fixed_t x, wl_fixed_t y)
{
    (void)d; (void)t; (void)serial; (void)tm; (void)s; (void)id;
    in_press(&wl_in, &wl_dev, (int)wl_fixed_to_double(x),
             (int)wl_fixed_to_double(y), in_now_ms());
}

static void tch_up(void *d, struct wl_touch *t, uint32_t serial, uint32_t tm, int32_t id)
{
    (void)d; (void)t; (void)serial; (void)tm; (void)id;
    in_release(&wl_in, &wl_dev, wl_dev.x, wl_dev.y, WQ, &wq_n, WQ_MAX, in_now_ms());
}

static void tch_motion(void *d, struct wl_touch *t, uint32_t tm, int32_t id,
                       wl_fixed_t x, wl_fixed_t y)
{
    (void)d; (void)t; (void)tm; (void)id;
    in_move(&wl_in, &wl_dev, (int)wl_fixed_to_double(x),
            (int)wl_fixed_to_double(y), WQ, &wq_n, WQ_MAX, in_now_ms());
}

static void tch_frame(void *d, struct wl_touch *t) { (void)d; (void)t; }
static void tch_cancel(void *d, struct wl_touch *t)
{
    (void)d; (void)t;
    in_release(&wl_in, &wl_dev, wl_dev.x, wl_dev.y, WQ, &wq_n, WQ_MAX, in_now_ms());
}

static const struct wl_touch_listener touch_listener = {
    .down = tch_down,
    .up = tch_up,
    .motion = tch_motion,
    .frame = tch_frame,
    .cancel = tch_cancel,
};

static void seat_caps(void *d, struct wl_seat *seat, uint32_t caps)
{
    struct wlctx *c = d;
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !c->ptr) {
        c->ptr = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(c->ptr, &ptr_listener, c);
    } else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && c->ptr) {
        wl_pointer_destroy(c->ptr);
        c->ptr = NULL;
    }
    if ((caps & WL_SEAT_CAPABILITY_TOUCH) && !c->touch) {
        c->touch = wl_seat_get_touch(seat);
        wl_touch_add_listener(c->touch, &touch_listener, c);
    } else if (!(caps & WL_SEAT_CAPABILITY_TOUCH) && c->touch) {
        wl_touch_destroy(c->touch);
        c->touch = NULL;
    }
}

static void seat_name(void *d, struct wl_seat *s, const char *name) { (void)d; (void)s; (void)name; }

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_caps,
    .name = seat_name,
};

/* ---------------- registry ---------------- */

static void reg_global(void *d, struct wl_registry *reg, uint32_t name,
                       const char *iface, uint32_t ver)
{
    struct wlctx *c = d;
    if (!strcmp(iface, wl_compositor_interface.name)) {
        c->comp = wl_registry_bind(reg, name, &wl_compositor_interface, ver < 4 ? ver : 4);
    } else if (!strcmp(iface, wl_shm_interface.name)) {
        c->shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
    } else if (!strcmp(iface, xdg_wm_base_interface.name)) {
        c->wm = wl_registry_bind(reg, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(c->wm, &wm_listener, c);
    } else if (!strcmp(iface, wl_seat_interface.name)) {
        c->seat = wl_registry_bind(reg, name, &wl_seat_interface, ver < 5 ? ver : 5);
        wl_seat_add_listener(c->seat, &seat_listener, c);
    }
}

static void reg_remove(void *d, struct wl_registry *r, uint32_t name)
{
    (void)d; (void)r; (void)name;
}

static const struct wl_registry_listener reg_listener = {
    .global = reg_global,
    .global_remove = reg_remove,
};

/* ---------------- 对外接口 ---------------- */

static int wait_configure(struct wlctx *c, int timeout_ms)
{
    int waited = 0;
    while (!c->configured && waited < timeout_ms) {
        wl_display_flush(c->dpy);
        struct pollfd p = { wl_display_get_fd(c->dpy), POLLIN, 0 };
        int r = poll(&p, 1, 10);
        waited += 10;
        if (r > 0 && (p.revents & POLLIN)) {
            if (wl_display_dispatch(c->dpy) < 0) return -1;
        } else {
            wl_display_dispatch_pending(c->dpy);
        }
    }
    return c->configured ? 0 : -1;
}

/* 从串口脚本、systemd 单元或桌面快捷方式启动时，XDG_RUNTIME_DIR /
 * WAYLAND_DISPLAY 经常缺失或指向不存在的 socket。这里按「环境变量 →
 * $XDG_RUNTIME_DIR 下的 wayland-* → /run/user/<uid> 下的 wayland-*」
 * 顺序试连，第一个连上的就用。找不到就返回 NULL，由调用方回退。 */
static struct wl_display *connect_first(void)
{
    struct wl_display *d = wl_display_connect(NULL);
    if (d) return d;

    char dirs[2][64];
    int nd = 0;
    const char *xr = getenv("XDG_RUNTIME_DIR");
    if (xr && *xr) snprintf(dirs[nd++], sizeof(dirs[0]), "%s", xr);
    snprintf(dirs[nd++], sizeof(dirs[0]), "/run/user/%u", (unsigned)getuid());

    char keepx[64] = {0}, keepw[64] = {0};
    if (xr) snprintf(keepx, sizeof(keepx), "%s", xr);
    const char *wd = getenv("WAYLAND_DISPLAY");
    if (wd) snprintf(keepw, sizeof(keepw), "%s", wd);

    for (int i = 0; i < nd; i++) {
        DIR *dp = opendir(dirs[i]);
        if (!dp) continue;
        struct dirent *e;
        while ((e = readdir(dp))) {
            if (strncmp(e->d_name, "wayland-", 8) != 0) continue;
            if (strstr(e->d_name, ".lock")) continue;
            setenv("XDG_RUNTIME_DIR", dirs[i], 1);
            setenv("WAYLAND_DISPLAY", e->d_name, 1);
            d = wl_display_connect(e->d_name);
            if (d) {
                fprintf(stderr, "properos-wl: 自动选用合成器 %s/%s\n", dirs[i], e->d_name);
                closedir(dp);
                return d;
            }
        }
        closedir(dp);
    }

    /* 没连上就还原环境，避免影响后面的 framebuffer 回退 */
    if (keepx[0]) setenv("XDG_RUNTIME_DIR", keepx, 1);
    if (keepw[0]) setenv("WAYLAND_DISPLAY", keepw, 1);
    return NULL;
}

/* 失败点必须说清是哪一步，否则在设备上只能看到笼统的"初始化失败" */
#define WFAIL(m) do { fprintf(stderr, "properos-wl: %s\n", (m)); goto fail; } while (0)

int wl_open(gfx_t *g, const char *title)
{
    struct wlctx *c = calloc(1, sizeof(*c));
    if (!c) return -1;

    c->dpy = connect_first();
    if (!c->dpy) WFAIL("连不上合成器（XDG_RUNTIME_DIR / WAYLAND_DISPLAY 不对，或 socket 权限不足）");

    c->reg = wl_display_get_registry(c->dpy);
    wl_registry_add_listener(c->reg, &reg_listener, c);
    if (wl_display_roundtrip(c->dpy) < 0) WFAIL("registry 第一轮 roundtrip 失败");
    if (wl_display_roundtrip(c->dpy) < 0) WFAIL("registry 第二轮 roundtrip 失败");

    if (!c->comp || !c->shm || !c->wm) {
        fprintf(stderr, "properos-wl: 合成器缺少全局对象（compositor=%d shm=%d xdg_wm_base=%d）\n",
                c->comp != NULL, c->shm != NULL, c->wm != NULL);
        goto fail;
    }

    c->surf = wl_compositor_create_surface(c->comp);
    if (!c->surf) WFAIL("wl_compositor.create_surface 失败");
    c->xs = xdg_wm_base_get_xdg_surface(c->wm, c->surf);
    if (!c->xs) WFAIL("xdg_wm_base.get_xdg_surface 失败");
    xdg_surface_add_listener(c->xs, &xs_listener, c);
    c->tl = xdg_surface_get_toplevel(c->xs);
    if (!c->tl) WFAIL("xdg_surface.get_toplevel 失败");
    xdg_toplevel_add_listener(c->tl, &tl_listener, c);

    xdg_toplevel_set_title(c->tl, title ? title : "ProperOS");
    xdg_toplevel_set_app_id(c->tl, "properos-app");
    xdg_toplevel_set_fullscreen(c->tl, NULL);
    wl_surface_commit(c->surf);

    /* TCG / 软件渲染下首帧 configure 可能要十几秒，超时给足 */
    if (wait_configure(c, 20000) < 0) WFAIL("等待首帧 configure 超时（合成器没给尺寸）");

    if (c->pw <= 0 || c->ph <= 0) { c->pw = 720; c->ph = 1280; }
    if (make_buffer(c, c->pw, c->ph) < 0) WFAIL("wl_shm 缓冲区分配失败（memfd/ftruncate 不可用）");

    /* 首帧先黑屏，避免显示未初始化内存 */
    memset(c->pix, 0, c->pixlen);
    wl_surface_attach(c->surf, c->buf, 0, 0);
    wl_surface_damage(c->surf, 0, 0, c->w, c->h);
    wl_surface_commit(c->surf);
    wl_display_flush(c->dpy);

    G = c;

    /* 手势状态机初始化 */
    memset(&wl_in, 0, sizeof(wl_in));
    wl_in.w = c->w;
    wl_in.h = c->h;
    memset(&wl_dev, 0, sizeof(wl_dev));
    wl_dev.x = c->w / 2;
    wl_dev.y = c->h / 2;
    wl_in.dev[0] = wl_dev;
    wl_in.n = 1;
    q_reset();

    g->wl = c;
    g->fd = wl_display_get_fd(c->dpy);
    g->w = c->w;
    g->h = c->h;
    g->bpp = 32;
    g->line = c->w * 4;
    g->size = c->pixlen;
    g->mem = c->pix;
    g->buf = calloc((size_t)c->w * (size_t)c->h, sizeof(uint32_t));
    if (!g->buf) WFAIL("帧缓冲分配失败");

    fprintf(stderr, "properos-wl: 已作为 Wayland 窗口运行 %dx%d\n", c->w, c->h);
    return 0;

fail:
    g->wl = NULL;
    if (c->dpy) wl_display_disconnect(c->dpy);
    free(c);
    return -1;
}

void wl_flush(gfx_t *g)
{
    struct wlctx *c = g->wl;
    if (!c || c->closed) return;

    /* gfx 的 buf 是 0x00RRGGBB，小端下内存布局与 XRGB8888 一致 */
    memcpy(g->mem, g->buf, g->size);

    wl_surface_attach(c->surf, c->buf, 0, 0);
    wl_surface_damage(c->surf, 0, 0, c->w, c->h);
    wl_surface_commit(c->surf);
    wl_display_flush(c->dpy);
    wl_display_dispatch_pending(c->dpy);
}

void wl_close(gfx_t *g)
{
    struct wlctx *c = g->wl;
    if (!c) return;

    if (c->touch) wl_touch_destroy(c->touch);
    if (c->ptr) wl_pointer_destroy(c->ptr);
    if (c->buf) wl_buffer_destroy(c->buf);
    if (c->tl) xdg_toplevel_destroy(c->tl);
    if (c->xs) xdg_surface_destroy(c->xs);
    if (c->surf) wl_surface_destroy(c->surf);
    if (c->seat) wl_seat_destroy(c->seat);
    if (c->wm) xdg_wm_base_destroy(c->wm);
    if (c->shm) wl_shm_destroy(c->shm);
    if (c->comp) wl_compositor_destroy(c->comp);
    if (c->reg) wl_registry_destroy(c->reg);
    if (c->dpy) { wl_display_flush(c->dpy); wl_display_disconnect(c->dpy); }
    if (c->pix) munmap(c->pix, c->pixlen);
    free(c);
    g->wl = NULL;
    G = NULL;
}

int wl_poll_events(input_t *in, in_event_t *out, int max, int timeout_ms)
{
    (void)in;
    struct wlctx *c = G;
    if (!c) return 0;

    int n = 0, k = 0;
    q_flush_to(out, max, &k);
    n = k;
    if (n >= max) return n;

    wl_display_dispatch_pending(c->dpy);
    wl_display_flush(c->dpy);

    struct pollfd p = { wl_display_get_fd(c->dpy), POLLIN, 0 };
    int r = poll(&p, 1, timeout_ms);
    if (r > 0 && (p.revents & POLLIN))
        wl_display_dispatch(c->dpy);
    else
        wl_display_dispatch_pending(c->dpy);

    q_flush_to(out + n, max - n, &k);
    return n + k;
}
