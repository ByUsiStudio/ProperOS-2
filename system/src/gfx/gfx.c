/* ProperOS 25.0.0 Mobile · gfx 图形基础库实现
 *
 * 开发者：ByUsi Studio · 北啊呢
 */
#define _GNU_SOURCE
#include "gfx.h"
#include "font_atlas.h"
#ifdef PROPEROS_WAYLAND
#include "wl.h"
#endif

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <linux/vt.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ================= 画布 ================= */

static void flush_rect(gfx_t *g, int x0, int y0, int x1, int y1)
{
    if (g->bpp == 32) {
        for (int y = y0; y < y1; y++)
            memcpy(g->mem + (size_t)y * g->line + (size_t)x0 * 4,
                   g->buf + (size_t)y * g->w + x0, (size_t)(x1 - x0) * 4);
    } else if (g->bpp == 16) {
        for (int y = y0; y < y1; y++) {
            uint16_t *dst = (uint16_t *)(g->mem + (size_t)y * g->line);
            uint32_t *src = g->buf + (size_t)y * g->w;
            for (int x = x0; x < x1; x++) {
                uint32_t c = src[x];
                dst[x] = (uint16_t)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F));
            }
        }
    }
}

int gfx_open(gfx_t *g, const char *dev)
{
    memset(g, 0, sizeof(*g));
    g->fd = -1;

#ifdef PROPEROS_WAYLAND
    /* 在桌面会话里优先当 Wayland 客户端；失败再退回 framebuffer。
     * 只看 WAYLAND_DISPLAY 不够：从服务/串口里启动时往往只继承了
     * XDG_RUNTIME_DIR，socket 名要靠 wl 后端自己去目录里找。 */
    const char *wdenv = getenv("WAYLAND_DISPLAY");
    const char *xrenv = getenv("XDG_RUNTIME_DIR");
    if (!dev && (wdenv || xrenv)) {
        if (wl_open(g, getenv("PROPEROS_TITLE")) == 0) {
            gfx_reset_clip(g);
            return 0;
        }
        if (wdenv) {
            /* 明确给了 socket 却连不上，说明会话在但连不过去；退回 fb0
             * 会和合成器抢同一块显存，画面互相覆盖，必须报出来。 */
            fprintf(stderr, "properos: Wayland 初始化失败，回落到 framebuffer\n");
        }
        memset(g, 0, sizeof(*g));
        g->fd = -1;
    }
#endif

    const char *path = dev ? dev : (getenv("PROPEROS_FB") ? getenv("PROPEROS_FB") : "/dev/fb0");
    int fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0) return -1;

    struct fb_var_screeninfo vi;
    struct fb_fix_screeninfo fi;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &vi) < 0 || ioctl(fd, FBIOGET_FSCREENINFO, &fi) < 0) {
        close(fd);
        return -1;
    }
    g->fd = fd;
    g->w = (int)vi.xres;
    g->h = (int)vi.yres;
    g->bpp = (int)vi.bits_per_pixel;
    g->line = (int)fi.line_length;
    g->size = (size_t)fi.smem_len ? (size_t)fi.smem_len : (size_t)g->line * (size_t)g->h;
    g->mem = mmap(NULL, g->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (g->mem == MAP_FAILED) { close(fd); g->fd = -1; return -1; }
    g->buf = calloc((size_t)g->w * (size_t)g->h, sizeof(uint32_t));
    if (!g->buf) { munmap(g->mem, g->size); close(fd); g->fd = -1; return -1; }
    gfx_reset_clip(g);
    return 0;
}

int gfx_offscreen(gfx_t *g, int w, int h)
{
    memset(g, 0, sizeof(*g));
    g->fd = -1;
    g->w = w; g->h = h; g->bpp = 32; g->line = w * 4;
    g->size = (size_t)w * (size_t)h * 4;
    g->mem = calloc(1, g->size);
    g->buf = calloc((size_t)w * (size_t)h, sizeof(uint32_t));
    if (!g->mem || !g->buf) return -1;
    gfx_reset_clip(g);
    return 0;
}

void gfx_close(gfx_t *g)
{
#ifdef PROPEROS_WAYLAND
    if (g->wl) {
        free(g->buf);
        g->buf = NULL;
        wl_close(g);          /* Wayland 后端自己回收 shm 与 surface */
        memset(g, 0, sizeof(*g));
        g->fd = -1;
        return;
    }
#endif
    if (g->mem) {
        if (g->fd >= 0) munmap(g->mem, g->size);
        else free(g->mem);
    }
    free(g->buf);
    if (g->fd >= 0) close(g->fd);
    memset(g, 0, sizeof(*g));
    g->fd = -1;
}

/* ================= 控制台接管 =================
 *
 * fbcon 是 /dev/fb0 的"隐形邻居"：只要它还是当前 VT 的控制台，
 * 任何控制台输出、VT 切换、光标闪烁都会把整屏重绘成终端底色，
 * 把 GUI 的画面擦掉。解决办法是传统的 X11/fbi 做法——
 * 切到一个空闲 VT，再对该 VT 执行 KDSETMODE(KD_GRAPHICS)：
 * fbcon 从此不再碰 framebuffer，GUI 独占扫描输出。 */

static int g_tty_fd = -1;

int gfx_console_grab(int vt)
{
    int fd = -1;

    if (vt > 0) {
        int ctl = open("/dev/tty0", O_RDWR | O_CLOEXEC);
        if (ctl >= 0) {
            ioctl(ctl, VT_ACTIVATE, vt);
            ioctl(ctl, VT_WAITACTIVE, vt);
            close(ctl);
        }
        char p[32];
        snprintf(p, sizeof(p), "/dev/tty%d", vt);
        fd = open(p, O_RDWR | O_CLOEXEC);
    }
    if (fd < 0) fd = open("/dev/tty0", O_RDWR | O_CLOEXEC);
    if (fd < 0) return -1;
    if (ioctl(fd, KDSETMODE, KD_GRAPHICS) < 0) {
        close(fd);
        return -1;
    }
    g_tty_fd = fd;
    return 0;
}

void gfx_console_release(void)
{
    if (g_tty_fd < 0) return;
    ioctl(g_tty_fd, KDSETMODE, KD_TEXT);
    close(g_tty_fd);
    g_tty_fd = -1;
}

void gfx_flush(gfx_t *g)
{
#ifdef PROPEROS_WAYLAND
    if (g->wl) { wl_flush(g); return; }
#endif
    if (g->fd < 0) {   /* 离屏：同步到 mem 以便导出 */
        memcpy(g->mem, g->buf, g->size);
        return;
    }
    flush_rect(g, 0, 0, g->w, g->h);

    /* drm_fbdev 的延迟刷新并不可靠（无 console 事件时画面停在上一帧），
     * 这里主动发起一次 pan_display，强制把脏页提交到 scanout。 */
    struct fb_var_screeninfo vi;
    if (ioctl(g->fd, FBIOGET_VSCREENINFO, &vi) == 0) {
        vi.yoffset = 0;
        vi.activate = FB_ACTIVATE_NOW;
        ioctl(g->fd, FBIOPAN_DISPLAY, &vi);
    }
}

void gfx_set_clip(gfx_t *g, int x, int y, int w, int h)
{
    int x1 = x + w, y1 = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > g->w) x1 = g->w;
    if (y1 > g->h) y1 = g->h;
    g->cx0 = x; g->cy0 = y; g->cx1 = x1 > x ? x1 : x; g->cy1 = y1 > y ? y1 : y;
}

void gfx_reset_clip(gfx_t *g)
{
    g->cx0 = 0; g->cy0 = 0; g->cx1 = g->w; g->cy1 = g->h;
}

/* ================= 像素与色块 ================= */

