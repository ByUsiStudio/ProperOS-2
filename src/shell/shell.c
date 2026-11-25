/* ProperOS 2.5 Mobile · 启动器（properos-shell）
 *
 * 直接绘制 /dev/fb0，无 X/Wayland 依赖；支持触摸与键盘导航。
 * 布局：状态栏 / 时钟 / 应用网格 / 提示条 / 关于面板
 *
 * 开发者：ByUsi Studio · 北啊呢
 */
#define _GNU_SOURCE
#include "../common/properos.h"
#include "font_atlas.h"
#include <linux/fb.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <linux/vt.h>
#include <sys/sysmacros.h>

#define MAX_APPS 24

typedef struct {
    char id[64];
    char name[96];
    uint32_t color;
    int builtin;
} app_t;

typedef struct {
    int fd;
    uint8_t *mem;
    size_t size;
    int w, h, bpp, line;
    uint32_t *buf;        /* 32 位后缓冲 */
} screen_t;

static screen_t g_scr;
static app_t g_apps[MAX_APPS];
static int g_napps;
static int g_focus;
static char g_toast[160];
static time_t g_toast_until;
static int g_about;
static volatile sig_atomic_t g_quit;

static const uint32_t C_BG_TOP    = 0x0B1020;
static const uint32_t C_BG_BOT    = 0x241046;
static const uint32_t C_TEXT      = 0xF2F4FF;
static const uint32_t C_TEXT_DIM  = 0x8C93B5;
static const uint32_t C_ACCENT    = 0x3B82F6;
static const uint32_t C_CARD      = 0x171C33;

/* ---------------- 绘制 ---------------- */

static inline void put_px(int x, int y, uint32_t c)
{
    if (x < 0 || y < 0 || x >= g_scr.w || y >= g_scr.h) return;
    g_scr.buf[(size_t)y * g_scr.w + x] = c;
}

static uint32_t blend(uint32_t dst, uint32_t src, int a)
{
    int dr = (dst >> 16) & 0xff, dg = (dst >> 8) & 0xff, db = dst & 0xff;
    int sr = (src >> 16) & 0xff, sg = (src >> 8) & 0xff, sb = src & 0xff;
    int r = (sr * a + dr * (255 - a)) / 255;
    int g = (sg * a + dg * (255 - a)) / 255;
    int b = (sb * a + db * (255 - a)) / 255;
    return (uint32_t)((r << 16) | (g << 8) | b);
}

static void fill_rect(int x, int y, int w, int h, uint32_t c)
{
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++)
            put_px(i, j, c);
}

static void fill_circle(int cx, int cy, int r, uint32_t c)
{
    for (int j = -r; j <= r; j++)
        for (int i = -r; i <= r; i++)
            if (i * i + j * j <= r * r) put_px(cx + i, cy + j, c);
}

/* 圆角矩形：四角按像素中心到圆心的距离裁切 */
static void round_rect(int x, int y, int w, int h, int r, uint32_t c)
{
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    if (r < 0) r = 0;
    fill_rect(x + r, y, w - 2 * r, h, c);
    fill_rect(x, y + r, r, h - 2 * r, c);
    fill_rect(x + w - r, y + r, r, h - 2 * r, c);

    int rr = r * r;
    for (int j = 0; j < r; j++) {
        for (int i = 0; i < r; i++) {
            int dx = r - i, dy = r - j;         /* 圆心在 (r,r) 内侧 */
            if ((dx * dx + dy * dy) > rr) continue;
            put_px(x + i, y + j, c);
            put_px(x + w - 1 - i, y + j, c);
            put_px(x + i, y + h - 1 - j, c);
            put_px(x + w - 1 - i, y + h - 1 - j, c);
        }
    }
}