void gfx_px(gfx_t *g, int x, int y, uint32_t c)
{
    if (x < g->cx0 || y < g->cy0 || x >= g->cx1 || y >= g->cy1) return;
    g->buf[(size_t)y * g->w + x] = c;
}

void gfx_px_a(gfx_t *g, int x, int y, uint32_t c, int a)
{
    if (a <= 0 || x < g->cx0 || y < g->cy0 || x >= g->cx1 || y >= g->cy1) return;
    if (a >= 255) { g->buf[(size_t)y * g->w + x] = c; return; }
    size_t i = (size_t)y * g->w + x;
    uint32_t d = g->buf[i];
    /* 整数定点混合，避免浮点误差积累 */
    int dr = (int)((d >> 16) & 0xff), dg = (int)((d >> 8) & 0xff), db = (int)(d & 0xff);
    int sr = (int)((c >> 16) & 0xff), sg = (int)((c >> 8) & 0xff), sb = (int)(c & 0xff);
    int ia = 255 - a;
    int r = (sr * a + dr * ia + 127) / 255;
    int gg = (sg * a + dg * ia + 127) / 255;
    int b = (sb * a + db * ia + 127) / 255;
    g->buf[i] = (uint32_t)((r << 16) | (gg << 8) | b);
}

void gfx_fill(gfx_t *g, int x, int y, int w, int h, uint32_t c)
{
    if (w <= 0 || h <= 0) return;
    int x0 = x < g->cx0 ? g->cx0 : x;
    int y0 = y < g->cy0 ? g->cy0 : y;
    int x1 = x + w > g->cx1 ? g->cx1 : x + w;
    int y1 = y + h > g->cy1 ? g->cy1 : y + h;
    for (int j = y0; j < y1; j++) {
        uint32_t *row = g->buf + (size_t)j * g->w;
        for (int i = x0; i < x1; i++) row[i] = c;
    }
}

void gfx_fill_a(gfx_t *g, int x, int y, int w, int h, uint32_t c, int a)
{
    if (a <= 0 || w <= 0 || h <= 0) return;
    if (a >= 255) { gfx_fill(g, x, y, w, h, c); return; }
    int x0 = x < g->cx0 ? g->cx0 : x;
    int y0 = y < g->cy0 ? g->cy0 : y;
    int x1 = x + w > g->cx1 ? g->cx1 : x + w;
    int y1 = y + h > g->cy1 ? g->cy1 : y + h;
    int sr = (int)((c >> 16) & 0xff), sg = (int)((c >> 8) & 0xff), sb = (int)(c & 0xff);
    int ia = 255 - a;
    for (int j = y0; j < y1; j++) {
        uint32_t *row = g->buf + (size_t)j * g->w;
        for (int i = x0; i < x1; i++) {
            uint32_t d = row[i];
            int r = (sr * a + (int)((d >> 16) & 0xff) * ia + 127) / 255;
            int gg = (sg * a + (int)((d >> 8) & 0xff) * ia + 127) / 255;
            int b = (sb * a + (int)(d & 0xff) * ia + 127) / 255;
            row[i] = (uint32_t)((r << 16) | (gg << 8) | b);
        }
    }
}

void gfx_rect(gfx_t *g, int x, int y, int w, int h, int t, uint32_t c)
{
    if (t <= 0) return;
    gfx_fill(g, x, y, w, t, c);
    gfx_fill(g, x, y + h - t, w, t, c);
    gfx_fill(g, x, y, t, h, c);
    gfx_fill(g, x + w - t, y, t, h, c);
}

void gfx_vgrad(gfx_t *g, int x, int y, int w, int h, uint32_t top, uint32_t bot)
{
    if (h <= 0) return;
    int tr = (int)((top >> 16) & 0xff), tg = (int)((top >> 8) & 0xff), tb = (int)(top & 0xff);
    int br = (int)((bot >> 16) & 0xff), bg = (int)((bot >> 8) & 0xff), bb = (int)(bot & 0xff);
    for (int j = 0; j < h; j++) {
        int t = h > 1 ? j * 255 / (h - 1) : 0;
        int r = (tr * (255 - t) + br * t) / 255;
        int gg = (tg * (255 - t) + bg * t) / 255;
        int b = (tb * (255 - t) + bb * t) / 255;
        gfx_fill(g, x, y + j, w, 1, (uint32_t)((r << 16) | (gg << 8) | b));
    }
}

/* ================= SDF 圆角 ================= */

static inline float sd_round_box(float px, float py, float cx, float cy, float hw, float hh, float r)
{
    float qx = fabsf(px - cx) - (hw - r);
    float qy = fabsf(py - cy) - (hh - r);
    float ax = qx > 0.f ? qx : 0.f;
    float ay = qy > 0.f ? qy : 0.f;
    float m = qx > qy ? qx : qy;
    if (m > 0.f) m = 0.f;
    return sqrtf(ax * ax + ay * ay) + m - r;
}

static void round_corner(gfx_t *g, int x, int y, int w, int h, int r, int ox, int oy,
                         uint32_t c, int a)
{
    /* 在 ox,oy 处的 r×r 角方块内按 SDF 覆盖率填充 */
    float cx = (float)x + (float)w * 0.5f;
    float cy = (float)y + (float)h * 0.5f;
    float hw = (float)w * 0.5f, hh = (float)h * 0.5f;
    for (int j = 0; j < r; j++) {
        for (int i = 0; i < r; i++) {
            float d = sd_round_box((float)(ox + i) + 0.5f, (float)(oy + j) + 0.5f,
                                   cx, cy, hw, hh, (float)r);
            float cov = 0.5f - d;
            if (cov <= 0.f) continue;
            if (cov > 1.f) cov = 1.f;
            gfx_px_a(g, ox + i, oy + j, c, (int)(cov * (float)a + 0.5f));
        }
    }
}

void gfx_round_a(gfx_t *g, int x, int y, int w, int h, int r, uint32_t c, int a)
{
    if (w <= 0 || h <= 0 || a <= 0) return;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    if (r <= 0) { gfx_fill_a(g, x, y, w, h, c, a); return; }

    /* 十字方向整块填充，四角交给 SDF，互不重叠 */
    gfx_fill_a(g, x + r, y, w - 2 * r, h, c, a);
    gfx_fill_a(g, x, y + r, r, h - 2 * r, c, a);
    gfx_fill_a(g, x + w - r, y + r, r, h - 2 * r, c, a);

    round_corner(g, x, y, w, h, r, x, y, c, a);
    round_corner(g, x, y, w, h, r, x + w - r, y, c, a);
    round_corner(g, x, y, w, h, r, x, y + h - r, c, a);
    round_corner(g, x, y, w, h, r, x + w - r, y + h - r, c, a);
}

void gfx_round(gfx_t *g, int x, int y, int w, int h, int r, uint32_t c)
{
    gfx_round_a(g, x, y, w, h, r, c, 255);
}

void gfx_round_outline(gfx_t *g, int x, int y, int w, int h, int r, int t, uint32_t c)
{
    if (t <= 0) return;
    float cx = x + w * 0.5f, cy = y + h * 0.5f;
    float hw = w * 0.5f, hh = h * 0.5f;
    for (int j = y - 1; j <= y + h; j++) {
        for (int i = x - 1; i <= x + w; i++) {
            float d = sd_round_box((float)i + 0.5f, (float)j + 0.5f, cx, cy, hw, hh, (float)r);
            float outer = 0.5f - d;                 /* 外缘覆盖率 */
            float inner = 0.5f - (d + (float)t);    /* 内缘 */
            if (outer > 1.f) outer = 1.f;
            if (outer <= 0.f) continue;
            if (inner < 0.f) inner = 0.f;
            if (inner > 1.f) inner = 1.f;
            float cov = outer - inner;
            if (cov <= 0.f) continue;
            if (cov > 1.f) cov = 1.f;
            gfx_px_a(g, i, j, c, (int)(cov * 255.f));
        }
    }
}

void gfx_shadow(gfx_t *g, int x, int y, int w, int h, int r, int spread, int alpha)
{
    if (spread <= 0 || alpha <= 0) return;
    float cx = x + w * 0.5f, cy = y + h * 0.5f;
    float hw = w * 0.5f, hh = h * 0.5f;
    float sigma = (float)spread * 0.55f;
    float inv2s2 = 1.f / (2.f * sigma * sigma);
    int pad = spread;
    int y0 = y - pad < g->cy0 ? g->cy0 : y - pad;
    int y1 = y + h + pad > g->cy1 ? g->cy1 : y + h + pad;
    int x0 = x - pad < g->cx0 ? g->cx0 : x - pad;
    int x1 = x + w + pad > g->cx1 ? g->cx1 : x + w + pad;
    for (int j = y0; j < y1; j++) {
        for (int i = x0; i < x1; i++) {
            float d = sd_round_box((float)i + 0.5f, (float)j + 0.5f, cx, cy, hw, hh, (float)r);
            if (d < -1.f) continue;              /* 卡片内部不画阴影 */
            float back = d < 0.f ? 0.f : d;
            float a = expf(-back * back * inv2s2) * (float)alpha;
            if (a < 0.5f) continue;
            gfx_px_a(g, i, j, 0x1A1F2E, (int)a);
        }
    }
}

void gfx_circle_a(gfx_t *g, int cx, int cy, int r, uint32_t c, int a)
{
    if (r <= 0) return;
    int x0 = cx - r - 1, x1 = cx + r + 1, y0 = cy - r - 1, y1 = cy + r + 1;
    for (int j = y0; j <= y1; j++) {
        for (int i = x0; i <= x1; i++) {
            float dx = (float)i + 0.5f - (float)cx;
            float dy = (float)j + 0.5f - (float)cy;
            float d = sqrtf(dx * dx + dy * dy) - (float)r;
            float cov = 0.5f - d;
            if (cov <= 0.f) continue;
            if (cov > 1.f) cov = 1.f;
            gfx_px_a(g, i, j, c, (int)(cov * (float)a));
        }
    }
}

void gfx_circle(gfx_t *g, int cx, int cy, int r, uint32_t c)
{
    gfx_circle_a(g, cx, cy, r, c, 255);
}

/* 径向柔光：alpha 随半径二次衰减，用作大面积背景光晕 */
void gfx_glow(gfx_t *g, int cx, int cy, int r, uint32_t c, int alpha)
{
    if (r <= 0 || alpha <= 0) return;
    float rf = (float)r;
    int y0 = cy - r < g->cy0 ? g->cy0 : cy - r;
    int y1 = cy + r > g->cy1 ? g->cy1 : cy + r;
    int x0 = cx - r < g->cx0 ? g->cx0 : cx - r;
    int x1 = cx + r > g->cx1 ? g->cx1 : cx + r;
    for (int j = y0; j < y1; j++) {
        for (int i = x0; i < x1; i++) {
            float dx = (float)i + 0.5f - (float)cx;
            float dy = (float)j + 0.5f - (float)cy;
            float d = sqrtf(dx * dx + dy * dy) / rf;
            if (d >= 1.f) continue;
            float f = 1.f - d;
            f = f * f;
            gfx_px_a(g, i, j, c, (int)(f * (float)alpha));
        }
    }
}

void gfx_ring(gfx_t *g, int cx, int cy, int r, int thick, uint32_t c, int a)
{
    if (r <= 0 || thick <= 0) return;
    float rr = (float)r, rt = (float)thick;
    for (int j = cy - r - thick; j <= cy + r + thick; j++) {
        for (int i = cx - r - thick; i <= cx + r + thick; i++) {
            float dx = (float)i + 0.5f - (float)cx;
            float dy = (float)j + 0.5f - (float)cy;
            float d = sqrtf(dx * dx + dy * dy);
            float cov = 1.f - fabsf(d - rr) / (rt * 0.5f);
            if (cov <= 0.f) continue;
            if (cov > 1.f) cov = 1.f;
            gfx_px_a(g, i, j, c, (int)(cov * (float)a));
        }
    }
}

void gfx_line(gfx_t *g, float x0, float y0, float x1, float y1, float thick, uint32_t c)
{
    float dx = x1 - x0, dy = y1 - y0;
    float len2 = dx * dx + dy * dy;
    if (len2 < 0.0001f) { gfx_circle(g, (int)x0, (int)y0, (int)(thick * 0.5f), c); return; }
    float half = thick * 0.5f;
    int bx0 = (int)floorf(fminf(x0, x1) - half - 1);
    int bx1 = (int)ceilf(fmaxf(x0, x1) + half + 1);
    int by0 = (int)floorf(fminf(y0, y1) - half - 1);
    int by1 = (int)ceilf(fmaxf(y0, y1) + half + 1);
    for (int j = by0; j <= by1; j++) {
        for (int i = bx0; i <= bx1; i++) {
            float px = (float)i + 0.5f - x0, py = (float)j + 0.5f - y0;
            float t = (px * dx + py * dy) / len2;
            if (t < 0.f) t = 0.f;
            if (t > 1.f) t = 1.f;
            float ex = px - t * dx, ey = py - t * dy;
            float d = sqrtf(ex * ex + ey * ey) - half;
            float cov = 0.5f - d;
            if (cov <= 0.f) continue;
            if (cov > 1.f) cov = 1.f;
            gfx_px_a(g, i, j, c, (int)(cov * 255.f));
        }
    }
}

/* ================= 图标 ================= */

typedef struct { const char *id; } icon_entry;

static const char *ICON_IDS[] = {
    "phone", "message", "browser", "camera", "settings", "files", "terminal",
    "music", "photos", "clock", "weather", "maps", "mail", "calc", "record",
    "notes", "store", "about", "search", "wifi", "battery", "bluetooth",
    "airplane", "brightness", "volume", "lock", "power", "back", "home",
    "menu", "close", "check", "plus", "minus", "chevron", "grid", "download",
    "shield", "chip", "update", "user", "refresh", "trash", "edit", "folder",
    "play", "pause", "skip", "signal", "dnd", "flashlight", "rotate",
};

int gfx_icon_known(const char *id)
{
    for (size_t i = 0; i < sizeof(ICON_IDS) / sizeof(ICON_IDS[0]); i++)
        if (!strcmp(id, ICON_IDS[i])) return 1;
    return 0;
}