static void gradient_bg(void)
{
    for (int y = 0; y < g_scr.h; y++) {
        int t = y * 255 / (g_scr.h > 1 ? g_scr.h - 1 : 1);
        int r = ((C_BG_TOP >> 16 & 0xff) * (255 - t) + (C_BG_BOT >> 16 & 0xff) * t) / 255;
        int g = ((C_BG_TOP >> 8 & 0xff) * (255 - t) + (C_BG_BOT >> 8 & 0xff) * t) / 255;
        int b = ((C_BG_TOP & 0xff) * (255 - t) + (C_BG_BOT & 0xff) * t) / 255;
        uint32_t c = (uint32_t)((r << 16) | (g << 8) | b);
        for (int x = 0; x < g_scr.w; x++) g_scr.buf[(size_t)y * g_scr.w + x] = c;
    }
    /* 右上角光晕 */
    int cx = g_scr.w - g_scr.w / 6, cy = g_scr.h / 8, rad = g_scr.w / 3;
    for (int j = -rad; j <= rad; j++) {
        for (int i = -rad; i <= rad; i++) {
            int d2 = i * i + j * j;
            if (d2 > rad * rad) continue;
            int x = cx + i, y = cy + j;
            if (x < 0 || y < 0 || x >= g_scr.w || y >= g_scr.h) continue;
            int a = 26 * (rad * rad - d2) / (rad * rad);
            size_t idx = (size_t)y * g_scr.w + x;
            g_scr.buf[idx] = blend(g_scr.buf[idx], 0x5B4BFF, a);
        }
    }
}

/* ---------------- 文本 ---------------- */

static const struct font_glyph *glyph_for(uint32_t cp)
{
    for (unsigned i = 0; i < FONT_GLYPH_COUNT; i++)
        if (FONT_GLYPHS[i].cp == cp) return &FONT_GLYPHS[i];
    return NULL;
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
        cp = (cp << 6) | (s[i] & 0x3F);
    }
    *sp += extra + 1;
    return cp;
}

static int text_width(const char *s, int scale)
{
    int w = 0;
    while (*s) {
        uint32_t cp = utf8_next(&s);
        const struct font_glyph *g = glyph_for(cp);
        w += (g ? g->adv : FONT_HEIGHT / 2) * scale;
    }
    return w;
}

static void draw_text(int x, int y, const char *s, uint32_t color, int scale)
{
    int cx = x;
    while (*s) {
        uint32_t cp = utf8_next(&s);
        const struct font_glyph *g = glyph_for(cp);
        if (!g) continue;
        int stride = (g->w + 7) / 8;
        for (int j = 0; j < g->h; j++) {
            for (int i = 0; i < g->w; i++) {
                unsigned char b = FONT_BITS[g->off + j * stride + i / 8];
                if (!(b & (0x80 >> (i % 8)))) continue;
                for (int sy = 0; sy < scale; sy++)
                    for (int sx = 0; sx < scale; sx++)
                        put_px(cx + i * scale + sx, y + j * scale + sy, color);
            }
        }
        cx += g->adv * scale;
    }
}

static void draw_text_center(int cx, int y, const char *s, uint32_t color, int scale)
{
    draw_text(cx - text_width(s, scale) / 2, y, s, color, scale);
}

/* 超出宽度时截断并加省略号 */
static void draw_text_ellipsis(int x, int y, int maxw, const char *s, uint32_t color, int scale)
{
    int w = text_width(s, scale);
    if (w <= maxw) { draw_text(x, y, s, color, scale); return; }
    char buf[128];
    snprintf(buf, sizeof(buf), "%s", s);
    size_t len = strlen(buf);
    while (len > 3) {
        buf[len - 1] = '\0';
        if (text_width(buf, scale) + text_width("…", scale) <= maxw) break;
        len--;
        /* 避免切断 UTF-8 序列 */
        while (len > 0 && ((unsigned char)buf[len] & 0xC0) == 0x80) buf[--len] = '\0';
    }
    draw_text(x, y, buf, color, scale);
    draw_text(x + text_width(buf, scale), y, "…", color, scale);
}

/* ---------------- framebuffer ---------------- */