/* 所有图标在 s×s 的方框内绘制，线宽按比例 */
void gfx_icon(gfx_t *g, const char *id, int x, int y, int s, uint32_t c)
{
    float cx = x + s * 0.5f, cy = y + s * 0.5f;
    float t = fmaxf(1.4f, s * 0.060f);      /* 线宽 */
    float r = s * 0.52f;                     /* 外接半径 */

    if (!strcmp(id, "phone")) {
        /* 听筒：斜置主杆 + 两端垂直手柄 */
        gfx_line(g, cx - r * .42f, cy - r * .42f, cx + r * .42f, cy + r * .42f, t * 2.4f, c);
        gfx_line(g, cx - r * .58f, cy - r * .16f, cx - r * .16f, cy - r * .58f, t * 1.8f, c);
        gfx_line(g, cx + r * .16f, cy + r * .58f, cx + r * .58f, cy + r * .16f, t * 1.8f, c);
    } else if (!strcmp(id, "message")) {
        gfx_round(g, (int)(cx - r * .78f), (int)(cy - r * .62f), (int)(r * 1.56f), (int)(r * 1.06f), (int)(r * .32f), c);
        gfx_line(g, cx - r * .26f, cy + r * .44f, cx - r * .05f, cy + r * .78f, t * 1.8f, c);
        gfx_line(g, cx - r * .26f, cy + r * .44f, cx + r * .10f, cy + r * .44f, t * 1.8f, c);
    } else if (!strcmp(id, "browser")) {
        gfx_ring(g, (int)cx, (int)cy, (int)(r * .80f), (int)t, c, 255);
        gfx_line(g, cx, cy - r * .80f, cx, cy + r * .80f, t * .9f, c);
        gfx_line(g, cx - r * .80f, cy, cx + r * .80f, cy, t * .9f, c);
        gfx_line(g, cx - r * .40f, cy - r * .62f, cx + r * .40f, cy - r * .62f, t * .8f, c);
        gfx_line(g, cx - r * .40f, cy + r * .62f, cx + r * .40f, cy + r * .62f, t * .8f, c);
    } else if (!strcmp(id, "camera")) {
        gfx_round_outline(g, (int)(cx - r * .80f), (int)(cy - r * .50f), (int)(r * 1.60f), (int)(r * 1.06f), (int)(r * .24f), (int)t, c);
        gfx_ring(g, (int)cx, (int)(cy + r * .03f), (int)(r * .28f), (int)t, c, 255);
        gfx_line(g, cx - r * .22f, cy - r * .50f, cx - r * .14f, cy - r * .68f, t, c);
        gfx_line(g, cx - r * .14f, cy - r * .68f, cx + r * .14f, cy - r * .68f, t, c);
        gfx_line(g, cx + r * .14f, cy - r * .68f, cx + r * .22f, cy - r * .50f, t, c);
    } else if (!strcmp(id, "settings")) {
        for (int k = 0; k < 8; k++) {
            float a = (float)k * (float)M_PI / 4.f + (float)M_PI / 8.f;
            gfx_line(g, cx + cosf(a) * r * .56f, cy + sinf(a) * r * .56f,
                     cx + cosf(a) * r * .76f, cy + sinf(a) * r * .76f, t * 1.9f, c);
        }
        gfx_ring(g, (int)cx, (int)cy, (int)(r * .54f), (int)t, c, 255);
        gfx_circle(g, (int)cx, (int)cy, (int)(r * .16f), c);
    } else if (!strcmp(id, "files") || !strcmp(id, "folder")) {
        gfx_round(g, (int)(cx - r * .80f), (int)(cy - r * .40f), (int)(r * .70f), (int)(r * .26f), (int)(r * .10f), c);
        gfx_round(g, (int)(cx - r * .80f), (int)(cy - r * .26f), (int)(r * 1.60f), (int)(r * .92f), (int)(r * .18f), c);
    } else if (!strcmp(id, "terminal")) {
        gfx_round_outline(g, (int)(cx - r * .84f), (int)(cy - r * .68f), (int)(r * 1.68f), (int)(r * 1.36f), (int)(r * .24f), (int)t, c);
        gfx_line(g, cx - r * .42f, cy - r * .30f, cx - r * .14f, cy, t * 1.5f, c);
        gfx_line(g, cx - r * .14f, cy, cx - r * .42f, cy + r * .30f, t * 1.5f, c);
        gfx_line(g, cx + r * .04f, cy + r * .32f, cx + r * .42f, cy + r * .32f, t * 1.5f, c);
    } else if (!strcmp(id, "music")) {
        gfx_circle(g, (int)(cx - r * .30f), (int)(cy + r * .46f), (int)(r * .26f), c);
        gfx_line(g, cx - r * .04f, cy + r * .46f, cx - r * .04f, cy - r * .62f, t * 1.5f, c);
        gfx_line(g, cx - r * .04f, cy - r * .62f, cx + r * .44f, cy - r * .48f, t * 1.5f, c);
        gfx_line(g, cx + r * .44f, cy - r * .48f, cx + r * .44f, cy + r * .30f, t * 1.5f, c);
        gfx_circle(g, (int)(cx + r * .18f), (int)(cy + r * .30f), (int)(r * .22f), c);
    } else if (!strcmp(id, "photos")) {
        gfx_round_outline(g, (int)(cx - r * .82f), (int)(cy - r * .64f), (int)(r * 1.64f), (int)(r * 1.28f), (int)(r * .20f), (int)t, c);
        gfx_circle(g, (int)(cx - r * .40f), (int)(cy - r * .26f), (int)(r * .13f), c);
        gfx_line(g, cx - r * .60f, cy + r * .42f, cx - r * .06f, cy - r * .06f, t * 1.5f, c);
        gfx_line(g, cx - r * .18f, cy + r * .42f, cx + r * .34f, cy - r * .04f, t * 1.5f, c);
    } else if (!strcmp(id, "clock")) {
        gfx_ring(g, (int)cx, (int)cy, (int)(r * .80f), (int)t, c, 255);
        gfx_line(g, cx, cy, cx, cy - r * .46f, t * 1.3f, c);
        gfx_line(g, cx, cy, cx + r * .40f, cy + r * .12f, t * 1.3f, c);
    } else if (!strcmp(id, "weather")) {
        /* 太阳：实心圆 + 短射线 */
        gfx_circle(g, (int)(cx - r * .28f), (int)(cy - r * .34f), (int)(r * .22f), c);
        for (int k = 0; k < 8; k++) {
            float a = (float)k * (float)M_PI / 4.f;
            gfx_line(g, cx - r * .28f + cosf(a) * r * .32f, cy - r * .34f + sinf(a) * r * .32f,
                     cx - r * .28f + cosf(a) * r * .46f, cy - r * .34f + sinf(a) * r * .46f, t * .85f, c);
        }
        /* 云：描边 */
        gfx_round_outline(g, (int)(cx - r * .54f), (int)(cy + r * .10f), (int)(r * 1.10f), (int)(r * .42f), (int)(r * .21f), (int)t, c);
        gfx_circle(g, (int)(cx + r * .16f), (int)(cy + r * .06f), (int)(r * .20f), c);
    } else if (!strcmp(id, "maps")) {
        gfx_circle(g, (int)cx, (int)(cy - r * .22f), (int)(r * .50f), c);
        gfx_line(g, cx - r * .34f, cy + r * .12f, cx, cy + r * .78f, t * 1.7f, c);
        gfx_line(g, cx + r * .34f, cy + r * .12f, cx, cy + r * .78f, t * 1.7f, c);
        gfx_circle(g, (int)cx, (int)(cy - r * .22f), (int)(r * .18f), 0xFFFFFF);
    } else if (!strcmp(id, "mail")) {
        gfx_round_outline(g, (int)(cx - r * .82f), (int)(cy - r * .54f), (int)(r * 1.64f), (int)(r * 1.08f), (int)(r * .14f), (int)t, c);
        gfx_line(g, cx - r * .74f, cy - r * .44f, cx, cy + r * .06f, t * 1.2f, c);
        gfx_line(g, cx + r * .74f, cy - r * .44f, cx, cy + r * .06f, t * 1.2f, c);
    } else if (!strcmp(id, "calc")) {
        gfx_round_outline(g, (int)(cx - r * .56f), (int)(cy - r * .76f), (int)(r * 1.12f), (int)(r * 1.52f), (int)(r * .18f), (int)t, c);
        for (int j = 0; j < 3; j++)
            for (int i = 0; i < 3; i++)
                gfx_circle(g, (int)(cx + (i - 1) * r * .32f), (int)(cy + r * .10f + j * r * .32f), (int)(r * .075f), c);
    } else if (!strcmp(id, "record")) {
        gfx_ring(g, (int)cx, (int)cy, (int)(r * .74f), (int)t, c, 255);
        gfx_circle(g, (int)cx, (int)cy, (int)(r * .34f), c);
    } else if (!strcmp(id, "notes")) {
        gfx_round_outline(g, (int)(cx - r * .64f), (int)(cy - r * .76f), (int)(r * 1.28f), (int)(r * 1.52f), (int)(r * .16f), (int)t, c);
        for (int k = 0; k < 3; k++)
            gfx_line(g, cx - r * .40f, cy - r * .30f + k * r * .30f, cx + r * .40f, cy - r * .30f + k * r * .30f, t, c);
    } else if (!strcmp(id, "store")) {
        gfx_round(g, (int)(cx - r * .76f), (int)(cy - r * .30f), (int)(r * 1.52f), (int)(r * 1.04f), (int)(r * .18f), c);
        gfx_line(g, cx - r * .34f, cy - r * .30f, cx - r * .34f, cy - r * .56f, t * 1.4f, c);
        gfx_line(g, cx + r * .34f, cy - r * .30f, cx + r * .34f, cy - r * .56f, t * 1.4f, c);
        gfx_line(g, cx - r * .34f, cy - r * .56f, cx + r * .34f, cy - r * .56f, t * 1.4f, c);
    } else if (!strcmp(id, "about")) {
        gfx_ring(g, (int)cx, (int)cy, (int)(r * .80f), (int)t, c, 255);
        gfx_circle(g, (int)cx, (int)(cy - r * .38f), (int)(r * .10f), c);
        gfx_line(g, cx, cy - r * .10f, cx, cy + r * .42f, t * 1.3f, c);
    } else if (!strcmp(id, "search")) {
        gfx_ring(g, (int)(cx - r * .10f), (int)(cy - r * .10f), (int)(r * .52f), (int)t, c, 255);
        gfx_line(g, cx + r * .18f, cy + r * .18f, cx + r * .60f, cy + r * .60f, t * 1.5f, c);
    } else if (!strcmp(id, "wifi")) {
        for (int k = 0; k < 3; k++) {
            float rr = r * (0.30f + k * 0.26f);
            for (int a = 0; a <= 60; a++) {
                float ang = (-55.f + (float)a * 110.f / 60.f) * (float)M_PI / 180.f;
                float px = cx + sinf(ang) * rr, py = cy + r * .34f - cosf(ang) * rr;
                gfx_circle(g, (int)px, (int)py, (int)(t * .85f), c);
            }
        }
        gfx_circle(g, (int)cx, (int)(cy + r * .40f), (int)(t * 1.1f), c);
    } else if (!strcmp(id, "battery")) {
        gfx_round_outline(g, (int)(cx - r * .72f), (int)(cy - r * .38f), (int)(r * 1.34f), (int)(r * .76f), (int)(r * .16f), (int)t, c);
        gfx_fill(g, (int)(cx + r * .62f), (int)(cy - r * .16f), (int)(r * .16f), (int)(r * .32f), c);
        gfx_fill(g, (int)(cx - r * .58f), (int)(cy - r * .22f), (int)(r * .86f), (int)(r * .44f), c);
    } else if (!strcmp(id, "bluetooth")) {
        gfx_line(g, cx - r * .40f, cy - r * .46f, cx + r * .34f, cy + r * .30f, t, c);
        gfx_line(g, cx + r * .34f, cy + r * .30f, cx, cy + r * .62f, t, c);
        gfx_line(g, cx, cy + r * .62f, cx, cy - r * .62f, t, c);
        gfx_line(g, cx, cy - r * .62f, cx + r * .34f, cy - r * .30f, t, c);
        gfx_line(g, cx + r * .34f, cy - r * .30f, cx - r * .40f, cy + r * .46f, t, c);
    } else if (!strcmp(id, "airplane")) {
        gfx_line(g, cx - r * .62f, cy + r * .30f, cx + r * .62f, cy + r * .30f, t * 1.3f, c);
        gfx_line(g, cx, cy + r * .30f, cx, cy - r * .62f, t * 1.3f, c);
        gfx_line(g, cx - r * .30f, cy - r * .18f, cx + r * .30f, cy - r * .18f, t * 1.1f, c);
    } else if (!strcmp(id, "brightness") || !strcmp(id, "flashlight")) {
        gfx_circle(g, (int)cx, (int)cy, (int)(r * .34f), c);
        for (int k = 0; k < 8; k++) {
            float a = (float)k * (float)M_PI / 4.f;
            gfx_line(g, cx + cosf(a) * r * .54f, cy + sinf(a) * r * .54f,
                     cx + cosf(a) * r * .82f, cy + sinf(a) * r * .82f, t, c);
        }
    } else if (!strcmp(id, "volume")) {
        gfx_fill(g, (int)(cx - r * .62f), (int)(cy - r * .16f), (int)(r * .26f), (int)(r * .32f), c);
        gfx_line(g, cx - r * .36f, cy - r * .16f, cx - r * .04f, cy - r * .46f, t * 1.2f, c);
        gfx_line(g, cx - r * .36f, cy + r * .16f, cx - r * .04f, cy + r * .46f, t * 1.2f, c);
        gfx_line(g, cx - r * .04f, cy - r * .46f, cx - r * .04f, cy + r * .46f, t * 1.2f, c);
        gfx_line(g, cx + r * .16f, cy - r * .30f, cx + r * .46f, cy, t, c);
        gfx_line(g, cx + r * .46f, cy, cx + r * .16f, cy + r * .30f, t, c);
    } else if (!strcmp(id, "lock")) {
        gfx_round(g, (int)(cx - r * .50f), (int)(cy - r * .02f), (int)(r * 1.00f), (int)(r * .74f), (int)(r * .16f), c);
        gfx_line(g, cx - r * .30f, cy - r * .02f, cx - r * .30f, cy - r * .30f, t * 1.3f, c);
        gfx_line(g, cx + r * .30f, cy - r * .02f, cx + r * .30f, cy - r * .30f, t * 1.3f, c);
        for (int a = 0; a <= 40; a++) {
            float ang = (float)M_PI + (float)a * (float)M_PI / 40.f;
            gfx_circle(g, (int)(cx + cosf(ang) * r * .30f), (int)(cy - r * .30f + sinf(ang) * r * .30f), (int)(t * .65f), c);
        }
    } else if (!strcmp(id, "power")) {
        gfx_ring(g, (int)cx, (int)cy, (int)(r * .70f), (int)t, c, 255);
        gfx_fill(g, (int)(cx - t * .55f), (int)(cy - r * .84f), (int)(t * 1.1f), (int)(r * .66f), 0x000000);
        gfx_fill(g, (int)(cx - t * .55f), (int)(cy - r * .84f), (int)(t * 1.1f), (int)(r * .40f), c);
    } else if (!strcmp(id, "back") || !strcmp(id, "chevron")) {
        gfx_line(g, cx + r * .22f, cy - r * .46f, cx - r * .28f, cy, t * 1.6f, c);
        gfx_line(g, cx - r * .28f, cy, cx + r * .22f, cy + r * .46f, t * 1.6f, c);
    } else if (!strcmp(id, "home")) {
        gfx_line(g, cx - r * .62f, cy + r * .02f, cx, cy - r * .54f, t * 1.4f, c);
        gfx_line(g, cx, cy - r * .54f, cx + r * .62f, cy + r * .02f, t * 1.4f, c);
        gfx_line(g, cx - r * .46f, cy + r * .02f, cx - r * .46f, cy + r * .58f, t * 1.4f, c);
        gfx_line(g, cx + r * .46f, cy + r * .02f, cx + r * .46f, cy + r * .58f, t * 1.4f, c);
        gfx_line(g, cx - r * .46f, cy + r * .58f, cx + r * .46f, cy + r * .58f, t * 1.4f, c);
    } else if (!strcmp(id, "menu")) {
        for (int k = -1; k <= 1; k++)
            gfx_line(g, cx - r * .58f, cy + k * r * .34f, cx + r * .58f, cy + k * r * .34f, t * 1.4f, c);
    } else if (!strcmp(id, "close")) {
        gfx_line(g, cx - r * .42f, cy - r * .42f, cx + r * .42f, cy + r * .42f, t * 1.4f, c);
        gfx_line(g, cx + r * .42f, cy - r * .42f, cx - r * .42f, cy + r * .42f, t * 1.4f, c);
    } else if (!strcmp(id, "check")) {
        gfx_line(g, cx - r * .46f, cy + r * .02f, cx - r * .10f, cy + r * .40f, t * 1.6f, c);
        gfx_line(g, cx - r * .10f, cy + r * .40f, cx + r * .50f, cy - r * .40f, t * 1.6f, c);
    } else if (!strcmp(id, "plus")) {
        gfx_line(g, cx - r * .52f, cy, cx + r * .52f, cy, t * 1.5f, c);
        gfx_line(g, cx, cy - r * .52f, cx, cy + r * .52f, t * 1.5f, c);
    } else if (!strcmp(id, "minus")) {
        gfx_line(g, cx - r * .52f, cy, cx + r * .52f, cy, t * 1.5f, c);
    } else if (!strcmp(id, "grid")) {
        for (int j = 0; j < 2; j++)
            for (int i = 0; i < 2; i++)
                gfx_round(g, (int)(cx - r * .54f + i * r * .60f), (int)(cy - r * .54f + j * r * .60f),
                          (int)(r * .44f), (int)(r * .44f), (int)(r * .12f), c);
    } else if (!strcmp(id, "download")) {
        gfx_line(g, cx, cy - r * .60f, cx, cy + r * .22f, t * 1.4f, c);
        gfx_line(g, cx - r * .30f, cy - r * .06f, cx, cy + r * .26f, t * 1.4f, c);
        gfx_line(g, cx + r * .30f, cy - r * .06f, cx, cy + r * .26f, t * 1.4f, c);
        gfx_line(g, cx - r * .52f, cy + r * .56f, cx + r * .52f, cy + r * .56f, t * 1.4f, c);
    } else if (!strcmp(id, "shield")) {
        gfx_line(g, cx - r * .52f, cy - r * .46f, cx + r * .52f, cy - r * .46f, t * 1.3f, c);
        gfx_line(g, cx - r * .52f, cy - r * .46f, cx - r * .52f, cy + r * .06f, t * 1.3f, c);
        gfx_line(g, cx + r * .52f, cy - r * .46f, cx + r * .52f, cy + r * .06f, t * 1.3f, c);
        gfx_line(g, cx - r * .52f, cy + r * .06f, cx, cy + r * .62f, t * 1.3f, c);
        gfx_line(g, cx + r * .52f, cy + r * .06f, cx, cy + r * .62f, t * 1.3f, c);
    } else if (!strcmp(id, "chip")) {
        gfx_round_outline(g, (int)(cx - r * .48f), (int)(cy - r * .48f), (int)(r * .96f), (int)(r * .96f), (int)(r * .12f), (int)t, c);
        gfx_round(g, (int)(cx - r * .20f), (int)(cy - r * .20f), (int)(r * .40f), (int)(r * .40f), (int)(r * .06f), c);
        for (int k = -1; k <= 1; k++) {
            gfx_line(g, cx + k * r * .30f, cy - r * .48f, cx + k * r * .30f, cy - r * .70f, t, c);
            gfx_line(g, cx + k * r * .30f, cy + r * .48f, cx + k * r * .30f, cy + r * .70f, t, c);
            gfx_line(g, cx - r * .48f, cy + k * r * .30f, cx - r * .70f, cy + k * r * .30f, t, c);
            gfx_line(g, cx + r * .48f, cy + k * r * .30f, cx + r * .70f, cy + k * r * .30f, t, c);
        }
    } else if (!strcmp(id, "update") || !strcmp(id, "refresh")) {
        for (int a = 0; a <= 260; a++) {
            float ang = (float)a * (float)M_PI / 180.f;
            gfx_circle(g, (int)(cx + cosf(ang) * r * .58f), (int)(cy + sinf(ang) * r * .58f), (int)(t * .8f), c);
        }
        gfx_line(g, cx + r * .58f, cy - r * .34f, cx + r * .58f, cy + r * .06f, t * 1.4f, c);
        gfx_line(g, cx + r * .58f, cy + r * .06f, cx + r * .26f, cy + r * .06f, t * 1.4f, c);
    } else if (!strcmp(id, "user")) {
        gfx_circle(g, (int)cx, (int)(cy - r * .28f), (int)(r * .30f), c);
        for (int a = 200; a <= 340; a++) {
            float ang = (float)a * (float)M_PI / 180.f;
            gfx_circle(g, (int)(cx + cosf(ang) * r * .52f), (int)(cy + r * .48f + sinf(ang) * r * .42f), (int)(t * .8f), c);
        }
    } else if (!strcmp(id, "trash")) {
        gfx_round_outline(g, (int)(cx - r * .38f), (int)(cy - r * .34f), (int)(r * .76f), (int)(r * .92f), (int)(r * .12f), (int)t, c);
        gfx_line(g, cx - r * .52f, cy - r * .40f, cx + r * .52f, cy - r * .40f, t, c);
        gfx_line(g, cx - r * .16f, cy - r * .56f, cx + r * .16f, cy - r * .56f, t, c);
    } else if (!strcmp(id, "edit")) {
        gfx_line(g, cx - r * .40f, cy + r * .40f, cx + r * .40f, cy - r * .40f, t * 2.2f, c);
        gfx_line(g, cx - r * .44f, cy + r * .44f, cx - r * .56f, cy + r * .56f, t * 1.2f, c);
    } else if (!strcmp(id, "play")) {
        gfx_line(g, cx - r * .26f, cy - r * .44f, cx + r * .44f, cy, t * 1.8f, c);
        gfx_line(g, cx + r * .44f, cy, cx - r * .26f, cy + r * .44f, t * 1.8f, c);
        gfx_line(g, cx - r * .26f, cy + r * .44f, cx - r * .26f, cy - r * .44f, t * 1.8f, c);
    } else if (!strcmp(id, "pause")) {
        gfx_fill(g, (int)(cx - r * .34f), (int)(cy - r * .42f), (int)(r * .22f), (int)(r * .84f), c);
        gfx_fill(g, (int)(cx + r * .12f), (int)(cy - r * .42f), (int)(r * .22f), (int)(r * .84f), c);
    } else if (!strcmp(id, "skip")) {
        gfx_line(g, cx - r * .34f, cy - r * .42f, cx + r * .10f, cy, t * 1.8f, c);
        gfx_line(g, cx + r * .10f, cy, cx - r * .34f, cy + r * .42f, t * 1.8f, c);
        gfx_fill(g, (int)(cx + r * .22f), (int)(cy - r * .42f), (int)(r * .16f), (int)(r * .84f), c);
    } else if (!strcmp(id, "signal")) {
        for (int k = 0; k < 4; k++)
            gfx_fill(g, (int)(cx - r * .56f + k * r * .30f), (int)(cy + r * .46f - (k + 1) * r * .28f),
                     (int)(r * .18f), (int)((k + 1) * r * .28f), c);
    } else if (!strcmp(id, "dnd")) {
        gfx_ring(g, (int)cx, (int)cy, (int)(r * .64f), (int)t, c, 255);
        gfx_line(g, cx - r * .52f, cy - r * .52f, cx + r * .52f, cy + r * .52f, t * 1.4f, c);
    } else if (!strcmp(id, "rotate")) {
        for (int a = 40; a <= 300; a++) {
            float ang = (float)a * (float)M_PI / 180.f;
            gfx_circle(g, (int)(cx + cosf(ang) * r * .56f), (int)(cy + sinf(ang) * r * .56f), (int)(t * .8f), c);
        }
        gfx_line(g, cx + r * .50f, cy - r * .56f, cx + r * .58f, cy - r * .14f, t * 1.3f, c);
        gfx_line(g, cx + r * .58f, cy - r * .14f, cx + r * .18f, cy - r * .22f, t * 1.3f, c);
    } else {
        /* 未知图标：圆点占位，避免空白 */
        gfx_circle(g, (int)cx, (int)cy, (int)(r * .30f), c);
    }
}

/* ================= 文本 ================= */

static int glyph_cmp(const void *key, const void *elem)
{
    uint32_t cp = *(const uint32_t *)key;
    const struct font_glyph *g = (const struct font_glyph *)elem;
    return cp < g->cp ? -1 : (cp > g->cp ? 1 : 0);
}

static const struct font_glyph *glyph_for(uint32_t cp)
{
    return (const struct font_glyph *)bsearch(&cp, FONT_GLYPHS, FONT_GLYPH_COUNT,
                                              sizeof(FONT_GLYPHS[0]), glyph_cmp);
}

static uint32_t utf8_next(const char **sp)
{
    const unsigned char *s = (const unsigned char *)*sp;
    if (!*s) return 0;
    uint32_t cp;
    int extra;
    if (s[0] < 0x80) { cp = s[0]; extra = 0; }
    else if ((s[0] & 0xE0) == 0xC0) { cp = s[0] & 0x1F; extra = 1; }
    else if ((s[0] & 0xF0) == 0xE0) { cp = s[0] & 0x0F; extra = 2; }
    else if ((s[0] & 0xF8) == 0xF0) { cp = s[0] & 0x07; extra = 3; }
    else { *sp += 1; return '?'; }
    for (int i = 1; i <= extra; i++) {
        if ((s[i] & 0xC0) != 0x80) { *sp += 1; return '?'; }
        cp = (cp << 6) | (uint32_t)(s[i] & 0x3F);
    }
    *sp += extra + 1;
    return cp;
}