static int screen_open(void)
{
    const char *dev = getenv("PROPEROS_FB") ? getenv("PROPEROS_FB") : "/dev/fb0";
    g_scr.fd = open(dev, O_RDWR);
    if (g_scr.fd < 0) return -1;

    struct fb_var_screeninfo vi;
    struct fb_fix_screeninfo fi;
    if (ioctl(g_scr.fd, FBIOGET_VSCREENINFO, &vi) < 0 || ioctl(g_scr.fd, FBIOGET_FSCREENINFO, &fi) < 0)
        return -1;

    g_scr.w = (int)vi.xres;
    g_scr.h = (int)vi.yres;
    g_scr.bpp = (int)vi.bits_per_pixel;
    g_scr.line = (int)fi.line_length;
    g_scr.size = (size_t)fi.smem_len ? (size_t)fi.smem_len : (size_t)g_scr.line * g_scr.h;

    g_scr.mem = mmap(NULL, g_scr.size, PROT_READ | PROT_WRITE, MAP_SHARED, g_scr.fd, 0);
    if (g_scr.mem == MAP_FAILED) return -1;

    g_scr.buf = xcalloc((size_t)g_scr.w * g_scr.h, sizeof(uint32_t));
    return 0;
}

static void screen_flush(void)
{
    if (g_scr.bpp == 32) {
        for (int y = 0; y < g_scr.h; y++)
            memcpy(g_scr.mem + (size_t)y * g_scr.line, g_scr.buf + (size_t)y * g_scr.w,
                   (size_t)g_scr.w * 4);
    } else if (g_scr.bpp == 16) {
        for (int y = 0; y < g_scr.h; y++) {
            uint16_t *dst = (uint16_t *)(g_scr.mem + (size_t)y * g_scr.line);
            uint32_t *src = g_scr.buf + (size_t)y * g_scr.w;
            for (int x = 0; x < g_scr.w; x++) {
                uint32_t c = src[x];
                dst[x] = (uint16_t)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F));
            }
        }
    }
}

/* ---------------- 应用清单 ---------------- */

static uint32_t color_from_id(const char *id)
{
    static const uint32_t palette[] = {
        0x3B82F6, 0x8B5CF6, 0xEC4899, 0xF59E0B, 0x10B981, 0x06B6D4, 0xEF4444, 0x6366F1,
    };
    uint32_t h = 2166136261u;
    for (const char *p = id; *p; p++) { h ^= (unsigned char)*p; h *= 16777619u; }
    return palette[h % ARRAY_LEN(palette)];
}

static void add_builtin(const char *id, const char *name, uint32_t color)
{
    if (g_napps >= MAX_APPS) return;
    app_t *a = &g_apps[g_napps++];
    snprintf(a->id, sizeof(a->id), "%s", id);
    snprintf(a->name, sizeof(a->name), "%s", name);
    a->color = color;
    a->builtin = 1;
}

static void scan_apps(void)
{
    g_napps = 0;
    add_builtin("__terminal", "终端", 0x111827);
    add_builtin("__files", "文件", 0xF59E0B);
    add_builtin("__settings", "设置", 0x64748B);
    add_builtin("__about", "关于", 0x8B5CF6);

    DIR *d = opendir("/data/apps");
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) && g_napps < MAX_APPS) {
            if (de->d_name[0] == '.') continue;
            char p[PATH_MAX_LEN];
            snprintf(p, sizeof(p), "/data/apps/%s/manifest.conf", de->d_name);
            kvlist_t kv;
            if (kv_load(&kv, p) < 0) continue;
            app_t *a = &g_apps[g_napps++];
            snprintf(a->id, sizeof(a->id), "%s", de->d_name);
            snprintf(a->name, sizeof(a->name), "%s", kv_get(&kv, "name", de->d_name));
            a->color = color_from_id(de->d_name);
            a->builtin = 0;
            kv_free(&kv);
        }
        closedir(d);
    }
}

/* ---------------- 交互 ---------------- */

static void toast(const char *msg)
{
    snprintf(g_toast, sizeof(g_toast), "%s", msg);
    g_toast_until = time(NULL) + 3;
}

static void switch_vt(int vt)
{
    int fd = open("/dev/tty0", O_RDWR);
    if (fd < 0) { toast("无法切换到控制台"); return; }
    ioctl(fd, VT_ACTIVATE, vt);
    ioctl(fd, VT_WAITACTIVE, vt);
    close(fd);
}