int gfx_text_h(int scale) { return FONT_HEIGHT * (scale < 1 ? 1 : scale); }

int gfx_text_w(const char *s, int scale)
{
    if (scale < 1) scale = 1;
    int w = 0;
    while (*s) {
        uint32_t cp = utf8_next(&s);
        const struct font_glyph *g = glyph_for(cp);
        w += (g ? g->adv : FONT_HEIGHT / 2) * scale;
    }
    return w;
}

void gfx_text_a(gfx_t *g, int x, int y, const char *s, uint32_t c, int alpha, int scale)
{
    if (alpha <= 0) return;
    if (scale < 1) scale = 1;
    int cx = x;
    while (*s) {
        uint32_t cp = utf8_next(&s);
        const struct font_glyph *gl = glyph_for(cp);
        if (!gl) continue;
        if (gl->w == 0 || gl->h == 0) { cx += gl->adv * scale; continue; }
        for (int j = 0; j < gl->h; j++) {
            int py = y + j * scale;
            if (py < g->cy0 - scale || py >= g->cy1 + scale) continue;
            const unsigned char *row = FONT_BITS + gl->off + (size_t)j * gl->w;
            for (int i = 0; i < gl->w; i++) {
                unsigned a = row[i];
                if (!a) continue;
                int px = cx + i * scale;
                if (px < g->cx0 - scale || px >= g->cx1 + scale) continue;
                int aa = (int)a * alpha / 255;
                if (scale == 1) {
                    gfx_px_a(g, px, py, c, aa);
                } else {
                    for (int sy = 0; sy < scale; sy++)
                        for (int sx = 0; sx < scale; sx++)
                            gfx_px_a(g, px + sx, py + sy, c, aa);
                }
            }
        }
        cx += gl->adv * scale;
    }
}

/* ---------------- 任意字号 ---------------- */

static int px_span(int v, int px)
{
    int r = (v * px + FONT_HEIGHT / 2) / FONT_HEIGHT;
    return r < 1 ? 1 : r;
}

static void glyph_px(gfx_t *g, const struct font_glyph *gl, int x, int y,
                     uint32_t c, int alpha, int dw, int dh)
{
    const unsigned char *bits = FONT_BITS + gl->off;
    int sxq = (gl->w << 16) / dw;
    int syq = (gl->h << 16) / dh;

    for (int dy = 0; dy < dh; dy++) {
        int py = y + dy;
        if (py < g->cy0 || py >= g->cy1) continue;
        int fy0 = dy * syq, fy1 = fy0 + syq;
        int y0 = fy0 >> 16, y1 = (fy1 + 65535) >> 16;
        if (y1 > gl->h) y1 = gl->h;
        for (int dx = 0; dx < dw; dx++) {
            int px = x + dx;
            if (px < g->cx0 || px >= g->cx1) continue;
            int fx0 = dx * sxq, fx1 = fx0 + sxq;
            int x0 = fx0 >> 16, x1 = (fx1 + 65535) >> 16;
            if (x1 > gl->w) x1 = gl->w;
            int acc = 0, area = 0;
            for (int yy = y0; yy < y1; yy++) {
                int wy = (fy1 < ((yy + 1) << 16) ? fy1 : ((yy + 1) << 16)) -
                         (fy0 > (yy << 16) ? fy0 : (yy << 16));
                if (wy <= 0) continue;
                const unsigned char *row = bits + (size_t)yy * gl->w;
                for (int xx = x0; xx < x1; xx++) {
                    int wx = (fx1 < ((xx + 1) << 16) ? fx1 : ((xx + 1) << 16)) -
                             (fx0 > (xx << 16) ? fx0 : (xx << 16));
                    if (wx <= 0) continue;
                    int w = (wy >> 8) * (wx >> 8);
                    acc += row[xx] * w;
                    area += w;
                }
            }
            if (!area) continue;
            int a = acc / area;
            if (a <= 0) continue;
            gfx_px_a(g, px, py, c, a * alpha / 255);
        }
    }
}

void gfx_text_px_a(gfx_t *g, int x, int y, const char *s, uint32_t c, int alpha, int px)
{
    if (alpha <= 0 || px <= 0) return;
    if (px > 120) px = 120;
    int cx = x;
    while (*s) {
        uint32_t cp = utf8_next(&s);
        const struct font_glyph *gl = glyph_for(cp);
        if (!gl) continue;
        int dw = px_span(gl->w, px), dh = px_span(gl->h, px);
        if (gl->w && gl->h) glyph_px(g, gl, cx, y, c, alpha, dw, dh);
        cx += px_span(gl->adv, px);
    }
}

void gfx_text_px(gfx_t *g, int x, int y, const char *s, uint32_t c, int px)
{
    gfx_text_px_a(g, x, y, s, c, 255, px);
}

int gfx_text_w_px(const char *s, int px)
{
    int w = 0;
    while (*s) {
        uint32_t cp = utf8_next(&s);
        const struct font_glyph *g = glyph_for(cp);
        int adv = g ? g->adv : FONT_HEIGHT / 2;
        w += px_span(adv, px);
    }
    return w;
}

void gfx_text_center_px(gfx_t *g, int cx, int y, const char *s, uint32_t c, int px)
{
    gfx_text_px(g, cx - gfx_text_w_px(s, px) / 2, y, s, c, px);
}

void gfx_text_right_px(gfx_t *g, int rx, int y, const char *s, uint32_t c, int px)
{
    gfx_text_px(g, rx - gfx_text_w_px(s, px), y, s, c, px);
}