static void launch(const app_t *a)
{
    if (a->builtin) {
        if (!strcmp(a->id, "__terminal")) { switch_vt(1); toast("已切换到终端 (Ctrl+Alt+F7 返回)"); }
        else if (!strcmp(a->id, "__settings")) { switch_vt(3); toast("已切换到设置控制台 (Ctrl+Alt+F7 返回)"); }
        else if (!strcmp(a->id, "__about")) { g_about = !g_about; }
        else if (!strcmp(a->id, "__files")) {
            run_shell("ls -1 /data > /tmp/files.txt 2>&1");
            toast("/data 目录已列出，见终端");
        }
        return;
    }

    char cmd[PATH_MAX_LEN];
    snprintf(cmd, sizeof(cmd),
             "setsid /usr/bin/papp run '%s' >>/var/log/papp.log 2>&1 &", a->id);
    run_shell(cmd);
    char msg[128];
    snprintf(msg, sizeof(msg), "正在启动 %s", a->name);
    toast(msg);
    info("启动应用 %s", a->id);
}

static int battery_percent(void)
{
    DIR *d = opendir("/sys/class/power_supply");
    if (!d) return -1;
    struct dirent *de;
    int pct = -1;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.') continue;
        char p[PATH_MAX_LEN];
        snprintf(p, sizeof(p), "/sys/class/power_supply/%s/capacity", de->d_name);
        char *v = read_file(p, NULL);
        if (v) { pct = atoi(v); free(v); break; }
    }
    closedir(d);
    return pct;
}

/* ---------------- 布局与绘制 ---------------- */

typedef struct {
    int cols, rows, icon, gap, grid_x, grid_y, cell_w, cell_h;
} layout_t;

static layout_t compute_layout(void)
{
    layout_t L;
    int pad = g_scr.w / 20;
    L.cols = 4;
    if (g_scr.w < 600) L.cols = 3;
    L.gap = pad / 2;
    L.icon = (g_scr.w - pad * 2 - L.gap * (L.cols - 1)) / L.cols;
    int icon_cap = g_scr.h / 7;
    if (L.icon > icon_cap) L.icon = icon_cap;
    if (L.icon > 220) L.icon = 220;
    L.cell_w = L.icon;
    L.cell_h = L.icon + FONT_HEIGHT + 26;
    int total = L.cols * L.icon + (L.cols - 1) * L.gap;
    L.grid_x = (g_scr.w - total) / 2;
    L.grid_y = g_scr.h * 36 / 100;
    L.rows = (g_scr.h - L.grid_y - pad) / L.cell_h;
    if (L.rows < 1) L.rows = 1;
    return L;
}

static void draw_status_bar(void)
{
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    char tbuf[16];
    strftime(tbuf, sizeof(tbuf), "%H:%M", &tm);

    draw_text(g_scr.w / 20, 18, tbuf, C_TEXT, 1);

    const char *brand = "ProperOS 2.5";
    draw_text_center(g_scr.w / 2, 18, brand, C_TEXT_DIM, 1);

    int pct = battery_percent();
    if (pct >= 0) {
        char bat[32];
        snprintf(bat, sizeof(bat), "%d%%", pct);
        int tw = text_width(bat, 1);
        draw_text(g_scr.w - g_scr.w / 20 - tw, 18, bat, C_TEXT, 1);

        int bx = g_scr.w - g_scr.w / 20 - tw - 56, by = 24, bw = 34, bh = 16;
        round_rect(bx, by, bw, bh, 4, C_TEXT_DIM);
        fill_rect(bx + 2, by + 2, bw - 4, bh - 4, C_BG_TOP);
        if (pct > 0) fill_rect(bx + 3, by + 3, (bw - 6) * (pct > 100 ? 100 : pct) / 100, bh - 6, C_TEXT);
    } else {
        const char *s = "AC";
        draw_text(g_scr.w - g_scr.w / 20 - text_width(s, 1), 18, s, C_TEXT_DIM, 1);
    }
}

static void draw_clock(void)
{
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);

    char big[16];
    strftime(big, sizeof(big), "%H:%M", &tm);
    draw_text(g_scr.w / 16, g_scr.h / 16, big, C_TEXT, 3);

    char date[64];
    const char *wd[] = { "周日", "周一", "周二", "周三", "周四", "周五", "周六" };
    snprintf(date, sizeof(date), "%d月%d日  %s", tm.tm_mon + 1, tm.tm_mday, wd[tm.tm_wday]);
    draw_text(g_scr.w / 16, g_scr.h / 16 + FONT_HEIGHT * 3 + 10, date, C_TEXT_DIM, 1);
}