void gfx_text_fit_px(gfx_t *g, int x, int y, int maxw, const char *s, uint32_t c, int px)
{
    if (gfx_text_w_px(s, px) <= maxw) { gfx_text_px(g, x, y, s, c, px); return; }
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", s);
    size_t len = strlen(buf);
    while (len > 0) {
        while (len > 0 && ((unsigned char)buf[len] & 0xC0) == 0x80) len--;
        if (len == 0) break;
        buf[len] = '\0';
        if (gfx_text_w_px(buf, px) + gfx_text_w_px("…", px) <= maxw) break;
        len--;
    }
    buf[len] = '\0';
    gfx_text_px(g, x, y, buf, c, px);
    gfx_text_px(g, x + gfx_text_w_px(buf, px), y, "…", c, px);
}

int gfx_text_wrap_px(gfx_t *g, int x, int y, int maxw, const char *s, uint32_t c,
                     int px, int line_gap)
{
    int line_h = px + line_gap;
    int lines = 0;
    const char *p = s;
    while (*p) {
        const char *line_start = p;
        const char *last_break = NULL;
        int w = 0;
        const char *q = p;
        while (*q) {
            const char *before = q;
            uint32_t cp = utf8_next(&q);
            if (cp == '\n') { last_break = q; break; }
            const struct font_glyph *gl = glyph_for(cp);
            int adv = px_span(gl ? gl->adv : FONT_HEIGHT / 2, px);
            if (w + adv > maxw && q > line_start) { p = last_break ? last_break : before; break; }
            w += adv;
            if (cp == ' ' || cp == '-' || cp == '/') last_break = q;
        }
        if (!*q) p = q;
        int len = (int)(p - line_start);
        char tmp[512];
        if (len > (int)sizeof(tmp) - 1) len = (int)sizeof(tmp) - 1;
        memcpy(tmp, line_start, (size_t)len);
        tmp[len] = '\0';
        if (len > 0 && tmp[len - 1] == '\n') tmp[len - 1] = '\0';
        gfx_text_px(g, x, y + lines * line_h, tmp, c, px);
        lines++;
        while (*p == '\n') p++;
        if (lines > 64) break;
    }
    return lines * line_h;
}

void gfx_text(gfx_t *g, int x, int y, const char *s, uint32_t c, int scale)
{
    gfx_text_a(g, x, y, s, c, 255, scale);
}

void gfx_text_center(gfx_t *g, int cx, int y, const char *s, uint32_t c, int scale)
{
    gfx_text(g, cx - gfx_text_w(s, scale) / 2, y, s, c, scale);
}

void gfx_text_right(gfx_t *g, int rx, int y, const char *s, uint32_t c, int scale)
{
    gfx_text(g, rx - gfx_text_w(s, scale), y, s, c, scale);
}

void gfx_text_fit(gfx_t *g, int x, int y, int maxw, const char *s, uint32_t c, int scale)
{
    if (gfx_text_w(s, scale) <= maxw) { gfx_text(g, x, y, s, c, scale); return; }
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", s);
    size_t len = strlen(buf);
    /* 逐字符回退，注意不要切断 UTF-8 序列 */
    while (len > 0) {
        while (len > 0 && ((unsigned char)buf[len] & 0xC0) == 0x80) len--;
        if (len == 0) break;
        buf[len] = '\0';
        if (gfx_text_w(buf, scale) + gfx_text_w("…", scale) <= maxw) break;
        len--;
    }
    buf[len] = '\0';
    gfx_text(g, x, y, buf, c, scale);
    gfx_text(g, x + gfx_text_w(buf, scale), y, "…", c, scale);
}

int gfx_text_wrap(gfx_t *g, int x, int y, int maxw, const char *s, uint32_t c,
                  int scale, int line_gap)
{
    int line_h = gfx_text_h(scale) + line_gap;
    int lines = 0;
    const char *p = s;
    while (*p) {
        const char *line_start = p;
        const char *last_break = NULL;
        int w = 0;
        const char *q = p;
        while (*q) {
            const char *before = q;
            uint32_t cp = utf8_next(&q);
            if (cp == '\n') { last_break = q; break; }
            const struct font_glyph *gl = glyph_for(cp);
            int adv = (gl ? gl->adv : FONT_HEIGHT / 2) * scale;
            if (w + adv > maxw && q > line_start) { p = last_break ? last_break : before; break; }
            w += adv;
            if (cp == ' ' || cp == '-' || cp == '/') last_break = q;
        }
        if (!*q) p = q;
        /* 画一行 */
        int len = (int)(p - line_start);
        char tmp[512];
        if (len > (int)sizeof(tmp) - 1) len = (int)sizeof(tmp) - 1;
        memcpy(tmp, line_start, (size_t)len);
        tmp[len] = '\0';
        if (len > 0 && tmp[len - 1] == '\n') tmp[len - 1] = '\0';
        gfx_text(g, x, y + lines * line_h, tmp, c, scale);
        lines++;
        while (*p == '\n') p++;
        if (lines > 64) break;
    }
    return lines * line_h;
}

/* ================= 工具 ================= */

uint32_t gfx_mix(uint32_t a, uint32_t b, int t)
{
    if (t <= 0) return a;
    if (t >= 255) return b;
    int ar = (int)((a >> 16) & 0xff), ag = (int)((a >> 8) & 0xff), ab = (int)(a & 0xff);
    int br = (int)((b >> 16) & 0xff), bg = (int)((b >> 8) & 0xff), bb = (int)(b & 0xff);
    int r = (ar * (255 - t) + br * t) / 255;
    int g = (ag * (255 - t) + bg * t) / 255;
    int bl = (ab * (255 - t) + bb * t) / 255;
    return (uint32_t)((r << 16) | (g << 8) | bl);
}

uint32_t gfx_lighten(uint32_t c, int amt)
{
    return gfx_mix(c, 0xFFFFFF, amt);
}

uint32_t gfx_darken(uint32_t c, int amt)
{
    return gfx_mix(c, 0x000000, amt);
}

int gfx_headless(void)
{
    return 0;
}

/* 导出 24 位 BMP，便于在开发机上直接检查版式 */
int gfx_save_bmp(gfx_t *g, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    int row_bytes = g->w * 3;
    int pad = (4 - (row_bytes % 4)) % 4;
    int data_size = (row_bytes + pad) * g->h;
    unsigned char hdr[54] = { 'B', 'M' };
    uint32_t file_size = 54 + (uint32_t)data_size;
    memcpy(hdr + 2, &file_size, 4);
    uint32_t off = 54; memcpy(hdr + 10, &off, 4);
    uint32_t ih = 40; memcpy(hdr + 14, &ih, 4);
    int32_t w = g->w, h = g->h;
    memcpy(hdr + 18, &w, 4); memcpy(hdr + 22, &h, 4);
    uint16_t planes = 1, bpp = 24;
    memcpy(hdr + 26, &planes, 2); memcpy(hdr + 28, &bpp, 2);
    memcpy(hdr + 34, &data_size, 4);
    fwrite(hdr, 1, 54, f);
    unsigned char zero[3] = { 0, 0, 0 };
    for (int y = g->h - 1; y >= 0; y--) {
        for (int x = 0; x < g->w; x++) {
            uint32_t c = g->buf[(size_t)y * g->w + x];
            unsigned char px[3] = { (unsigned char)(c & 0xff), (unsigned char)((c >> 8) & 0xff), (unsigned char)((c >> 16) & 0xff) };
            fwrite(px, 1, 3, f);
        }
        if (pad) fwrite(zero, 1, (size_t)pad, f);
    }
    fclose(f);
    return 0;
}