static void draw_apps(void)
{
    layout_t L = compute_layout();
    for (int i = 0; i < g_napps; i++) {
        int r = i / L.cols, c = i % L.cols;
        if (r >= L.rows) break;
        int x = L.grid_x + c * (L.icon + L.gap);
        int y = L.grid_y + r * L.cell_h;

        if (i == g_focus) {
            round_rect(x - 8, y - 8, L.icon + 16, L.icon + 16, 34, 0x2A3350);
        }
        round_rect(x, y, L.icon, L.icon, 28, g_apps[i].color);

        /* 图标内首字 */
        char initial[8] = {0};
        const char *p = g_apps[i].name;
        uint32_t cp = utf8_next(&p);
        if (cp < 0x80) { initial[0] = (char)cp; initial[1] = 0; }
        else {
            int n = (int)(p - g_apps[i].name);
            memcpy(initial, g_apps[i].name, n > 7 ? 7 : (size_t)n);
        }
        int iw = text_width(initial, 2);
        draw_text(x + (L.icon - iw) / 2, y + (L.icon - FONT_HEIGHT * 2) / 2, initial, 0xFFFFFF, 2);

        int label_w = L.icon + L.gap;
        draw_text_ellipsis(x - L.gap / 2, y + L.icon + 10, label_w,
                           g_apps[i].name, C_TEXT, 1);
    }
}

static void draw_toast(void)
{
    if (!g_toast[0] || time(NULL) > g_toast_until) return;
    int tw = text_width(g_toast, 1);
    int w = tw + 64, h = FONT_HEIGHT + 32;
    int x = (g_scr.w - w) / 2, y = g_scr.h - h - 40;
    round_rect(x, y, w, h, 24, 0x0E1224);
    draw_text_center(g_scr.w / 2, y + 16, g_toast, C_TEXT, 1);
}

static void draw_about(void)
{
    int w = g_scr.w * 3 / 4, h = g_scr.h / 2;
    int x = (g_scr.w - w) / 2, y = (g_scr.h - h) / 2;
    round_rect(x, y, w, h, 32, C_CARD);

    int cx = g_scr.w / 2;
    int ty = y + 40;
    draw_text_center(cx, ty, "ProperOS 2.5", C_TEXT, 2); ty += FONT_HEIGHT * 2 + 16;
    draw_text_center(cx, ty, "Mobile", C_ACCENT, 1); ty += FONT_HEIGHT + 8;
    draw_text_center(cx, ty, "ByUsi Studio", C_TEXT, 1); ty += FONT_HEIGHT + 8;
    draw_text_center(cx, ty, "开发者 北啊呢", C_TEXT_DIM, 1); ty += FONT_HEIGHT + 8;
    draw_text_center(cx, ty, "Linux 内核 · 容器运行时", C_TEXT_DIM, 1); ty += FONT_HEIGHT + 16;
    draw_text_center(cx, ty, "点击任意处关闭", C_TEXT_DIM, 1);
}

static void render(void)
{
    gradient_bg();
    draw_status_bar();
    draw_clock();
    draw_apps();
    draw_toast();
    if (g_about) draw_about();
    screen_flush();
}

/* ---------------- 输入 ---------------- */

typedef struct {
    int fd;
    int x, y;
    int down;
} input_t;

static input_t g_in[8];
static int g_nin;

static void input_open(void)
{
    for (int i = 0; i < 8; i++) {
        char p[64];
        snprintf(p, sizeof(p), "/dev/input/event%d", i);
        int fd = open(p, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) break;
        g_in[g_nin].fd = fd;
        g_in[g_nin].x = g_scr.w / 2;
        g_in[g_nin].y = g_scr.h / 2;
        g_in[g_nin].down = 0;
        g_nin++;
    }
}

static void handle_tap(int x, int y)
{
    if (g_about) { g_about = 0; return; }
    layout_t L = compute_layout();
    for (int i = 0; i < g_napps; i++) {
        int r = i / L.cols, c = i % L.cols;
        int ax = L.grid_x + c * (L.icon + L.gap);
        int ay = L.grid_y + r * L.cell_h;
        if (x >= ax - L.gap / 2 && x <= ax + L.icon + L.gap / 2 &&
            y >= ay - 10 && y <= ay + L.cell_h) {
            g_focus = i;
            launch(&g_apps[i]);
            return;
        }
    }
}

static void handle_key(int code)
{
    layout_t L = compute_layout();
    if (g_napps <= 0) return;
    if (g_about) { g_about = 0; return; }
    if (code == KEY_RIGHT) g_focus = (g_focus + 1) % g_napps;
    else if (code == KEY_LEFT) g_focus = (g_focus + g_napps - 1) % g_napps;
    else if (code == KEY_DOWN) g_focus = (g_focus + L.cols) % g_napps;
    else if (code == KEY_UP) g_focus = (g_focus + g_napps - L.cols) % g_napps;
    else if (code == KEY_ENTER || code == KEY_KPENTER) launch(&g_apps[g_focus]);
    else if (code == KEY_ESC || code == KEY_BACK) { g_about = 0; }
    (void)L;
}

static void input_poll(void)
{
    struct input_event ev;
    for (int i = 0; i < g_nin; i++) {
        while (read(g_in[i].fd, &ev, sizeof(ev)) == sizeof(ev)) {
            if (ev.type == EV_ABS) {
                if (ev.code == ABS_X || ev.code == ABS_MT_POSITION_X) g_in[i].x = ev.value;
                else if (ev.code == ABS_Y || ev.code == ABS_MT_POSITION_Y) g_in[i].y = ev.value;
            } else if (ev.type == EV_KEY) {
                if (ev.code == BTN_TOUCH || ev.code == BTN_LEFT) {
                    if (ev.value) { g_in[i].down = 1; }
                    else if (g_in[i].down) {
                        g_in[i].down = 0;
                        handle_tap(g_in[i].x, g_in[i].y);
                    }
                } else if (ev.value == 1) {
                    handle_key(ev.code);
                }
            }
        }
    }
}

static void on_signal(int sig) { (void)sig; g_quit = 1; }

int main(int argc, char **argv)
{
    plog_open("properos-shell");

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--version")) {
            printf("properos-shell %s (%s · %s)\n", PROPEROS_VERSION, PROPEROS_VENDOR, PROPEROS_AUTHOR);
            return 0;
        }
        if (!strcmp(argv[i], "--help")) {
            printf("用法: properos-shell [--info|--list|--version]\n");
            return 0;
        }
    }

    scan_apps();

    if (argc > 1 && !strcmp(argv[1], "--list")) {
        for (int i = 0; i < g_napps; i++)
            printf("%-24s %s%s\n", g_apps[i].id, g_apps[i].name, g_apps[i].builtin ? " [内置]" : "");
        return 0;
    }

    if (screen_open() < 0) {
        if (argc > 1 && !strcmp(argv[1], "--info"))
            printf("framebuffer 不可用（无 /dev/fb0 或 ioctl 失败）\n");
        else
            fprintf(stderr, "properos-shell: 无法打开 framebuffer\n");
        return 2;
    }

    if (argc > 1 && !strcmp(argv[1], "--info")) {
        printf("分辨率: %dx%d  %d bpp  行宽 %d  应用 %d 个\n",
               g_scr.w, g_scr.h, g_scr.bpp, g_scr.line, g_napps);
        return 0;
    }

    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);

    input_open();
    info("启动器就绪 %dx%d，应用 %d 个", g_scr.w, g_scr.h, g_napps);

    int need_render = 1;
    time_t last_min = 0;
    while (!g_quit) {
        if (need_render) { render(); need_render = 0; }

        fd_set fds;
        FD_ZERO(&fds);
        int maxfd = -1;
        for (int i = 0; i < g_nin; i++) { FD_SET(g_in[i].fd, &fds); if (g_in[i].fd > maxfd) maxfd = g_in[i].fd; }
        struct timeval tv = { 1, 0 };
        select(maxfd + 1, &fds, NULL, NULL, &tv);

        input_poll();

        time_t now = time(NULL);
        struct tm tm;
        localtime_r(&now, &tm);
        if (tm.tm_min != last_min) { last_min = tm.tm_min; need_render = 1; }

        if (g_toast[0] && now <= g_toast_until) need_render = 1;
        else if (g_toast[0] && now > g_toast_until) { g_toast[0] = '\0'; need_render = 1; }
    }

    info("启动器退出");
    return 0;
}
