/* ProperOS 25.0.0 Mobile · 桌面外壳（properos-shell）
 *
 * 设计参考 KDE Plasma Mobile，但保留普通 Linux 桌面的结构：
 *   顶部面板  —— 时钟、状态图标、快捷设置入口
 *   主屏      —— 壁纸 + 分页图标网格（虚拟桌面）
 *   底部 Dock —— 应用抽屉、固定应用、最近任务
 *   应用抽屉  —— 全屏网格 + 搜索
 *   快捷设置  —— 下拉面板：亮度/音量、磁贴、通知
 *
 * 应用全部来自 /opt/properos/apps 下的 ppkg 安装目录，
 * 由 properos-app 运行；桌面本身只负责壳层与窗口切换。
 *
 * 触摸：跟手翻页、边缘返回、下拉/上滑手势、长按菜单、按下反馈。
 *
 * 开发者：ByUsi Studio · 北啊呢
 */
#define _GNU_SOURCE
#include "gfx.h"
#include "ui.h"
#include "input.h"
#include "json.h"
#include "zip.h"

#include <dirent.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX_APPS 96
#define MAX_PAGES 8
#define DOCK_SLOTS 5

/* 字号不再用固定像素：layout() 按屏幕密度算出 D.fs_* */

typedef struct {
    char id[96];
    char name[64];
    char icon[32];
    char path[288];
    uint32_t color;
    int kind;          /* 0 = ppkg 目录, 1 = papp */
} app_t;

typedef enum { SC_HOME = 0, SC_DRAWER, SC_QS, SC_MENU } screen_t;

typedef struct {
    gfx_t g;
    ui_t u;
    input_t in;
    int dark;

    app_t apps[MAX_APPS];
    int napps;
    int per_page, npages;
    int page;
    int dock[DOCK_SLOTS];
    int ndock;

    screen_t screen;
    float drawer_p;      /* 0..1 抽屉展开进度 */
    float qs_p;          /* 0..1 快捷设置展开进度 */
    float page_ofs;      /* 翻页跟手位移（px） */
    int page_drag;
    int drag_x0, drag_y0;
    int drag_kind;       /* 0 无 1 横向 2 纵向 */
    float fling;

    int drawer_page;
    int search_focus;
    char query[64];
    int menu_app;
    char toast[128];
    int64_t toast_until;

    char wallpaper[24];
    int brightness, volume;
    int wifi_on, bt_on, dnd_on, airplane, flash, rotate, saver, hotspot;

    int W, H;
    int status_h, dock_h, grid_top, grid_bottom;
    int cx_pad, cell_w, cell_h, icon, cols, rows;
    int fs_label, fs_status, dock_isz, label_gap;
    float dn;   /* 密度：1dp 对应像素数（720p 手机 ≈ 2.0） */
    int fs_micro, fs_tiny, fs_small, fs_body, fs_title, fs_h1;
    uint32_t *wall;      /* 壁纸缓存，避免每帧重算渐变与柔光 */
    int wall_w, wall_h;

    int quit;
    int dirty;
} desk_t;

static desk_t D;
static volatile sig_atomic_t g_quit;

/* ---------------- 基础 ---------------- */

static int64_t mono_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

static void toast(const char *msg)
{
    snprintf(D.toast, sizeof(D.toast), "%s", msg ? msg : "");
    D.toast_until = mono_ms() + 1800;
}

static void mkdir_p(const char *path)
{
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') { *p = '\0'; mkdir(tmp, 0755); *p = '/'; }
    }
    mkdir(tmp, 0755);
}

static char *read_text(const char *path, size_t *len)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return NULL;
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size > (2 << 20)) { close(fd); return NULL; }
    char *buf = malloc((size_t)st.st_size + 1);
    if (!buf) { close(fd); return NULL; }
    ssize_t got = 0;
    while (got < st.st_size) {
        ssize_t r = read(fd, buf + got, (size_t)(st.st_size - got));
        if (r <= 0) break;
        got += r;
    }
    close(fd);
    buf[got] = '\0';
    if (len) *len = (size_t)got;
    return buf;
}

static int conf_get(const char *key, char *out, size_t n)
{
    size_t len = 0;
    char *txt = read_text("/data/home/.config/properos/system.conf", &len);
    if (!txt) return 0;
    int found = 0;
    char *save = NULL;
    for (char *ln = strtok_r(txt, "\n", &save); ln; ln = strtok_r(NULL, "\n", &save)) {
        char *eq = strchr(ln, '=');
        if (!eq) continue;
        *eq = '\0';
        if (!strcmp(ln, key)) {
            snprintf(out, n, "%s", eq + 1);
            found = 1;
            break;
        }
    }
    free(txt);
    return found;
}

static int conf_int(const char *key, int def)
{
    char v[32];
    if (!conf_get(key, v, sizeof(v))) return def;
    return atoi(v);
}

/* ---------------- 应用扫描 ---------------- */

/* 宽松 JSON 取值：同时接受 "k": "v" 与 hjson 的 k: "v" */
static int json_lite(const char *txt, const char *key, char *out, size_t n)
{
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(txt, pat);
    if (!p) {
        snprintf(pat, sizeof(pat), "%s:", key);
        p = strstr(txt, pat);
        if (!p) return 0;
        p += strlen(pat);
    } else {
        p += strlen(pat);
        while (*p == ' ' || *p == '\t') p++;
        if (*p != ':') return 0;
        p++;
    }
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p == '"') {
        p++;
        size_t i = 0;
        while (*p && *p != '"' && i + 1 < n) {
            if (*p == '\\' && p[1]) p++;
            out[i++] = *p++;
        }
        out[i] = '\0';
        return 1;
    }
    size_t i = 0;
    while (*p && *p != ',' && *p != '\n' && *p != '}' && i + 1 < n) out[i++] = *p++;
    out[i] = '\0';
    return i > 0;
}

static uint32_t hash_color(const char *id)
{
    static const uint32_t pal[10] = {
        0x3E7BFF, 0xE95420, 0x2FA84F, 0x9B59B6, 0xE8A33D,
        0x16A3B8, 0xD94F70, 0x5B6CF0, 0x3E9E6E, 0xC0562E
    };
    unsigned h = 5381;
    for (const char *p = id; *p; p++) h = h * 33 + (unsigned char)*p;
    return pal[h % 10];
}

static void add_app(const char *id, const char *name, const char *icon,
                    uint32_t color, const char *path, int kind)
{
    if (D.napps >= MAX_APPS) return;
    for (int i = 0; i < D.napps; i++)
        if (!strcmp(D.apps[i].id, id)) return;
    app_t *a = &D.apps[D.napps++];
    snprintf(a->id, sizeof(a->id), "%s", id);
    snprintf(a->name, sizeof(a->name), "%s", name);
    snprintf(a->icon, sizeof(a->icon), "%s", icon);
    snprintf(a->path, sizeof(a->path), "%s", path);
    a->color = color;
    a->kind = kind;
}

static int read_jspk_manifest(const char *path, char *id, size_t idn,
                              char *name, size_t nn, char *icon, size_t icn,
                              char *color, size_t cn)
{
    zip_t z;
    if (zip_open_file(&z, path) != 0) return -1;
    const zip_ent_t *e = zip_find(&z, "manifest.json");
    uint8_t *out = NULL;
    size_t len = 0;
    if (!e || zip_read(&z, e, &out, &len) != 0) { zip_close(&z); return -1; }
    char *txt = (char *)out;
    int ok = json_lite(txt, "id", id, idn) && json_lite(txt, "name", name, nn);
    json_lite(txt, "icon", icon, icn);
    json_lite(txt, "color", color, cn);
    free(out);
    zip_close(&z);
    return ok ? 0 : -1;
}

static int cmp_app(const void *a, const void *b)
{
    const app_t *x = a, *y = b;
    return strcmp(x->name, y->name);
}

static void scan_apps(void)
{
    D.napps = 0;

    /* 1) ppkg 安装的应用：/opt/properos/apps/<名字>/manifest.json
     *    25.0.0 起应用以 .ppkg 交付，payload 展开后就是这种目录布局。 */
    const char *pv = getenv("PROPEROS_APPS");
    const char *pkgdirs[] = { pv ? pv : "/opt/properos/apps" };
    for (size_t d = 0; d < sizeof(pkgdirs) / sizeof(pkgdirs[0]); d++) {
        DIR *dp = opendir(pkgdirs[d]);
        if (!dp) continue;
        struct dirent *de;
        while ((de = readdir(dp))) {
            if (de->d_name[0] == '.') continue;
            char full[320], mf[360];
            snprintf(full, sizeof(full), "%s/%s", pkgdirs[d], de->d_name);
            snprintf(mf, sizeof(mf), "%s/manifest.json", full);
            size_t len = 0;
            char *txt = read_text(mf, &len);
            if (!txt) continue;
            char id[96] = "", name[64] = "", icon[32] = "store", cs[16] = "";
            json_lite(txt, "id", id, sizeof(id));
            json_lite(txt, "name", name, sizeof(name));
            json_lite(txt, "icon", icon, sizeof(icon));
            json_lite(txt, "color", cs, sizeof(cs));
            free(txt);
            if (!name[0]) continue;
            if (!id[0]) snprintf(id, sizeof(id), "app:%s", de->d_name);
            uint32_t col = cs[0] == '#' ? (uint32_t)strtoul(cs + 1, NULL, 16) : hash_color(id);
            if (!col) col = hash_color(id);
            add_app(id, name, icon, col, full, 0);
        }
        closedir(dp);
    }

    /* 2) papp 应用（旧的 /data/apps/<pkg>/manifest.conf 布局） */
    DIR *dp = opendir("/data/apps");
    if (dp) {
        struct dirent *de;
        while ((de = readdir(dp))) {
            if (de->d_name[0] == '.') continue;
            char mp[320];
            snprintf(mp, sizeof(mp), "/data/apps/%s/manifest.conf", de->d_name);
            size_t len = 0;
            char *txt = read_text(mp, &len);
            if (!txt) continue;
            char name[64] = "", icon[32] = "store";
            json_lite(txt, "name", name, sizeof(name));
            json_lite(txt, "icon", icon, sizeof(icon));
            free(txt);
            if (!name[0]) continue;
            add_app(de->d_name, name, icon, hash_color(de->d_name), de->d_name, 1);
        }
        closedir(dp);
    }

    qsort(D.apps, (size_t)D.napps, sizeof(app_t), cmp_app);

    /* Dock 固定位：设置、电话、消息、相机、浏览器 */
    const char *want[] = { "settings", "phone", "message", "camera", "browser" };
    D.ndock = 0;
    for (int w = 0; w < DOCK_SLOTS; w++) {
        for (int i = 0; i < D.napps && D.ndock < DOCK_SLOTS; i++) {
            if (strstr(D.apps[i].id, want[w])) {
                int dup = 0;
                for (int k = 0; k < D.ndock; k++) if (D.dock[k] == i) dup = 1;
                if (!dup) D.dock[D.ndock++] = i;
                break;
            }
        }
    }
    for (int i = 0; i < D.napps && D.ndock < DOCK_SLOTS; i++) {
        int dup = 0;
        for (int k = 0; k < D.ndock; k++) if (D.dock[k] == i) dup = 1;
        if (!dup) D.dock[D.ndock++] = i;
    }
}

/* ---------------- 系统状态 ---------------- */

static int battery_percent(void)
{
    DIR *d = opendir("/sys/class/power_supply");
    if (!d) return 87;
    int pct = -1;
    struct dirent *de;
    while ((de = readdir(d))) {
        char p[256];
        snprintf(p, sizeof(p), "/sys/class/power_supply/%s/capacity", de->d_name);
        size_t len = 0;
        char *t = read_text(p, &len);
        if (t) {
            pct = atoi(t);
            free(t);
            if (pct > 0) break;
        }
    }
    closedir(d);
    return pct >= 0 ? pct : 87;
}

static int wifi_up(void)
{
    DIR *d = opendir("/sys/class/net");
    if (!d) return 0;
    int up = 0;
    struct dirent *de;
    while ((de = readdir(d))) {
        if (!strncmp(de->d_name, "wl", 2)) { up = 1; break; }
    }
    closedir(d);
    return up;
}

/* ---------------- 壁纸 ---------------- */

static void draw_wallpaper(void)
{
    int W = D.W, H = D.H;
    uint32_t top = 0x2A3F6E, mid = 0x1B2740, bot = 0x0D1220;
    if (!strcmp(D.wallpaper, "dusk")) { top = 0x3A2A52; mid = 0x221A34; bot = 0x0E0B14; }
    else if (!strcmp(D.wallpaper, "bamboo")) { top = 0x1C4038; mid = 0x122822; bot = 0x080F0D; }

    /* 上下两段各画一半，接缝处同为 mid，避免出现明暗台阶 */
    gfx_vgrad(&D.g, 0, 0, W, H / 2, top, mid);
    gfx_vgrad(&D.g, 0, H / 2, W, H - H / 2, mid, bot);

    /* Plasma 风格的两团柔光 */
    gfx_glow(&D.g, W * 82 / 100, H * 12 / 100, W * 60 / 100, 0x5B8BFF, 44);
    gfx_glow(&D.g, W * 12 / 100, H * 72 / 100, W * 52 / 100, 0x8A6BFF, 26);

    /* 细颗粒噪点，避免大面积渐变出现色带 */
    for (int y = 0; y < H; y += 3) {
        for (int x = (y * 7) % 11; x < W; x += 11) {
            unsigned v = (unsigned)((unsigned)x * 73856093u ^ (unsigned)y * 19349663u);
            if ((v & 7) == 0) gfx_px_a(&D.g, x, y, 0xFFFFFF, 6);
        }
    }
}

/* ---------------- 布局 ---------------- */

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static void layout(void)
{
    int W = D.W, H = D.H;
    int base = W < H ? W : H;          /* 以短边为密度基准 */
    float dn = (float)base / 360.0f;
    if (dn < 1.5f) dn = 1.5f;
    if (dn > 3.2f) dn = 3.2f;
    D.dn = dn;

    D.fs_micro = (int)(11 * dn);
    D.fs_tiny  = (int)(12 * dn);
    D.fs_small = (int)(14 * dn);
    D.fs_body  = (int)(16 * dn);
    D.fs_title = (int)(20 * dn);
    D.fs_h1    = (int)(26 * dn);

    D.status_h = (int)(22 * dn);
    if (D.status_h < 36) D.status_h = 36;
    D.dock_h = (int)(44 * dn);
    if (D.dock_h < 76) D.dock_h = 76;
    D.fs_status = D.fs_small;
    D.fs_label = D.fs_tiny;
    D.label_gap = (int)(4 * dn);

    D.cx_pad = (int)(12 * dn);
    if (D.cx_pad < 12) D.cx_pad = 12;
    D.cols = W >= H ? 6 : 4;
    D.rows = W >= H ? 3 : 4;
    D.per_page = D.cols * D.rows;
    D.npages = (D.napps + D.per_page - 1) / D.per_page;
    if (D.npages < 1) D.npages = 1;
    if (D.npages > MAX_PAGES) D.npages = MAX_PAGES;
    if (D.page >= D.npages) D.page = D.npages - 1;

    D.cell_w = (W - D.cx_pad * 2) / D.cols;
    D.grid_top = D.status_h + (int)(10 * dn);
    D.grid_bottom = H - D.dock_h - (int)(12 * dn);
    int avail = D.grid_bottom - D.grid_top;
    /* 格子高度按 dp 定，不按整屏均分，否则行距被拉得很散 */
    int ch = (int)(88 * dn);
    if (ch * D.rows > avail) ch = avail / D.rows;
    if (ch < 8) ch = 8;
    D.cell_h = ch;

    /* 图标 54dp，受格宽限制 */
    int target = (int)(54 * dn);
    if (target > D.cell_w - 20) target = D.cell_w - 20;
    if (target > D.cell_h - D.fs_label - 20) target = D.cell_h - D.fs_label - 20;
    if (target < 40) target = 40;
    D.icon = target;

    D.dock_isz = (int)(38 * dn);
    if (D.dock_isz > D.dock_h - 20) D.dock_isz = D.dock_h - 20;
}

/* ---------------- 绘制：状态栏 ---------------- */

static void draw_status_bar(void)
{
    int W = D.W, h = D.status_h;
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    char tb[16];
    snprintf(tb, sizeof(tb), "%02d:%02d", tm.tm_hour, tm.tm_min);

    gfx_text_px(&D.g, D.cx_pad, h / 2 - D.fs_status / 2, tb, C_WHITE, D.fs_status);

    int pct = battery_percent();
    int isz = (int)(9 * D.dn);
    int rx = W - D.cx_pad;
    char pb[8];
    snprintf(pb, sizeof(pb), "%d%%", pct);
    gfx_text_right_px(&D.g, rx, h / 2 - D.fs_status / 2 + 2, pb, 0xE8EDF7, D.fs_status - 3);
    rx -= gfx_text_w_px(pb, D.fs_status - 3) + 10;

    int bw = 22, bh = 11;
    gfx_round_outline(&D.g, rx - bw, h / 2 - bh / 2, bw, bh, 3, 1, 0xE8EDF7);
    gfx_fill(&D.g, rx - bw + bw, h / 2 - 2, 2, 4, 0xE8EDF7);
    int fw = (bw - 4) * (pct > 100 ? 100 : pct) / 100;
    if (fw > 1) gfx_round(&D.g, rx - bw + 2, h / 2 - bh / 2 + 2, fw, bh - 4, 1,
                          pct < 20 ? 0xE5484D : 0xF2F5FA);
    rx -= bw + 10;

    for (int i = 0; i < 4; i++) {
        int bh2 = 4 + i * 3;
        gfx_round(&D.g, rx - 20 + i * 5, h / 2 + 5 - bh2, 3, bh2, 1,
                  D.airplane ? 0x6B7688 : 0xE8EDF7);
    }
    rx -= 28;

    gfx_icon(&D.g, "wifi", rx - isz * 2, h / 2 - isz, isz * 2, D.wifi_on ? 0xE8EDF7 : 0x6B7688);
    rx -= isz * 2 + 8;

    if (D.dnd_on) { gfx_icon(&D.g, "dnd", rx - isz * 2, h / 2 - isz, isz * 2, 0xE8EDF7); rx -= isz * 2 + 6; }
    if (D.bt_on) { gfx_icon(&D.g, "bluetooth", rx - isz * 2, h / 2 - isz, isz * 2, 0xE8EDF7); rx -= isz * 2 + 6; }

    if (ui_hit(&D.u, W - 180, 0, 180, h)) D.screen = SC_QS;
}

/* ---------------- 绘制：图标 ---------------- */

static void draw_app_icon(int x, int y, int size, const app_t *a)
{
    gfx_shadow(&D.g, x, y + 2, size, size, size / 4, 6, 46);
    gfx_round(&D.g, x, y, size, size, size / 4, a->color);
    gfx_round_a(&D.g, x, y, size, size / 2, size / 4, 0xFFFFFF, 26);
    int pad = size / 5;
    gfx_icon(&D.g, a->icon, x + pad, y + pad, size - pad * 2, C_WHITE);
}

static void launch_app(int idx);

static void fill_wallpaper(void)
{
    int W = D.W, H = D.H;
    if (!D.wall || D.wall_w != W || D.wall_h != H) {
        free(D.wall);
        D.wall = malloc((size_t)W * (size_t)H * 4);
        D.wall_w = W;
        D.wall_h = H;
        if (D.wall) {
            /* 先画到后缓冲，再拷进缓存（gfx 没有独立的目标切换） */
            draw_wallpaper();
            memcpy(D.wall, D.g.buf, (size_t)W * (size_t)H * 4);
            return;
        }
    }
    memcpy(D.g.buf, D.wall, (size_t)W * (size_t)H * 4);
}

static void draw_home(int interactive)
{
    int W = D.W;

    fill_wallpaper();
    draw_status_bar();

    /* 图标网格垂直居中：单页按实际行数，多页按满格，避免滑动时上下跳动 */
    int rows_used = (D.npages == 1) ? (D.napps + D.cols - 1) / D.cols : D.rows;
    if (rows_used < 1) rows_used = 1;
    if (rows_used > D.rows) rows_used = D.rows;
    int gtop = D.grid_top;
    int gh = rows_used * D.cell_h;
    if (gh < D.grid_bottom - D.grid_top)
        gtop += ((D.grid_bottom - D.grid_top) - gh) * 38 / 100;   /* 略偏上，重心更自然 */

    int ofs = (int)D.page_ofs;
    for (int p = 0; p < D.npages; p++) {
        int px = (p - D.page) * W + ofs;
        if (px <= -W || px >= W) continue;
        int start = p * D.per_page;
        for (int i = 0; i < D.per_page; i++) {
            int idx = start + i;
            if (idx >= D.napps) break;
            int cx = px + D.cx_pad + (i % D.cols) * D.cell_w;
            int cy = gtop + (i / D.cols) * D.cell_h;
            int ix = cx + (D.cell_w - D.icon) / 2;
            int iy = cy + (D.cell_h - D.icon - D.fs_label - D.label_gap) / 2;

            app_t *a = &D.apps[idx];
            int press = D.u.down && D.u.px >= cx && D.u.px < cx + D.cell_w &&
                        D.u.py >= cy && D.u.py < cy + D.cell_h;
            if (press && !D.page_drag) {
                gfx_round_a(&D.g, cx + 6, cy + 4, D.cell_w - 12, D.cell_h - 10, 16,
                            0xFFFFFF, 22);
            }
            draw_app_icon(ix, iy, D.icon, a);
            int ny = iy + D.icon + D.label_gap;
            if (gfx_text_w_px(a->name, D.fs_label) <= D.cell_w - 8)
                gfx_text_center_px(&D.g, cx + D.cell_w / 2, ny, a->name, 0xF0F4FA,
                                   D.fs_label);
            else
                gfx_text_fit_px(&D.g, cx + 4, ny, D.cell_w - 8, a->name,
                                0xF0F4FA, D.fs_label);

            if (interactive) {
                if (ui_hit(&D.u, cx, cy, D.cell_w, D.cell_h)) launch_app(idx);
                if (ui_hit_long(&D.u, cx, cy, D.cell_w, D.cell_h)) {
                    D.menu_app = idx;
                    D.screen = SC_MENU;
                }
            }
        }
    }

    if (D.npages > 1) {
        int dot_w = 7, gap = 8;
        int total = D.npages * dot_w + (D.npages - 1) * gap;
        int x = (W - total) / 2;
        int y = D.H - D.dock_h - 16;
        for (int i = 0; i < D.npages; i++) {
            int on = (i == D.page);
            gfx_circle_a(&D.g, x + i * (dot_w + gap) + dot_w / 2, y, on ? 4 : 3,
                         0xFFFFFF, on ? 235 : 90);
        }
    }
}

/* ---------------- 绘制：Dock ---------------- */

static void draw_dock(int interactive)
{
    int W = D.W, H = D.H;
    int h = D.dock_h - 14;
    int y = H - D.dock_h + 4;

    gfx_shadow(&D.g, D.cx_pad, y + 4, W - D.cx_pad * 2, h, h / 2, 12, 70);
    gfx_round_a(&D.g, D.cx_pad, y, W - D.cx_pad * 2, h, h / 2, 0x161C2A, 224);
    gfx_round_outline(&D.g, D.cx_pad, y, W - D.cx_pad * 2, h, h / 2, 1, 0x3A4560);

    int slots = D.ndock + 1;
    int sw = (W - D.cx_pad * 2) / slots;
    int isz = D.dock_isz;
    if (isz > sw - 14) isz = sw - 14;
    if (isz < 34) isz = 34;

    int cx = D.cx_pad + sw / 2;
    int iy = y + (h - isz) / 2;
    int opened = D.drawer_p > 0.02f;
    gfx_round(&D.g, cx - isz / 2, iy, isz, isz, isz / 4, opened ? 0x2F6BFF : 0x2A3247);
    gfx_icon(&D.g, "grid", cx - isz / 4, iy + isz / 4, isz / 2, C_WHITE);
    if (interactive && ui_hit(&D.u, cx - sw / 2, y, sw, h)) {
        D.screen = (D.screen == SC_DRAWER) ? SC_HOME : SC_DRAWER;
    }

    for (int i = 0; i < D.ndock; i++) {
        int idx = D.dock[i];
        if (idx >= D.napps) continue;
        int x = D.cx_pad + sw * (i + 1) + sw / 2;
        app_t *a = &D.apps[idx];
        int press = D.u.down && D.u.px >= x - sw / 2 && D.u.px < x + sw / 2 &&
                    D.u.py >= y && D.u.py < y + h;
        if (press) gfx_round_a(&D.g, x - isz / 2 - 4, iy - 2, isz + 8, isz + 8, 14,
                               0xFFFFFF, 26);
        draw_app_icon(x - isz / 2, iy, isz, a);
        if (interactive) {
            if (ui_hit(&D.u, x - sw / 2, y, sw, h)) launch_app(idx);
            if (ui_hit_long(&D.u, x - sw / 2, y, sw, h)) {
                D.menu_app = idx;
                D.screen = SC_MENU;
            }
        }
    }
}

/* ---------------- 绘制：应用抽屉 ---------------- */

static int app_match(const app_t *a, const char *q)
{
    if (!q || !q[0]) return 1;
    if (strcasestr(a->name, q)) return 1;
    if (strcasestr(a->id, q)) return 1;
    return 0;
}

static void draw_drawer(void)
{
    float p = D.drawer_p;
    if (p <= 0.005f) return;

    int W = D.W, H = D.H;
    int panel_y = (int)(H * (1.0f - p));

    /* 面板底板：接近不透明，避免下层图标透出造成重影 */
    gfx_fill(&D.g, 0, panel_y, W, H - panel_y, 0x0E1320);
    gfx_fill_a(&D.g, 0, panel_y, W, 1, 0x3A4560, 220);

    gfx_set_clip(&D.g, 0, panel_y, W, H - panel_y);

    int top = panel_y + (int)(10 * D.dn);
    gfx_round_a(&D.g, W / 2 - (int)(18 * D.dn), top, (int)(36 * D.dn), (int)(2 * D.dn),
                (int)(D.dn), 0xFFFFFF, 70);
    top += (int)(10 * D.dn);

    int bh = (int)(24 * D.dn);
    int sx = D.cx_pad, sw = W - D.cx_pad * 2 - (int)(28 * D.dn);
    gfx_round(&D.g, sx, top, sw, bh, bh / 2, 0x1E2534);
    gfx_icon(&D.g, "search", sx + (int)(8 * D.dn), top + bh / 2 - (int)(7 * D.dn),
             (int)(14 * D.dn), 0x8A93A6);
    int tby = top + bh / 2 - D.fs_small / 2;
    if (D.query[0])
        gfx_text_px(&D.g, sx + (int)(28 * D.dn), tby, D.query, 0xEDEFF3, D.fs_small);
    else
        gfx_text_px(&D.g, sx + (int)(28 * D.dn), tby, "搜索应用", 0x8A93A6, D.fs_small);
    if (ui_hit(&D.u, sx, top, sw, bh)) D.search_focus = 1;

    gfx_icon(&D.g, "close", W - D.cx_pad - (int)(12 * D.dn), top + bh / 2 - (int)(7 * D.dn),
             (int)(14 * D.dn), 0xB9C0CE);
    if (ui_hit(&D.u, W - D.cx_pad - (int)(22 * D.dn), top, (int)(22 * D.dn), bh)) {
        D.screen = SC_HOME;
        D.search_focus = 0;
    }
    top += bh + (int)(10 * D.dn);

    /* 应用网格 */
    int cols = W >= H ? 6 : 4;
    int cell_w = (W - D.cx_pad * 2) / cols;
    int icon = (int)(28 * D.dn);
    if (icon > cell_w - (int)(16 * D.dn)) icon = cell_w - (int)(16 * D.dn);
    int cell_h = icon + D.fs_tiny + (int)(18 * D.dn);

    int shown = 0;
    for (int i = 0; i < D.napps; i++) {
        if (!app_match(&D.apps[i], D.query)) continue;
        int cx = D.cx_pad + (shown % cols) * cell_w;
        int cy = top + (shown / cols) * cell_h;
        if (cy + cell_h > H - D.dock_h - 10) break;   /* 一屏放不下就停 */
        app_t *a = &D.apps[i];
        int ix = cx + (cell_w - icon) / 2;

        int press = D.u.down && D.u.px >= cx && D.u.px < cx + cell_w &&
                    D.u.py >= cy && D.u.py < cy + cell_h;
        if (press) gfx_round_a(&D.g, cx + 4, cy + 2, cell_w - 8, cell_h - 6, 14,
                               0xFFFFFF, 20);
        draw_app_icon(ix, cy + 8, icon, a);
        if (gfx_text_w_px(a->name, D.fs_tiny) <= cell_w - 10)
            gfx_text_center_px(&D.g, cx + cell_w / 2, cy + icon + 16, a->name,
                               0xE6EAF2, D.fs_tiny);
        else
            gfx_text_fit_px(&D.g, cx + 5, cy + icon + 16, cell_w - 10, a->name,
                            0xE6EAF2, D.fs_tiny);

        if (ui_hit(&D.u, cx, cy, cell_w, cell_h)) {
            D.screen = SC_HOME;
            D.search_focus = 0;
            launch_app(i);
        }
        if (ui_hit_long(&D.u, cx, cy, cell_w, cell_h)) {
            D.menu_app = i;
            D.screen = SC_MENU;
        }
        shown++;
    }
    if (shown == 0) {
        gfx_text_center_px(&D.g, W / 2, top + 40, "没有匹配的应用", 0x8A93A6, D.fs_small);
    }

    gfx_reset_clip(&D.g);

    /* 软键盘（搜索时） */
    if (D.search_focus) {
        int kb_h = H * 34 / 100;
        if (kb_h < 280) kb_h = 280;
        if (kb_h > 400) kb_h = 400;
        ui_keyboard(&D.u, 0, H - kb_h, W, kb_h, D.query, (int)sizeof(D.query));
    }
}

/* ---------------- 绘制：快捷设置 ---------------- */

typedef struct {
    const char *icon;
    const char *label;
    int *state;
    const char *key;
} tile_t;

static void set_conf(const char *key, int v)
{
    char p[256];
    snprintf(p, sizeof(p), "/data/home/.config/properos/system.conf");
    char val[16];
    snprintf(val, sizeof(val), "%d", v);

    /* 读改写，保持其他键 */
    size_t len = 0;
    char *txt = read_text(p, &len);
    char out[4096];
    size_t used = 0;
    int replaced = 0;
    if (txt) {
        char *save = NULL;
        for (char *ln = strtok_r(txt, "\n", &save); ln; ln = strtok_r(NULL, "\n", &save)) {
            char *eq = strchr(ln, '=');
            if (!eq) continue;
            *eq = '\0';
            if (!strcmp(ln, key)) {
                used += (size_t)snprintf(out + used, sizeof(out) - used, "%s=%s\n", key, val);
                replaced = 1;
            } else {
                used += (size_t)snprintf(out + used, sizeof(out) - used, "%s=%s\n", ln, eq + 1);
            }
        }
        free(txt);
    }
    if (!replaced) used += (size_t)snprintf(out + used, sizeof(out) - used, "%s=%s\n", key, val);

    char dir[256];
    snprintf(dir, sizeof(dir), "%s", p);
    char *slash = strrchr(dir, '/');
    if (slash) { *slash = '\0'; mkdir_p(dir); }
    int fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) { (void)write(fd, out, used); close(fd); }
}

static void apply_brightness(int pct)
{
    DIR *d = opendir("/sys/class/backlight");
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.') continue;
        char mp[256], bp[256];
        snprintf(mp, sizeof(mp), "/sys/class/backlight/%s/max_brightness", de->d_name);
        snprintf(bp, sizeof(bp), "/sys/class/backlight/%s/brightness", de->d_name);
        size_t l = 0;
        char *t = read_text(mp, &l);
        if (!t) continue;
        int max = atoi(t);
        free(t);
        if (max <= 0) continue;
        int v = max * pct / 100;
        int fd = open(bp, O_WRONLY | O_CLOEXEC);
        if (fd >= 0) {
            char s[16];
            int n = snprintf(s, sizeof(s), "%d\n", v);
            (void)write(fd, s, (size_t)n);
            close(fd);
        }
    }
    closedir(d);
}

static void draw_slider_row(const char *icon, int x, int y, int w, int *val, const char *key)
{
    gfx_icon(&D.g, icon, x, y + 2, 20, 0xC9D1E0);
    int tx = x + 30;
    int tw = w - 30 - 44;
    int sy = y + 10;

    gfx_round(&D.g, tx, sy - 3, tw, 8, 4, 0x2B3346);
    int fill = tw * (*val) / 100;
    if (fill > 2) gfx_round(&D.g, tx, sy - 3, fill, 8, 4, 0x5B8BFF);
    gfx_circle(&D.g, tx + fill, sy + 1, 9, C_WHITE);

    char vb[8];
    snprintf(vb, sizeof(vb), "%d", *val);
    gfx_text_right_px(&D.g, x + w, y + 1, vb, 0xC9D1E0, D.fs_tiny);

    /* 跟手拖动 */
    if (D.u.down && D.u.py >= y - 6 && D.u.py <= y + 24 &&
        D.u.px >= tx - 12 && D.u.px <= tx + tw + 12) {
        int nv = (D.u.px - tx) * 100 / (tw > 0 ? tw : 1);
        if (nv < 0) nv = 0;
        if (nv > 100) nv = 100;
        if (nv != *val) {
            *val = nv;
            set_conf(key, nv);
            if (!strcmp(key, "brightness")) apply_brightness(nv);
        }
    }
}

static void draw_qs(void)
{
    float p = D.qs_p;
    if (p <= 0.005f) return;

    int W = D.W, H = D.H;

    /* 面板高度按内容预算，只包住内容；其余区域用遮罩压暗，
       避免出现「铺满全屏却只有一小块内容」的空洞感 */
    int ytop = (int)(10 * D.dn);
    int yb = ytop;
    yb += D.fs_body + 2 + D.fs_tiny;                     /* 日期 + 版本 */
    yb += (int)(12 * D.dn);
    yb += (int)(20 * D.dn) + (int)(26 * D.dn);           /* 两个滑块行 */
    yb += (int)(40 * D.dn) * 2 + (int)(5 * D.dn) * 2;    /* 两行磁贴 */
    yb += (int)(16 * D.dn);
    int full = H - D.dock_h;
    if (yb > full) yb = full;
    int panel_h = (int)(yb * p);
    if (panel_h < 8) return;

    gfx_fill_a(&D.g, 0, 0, W, H, 0x000000, (int)(120 * p));
    gfx_fill_a(&D.g, 0, 0, W, panel_h, 0x0E1320, 250);
    gfx_fill_a(&D.g, 0, panel_h - 1, W, 1, 0x3A4560, 210);
    gfx_set_clip(&D.g, 0, 0, W, panel_h);

    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    static const char *wd[] = { "周日", "周一", "周二", "周三", "周四", "周五", "周六" };
    char db[64];
    snprintf(db, sizeof(db), "%d月%d日 %s", tm.tm_mon + 1, tm.tm_mday, wd[tm.tm_wday]);

    int x = D.cx_pad, y = ytop;
    gfx_text_px(&D.g, x, y, db, 0xE6EAF2, D.fs_body);
    gfx_text_px(&D.g, x, y + D.fs_body + 2, "ProperOS 25.0.0", 0x7F8AA0, D.fs_tiny);

    /* 右上：设置 + 电源 */
    gfx_icon(&D.g, "settings", W - D.cx_pad - 62, y, 24, 0xC9D1E0);
    if (ui_hit(&D.u, W - D.cx_pad - 74, y - 8, 40, 40)) {
        D.screen = SC_HOME;
        for (int i = 0; i < D.napps; i++)
            if (strstr(D.apps[i].id, "settings")) { launch_app(i); break; }
    }
    gfx_icon(&D.g, "power", W - D.cx_pad - 26, y, 24, 0xE08A8A);
    if (ui_hit(&D.u, W - D.cx_pad - 40, y - 8, 40, 40)) toast("长按电源键关机");

    y += D.fs_body + 2 + D.fs_tiny + (int)(12 * D.dn);

    draw_slider_row("brightness", x, y, W - x * 2, &D.brightness, "brightness");
    y += (int)(20 * D.dn);
    draw_slider_row("volume", x, y, W - x * 2, &D.volume, "volume");
    y += (int)(26 * D.dn);

    /* 磁贴 */
    tile_t tiles[8] = {
        { "wifi", "WLAN", &D.wifi_on, "wifi" },
        { "bluetooth", "蓝牙", &D.bt_on, "bt" },
        { "dnd", "勿扰", &D.dnd_on, "dnd" },
        { "brightness", "手电", &D.flash, "flash" },
        { "airplane", "飞行模式", &D.airplane, "airplane" },
        { "rotate", "自动旋转", &D.rotate, "auto_rotate" },
        { "battery", "省电", &D.saver, "battery_saver" },
        { "signal", "热点", &D.hotspot, "hotspot" },
    };
    int cols = 4;
    int gap = (int)(5 * D.dn);
    int tw = (W - x * 2 - gap * (cols - 1)) / cols;
    int th = (int)(40 * D.dn);
    int y_end = y;
    for (int i = 0; i < 8; i++) {
        int cx = x + (i % cols) * (tw + gap);
        int cy = y + (i / cols) * (th + gap);
        if (cy + th > panel_h - 10) break;
        y_end = cy + th;
        int on = *tiles[i].state;
        uint32_t bg = on ? 0x2F6BFF : 0x1E2534;
        if (D.u.down && D.u.px >= cx && D.u.px < cx + tw &&
            D.u.py >= cy && D.u.py < cy + th)
            bg = gfx_darken(bg, 16);
        gfx_round(&D.g, cx, cy, tw, th, (int)(8 * D.dn), bg);
        gfx_icon(&D.g, tiles[i].icon, cx + (int)(7 * D.dn), cy + (int)(6 * D.dn),
                 (int)(11 * D.dn), on ? C_WHITE : 0x9AA6BC);
        gfx_text_fit_px(&D.g, cx + (int)(7 * D.dn), cy + th - D.fs_tiny - (int)(5 * D.dn),
                        tw - (int)(12 * D.dn), tiles[i].label,
                        on ? C_WHITE : 0xB9C2D2, D.fs_tiny);
        if (ui_hit(&D.u, cx, cy, tw, th)) {
            *tiles[i].state = !on;
            set_conf(tiles[i].key, !on);
        }
    }

    gfx_reset_clip(&D.g);

    /* 点面板空白处收起（磁贴下方的留白 / 顶部极窄区） */
    if (ui_hit(&D.u, 0, 0, W, 6)) D.screen = SC_HOME;
    if (y_end + 4 < panel_h && ui_hit(&D.u, 0, y_end + 4, W, panel_h - y_end - 4))
        D.screen = SC_HOME;
}

/* ---------------- 绘制：长按菜单 ---------------- */

static void draw_menu(void)
{
    if (D.menu_app < 0 || D.menu_app >= D.napps) { D.screen = SC_HOME; return; }
    app_t *a = &D.apps[D.menu_app];
    int W = D.W, H = D.H;

    gfx_fill_a(&D.g, 0, 0, W, H, 0x000000, 140);

    int mw = W - D.cx_pad * 2;
    if (mw > 460) mw = 460;
    int mx = (W - mw) / 2;
    int mh = 268;
    int my = (H - mh) / 2;

    gfx_shadow(&D.g, mx, my + 10, mw, mh, 20, 16, 90);
    gfx_round(&D.g, mx, my, mw, mh, 20, 0x1B2130);

    draw_app_icon(mx + 20, my + 20, 48, a);
    gfx_text_fit_px(&D.g, mx + 80, my + 26, mw - 100, a->name, 0xEDEFF3, D.fs_body);
    gfx_text_fit_px(&D.g, mx + 80, my + 50, mw - 100, a->id, 0x7F8AA0, D.fs_tiny);

    const char *items[] = { "打开", "应用信息", "添加到主屏", "卸载" };
    int iy = my + 84;
    for (int i = 0; i < 4; i++) {
        int ry = iy + i * 44;
        if (D.u.down && D.u.px >= mx && D.u.px < mx + mw && D.u.py >= ry &&
            D.u.py < ry + 44)
            gfx_fill_a(&D.g, mx + 8, ry, mw - 16, 44, 0xFFFFFF, 12);
        gfx_text_px(&D.g, mx + 24, ry + 12, items[i], i == 3 ? 0xE08A8A : 0xE6EAF2,
                    D.fs_small);
        if (ui_hit(&D.u, mx + 8, ry, mw - 16, 44)) {
            if (i == 0) { D.screen = SC_HOME; launch_app(D.menu_app); }
            else if (i == 1) { D.screen = SC_HOME; toast(a->id); }
            else if (i == 2) { D.screen = SC_HOME; toast("已添加到主屏"); }
            else { D.screen = SC_HOME; toast("系统应用不可卸载"); }
        }
    }

    /* 点遮罩关闭 */
    if (ui_hit(&D.u, 0, 0, W, my)) D.screen = SC_HOME;
    if (ui_hit(&D.u, 0, my + mh, W, H - my - mh)) D.screen = SC_HOME;
}

/* ---------------- 提示条 ---------------- */

static void draw_toast(void)
{
    if (!D.toast[0] || mono_ms() > D.toast_until) return;
    int tw = gfx_text_w_px(D.toast, D.fs_small) + 40;
    if (tw > D.W - 40) tw = D.W - 40;
    int x = (D.W - tw) / 2;
    int y = D.H - D.dock_h - 70;
    gfx_round(&D.g, x, y, tw, 42, 21, 0x1F2532);
    gfx_text_fit_px(&D.g, x + 20, y + 13, tw - 40, D.toast, C_WHITE, D.fs_small);
}

/* 读取 system.conf：桌面与 .jspk 应用共用这一份，应用里改了开关回桌面要立刻生效 */
static void load_settings(void)
{
    char v[32];
    /* 缺键时把默认值落盘，.jspk 应用读同一份文件才不会拿到空值 */
    static const struct { const char *k; int def; } seeds[] = {
        { "wifi", 1 }, { "bt", 0 }, { "dnd", 0 }, { "airplane", 0 },
        { "auto_rotate", 1 }, { "battery_saver", 0 }, { "dark", 0 },
        { "brightness", 70 }, { "volume", 65 }, { "timeout", 30 },
    };
    for (size_t i = 0; i < sizeof(seeds) / sizeof(seeds[0]); i++)
        if (!conf_get(seeds[i].k, v, sizeof(v))) set_conf(seeds[i].k, seeds[i].def);

    if (conf_get("wallpaper", v, sizeof(v))) {
        if (strcmp(v, D.wallpaper)) {
            snprintf(D.wallpaper, sizeof(D.wallpaper), "%s", v);
            free(D.wall);          /* 换壁纸后缓存作废 */
            D.wall = NULL;
        }
    }
    D.brightness = conf_int("brightness", 70);
    D.volume = conf_int("volume", 65);
    D.wifi_on = conf_int("wifi", 1);
    D.bt_on = conf_int("bt", 0);
    D.dnd_on = conf_int("dnd", 0);
    D.airplane = conf_int("airplane", 0);
    D.rotate = conf_int("auto_rotate", 1);
    D.saver = conf_int("battery_saver", 0);
}

/* ---------------- 启动应用 ---------------- */

static void exec_app(const app_t *a)
{
    if (a->kind == 0) {
        execl("/usr/bin/properos-app", "properos-app", a->path, (char *)NULL);
        execl("/usr/local/bin/properos-app", "properos-app", a->path, (char *)NULL);
    } else {
        execl("/usr/bin/papp", "papp", "run", a->id, (char *)NULL);
    }
    _exit(127);
}

static void launch_app(int idx)
{
    if (idx < 0 || idx >= D.napps) return;
    app_t *a = &D.apps[idx];

    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        exec_app(a);
    }
    if (pid > 0) {
        int st = 0;
        waitpid(pid, &st, 0);
        /* 应用退出后重新接管控制台，避免 fbcon 覆盖 */
        gfx_console_grab(7);
        D.search_focus = 0;
        D.screen = SC_HOME;
        D.drawer_p = 0;
        D.qs_p = 0;
        D.page_ofs = 0;
        load_settings();
        scan_apps();
        layout();
    }
}

/* ---------------- 手势 ---------------- */

static void handle_events(const in_event_t *ev, int n)
{
    for (int i = 0; i < n; i++) {
        const in_event_t *e = &ev[i];
        /* 任何「抬手之后」的语义事件都说明手指已经离开；
         * 应用返回、浮层关闭时清一遍，避免图标按压高亮残留一帧 */
        if (e->kind != IN_DOWN && e->kind != IN_MOVE) D.u.down = 0;
        switch (e->kind) {
        case IN_DOWN:
            D.drag_x0 = e->x;
            D.drag_y0 = e->y;
            D.drag_kind = 0;
            D.page_drag = 0;
            D.fling = 0;
            break;

        case IN_MOVE: {
            int dx = e->x - D.drag_x0;
            int dy = e->y - D.drag_y0;
            if (!D.drag_kind) {
                if (abs(dx) > 14 && abs(dx) > abs(dy)) D.drag_kind = 1;
                else if (abs(dy) > 14) D.drag_kind = 2;
            }
            if (D.drag_kind == 1 && D.screen == SC_HOME && D.npages > 1) {
                if ((D.page == 0 && dx > 0) || (D.page == D.npages - 1 && dx < 0))
                    dx /= 3;   /* 边缘阻尼 */
                D.page_ofs = (float)dx;
                D.page_drag = 1;
            }
            break;
        }

        case IN_UP:
            if (D.drag_kind == 1 && D.page_drag) {
                int W = D.W;
                if (D.page_ofs < -W / 6 && D.page < D.npages - 1) {
                    D.page++;
                    D.page_ofs += (float)W;
                } else if (D.page_ofs > W / 6 && D.page > 0) {
                    D.page--;
                    D.page_ofs -= (float)W;
                }
                D.page_drag = 0;
            }
            D.drag_kind = 0;
            break;

        case IN_PULL_DOWN:
            D.screen = SC_QS;
            break;
        case IN_SWIPE:
            /* 浮层内随手甩动：快捷设置往上甩收起，抽屉往下甩关闭 */
            if (D.screen == SC_QS && (e->dy < -40 || e->vy < -1)) {
                D.screen = SC_HOME;
            } else if (D.screen == SC_DRAWER && (e->dy > 40 || e->vy > 1)) {
                D.screen = SC_HOME;
                D.search_focus = 0;
            }
            break;
        case IN_PULL_UP:
            /* 快捷设置里上滑是收起，其它界面才是拉出抽屉 */
            if (D.screen == SC_QS) D.screen = SC_HOME;
            else D.screen = SC_DRAWER;
            break;
        case IN_EDGE_LEFT:
        case IN_EDGE_RIGHT:
            if (D.screen == SC_QS || D.screen == SC_DRAWER || D.screen == SC_MENU) {
                D.screen = SC_HOME;
                D.search_focus = 0;
            }
            break;
        case IN_KEY:
            /* 键盘返回键（ESC / BACK）关闭浮层 */
            if (e->key == 1 || e->key == 158) {
                if (D.screen != SC_HOME) { D.screen = SC_HOME; D.search_focus = 0; }
            }
            break;
        default:
            break;
        }
    }
}

/* ---------------- 主循环 ---------------- */

static void on_signal(int s) { (void)s; g_quit = 1; }

static void draw_frame(void)
{
    switch (D.screen) {
    case SC_HOME:
        draw_home(1);
        draw_dock(1);
        break;
    case SC_DRAWER:
        draw_home(0);
        draw_dock(0);
        draw_drawer();
        break;
    case SC_QS:
        draw_home(0);
        draw_dock(0);
        draw_qs();
        break;
    case SC_MENU:
        draw_home(0);
        draw_dock(0);
        draw_menu();
        break;
    }
    draw_toast();
}

int main(int argc, char **argv)
{
    const char *rpath = NULL;
    int rs = 0;
    int rw = 0, rh = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--render") && i + 2 < argc) {
            rpath = argv[i + 2];
            rs = (i + 3 < argc) ? atoi(argv[i + 3]) : 0;
            char wh[32];
            snprintf(wh, sizeof(wh), "%s", argv[i + 1]);
            if (sscanf(wh, "%dx%d", &rw, &rh) != 2) { rw = 1280; rh = 800; }
            i += (i + 3 < argc) ? 3 : 2;
        }
    }
    (void)rs;

    memset(&D, 0, sizeof(D));
    D.W = rw;
    D.H = rh;
    D.wifi_on = 1;
    D.bt_on = 0;
    D.rotate = 1;
    D.brightness = 70;
    D.volume = 65;
    D.menu_app = -1;
    D.screen = SC_HOME;
    snprintf(D.wallpaper, sizeof(D.wallpaper), "mist");

    if (!rpath) {
        gfx_console_grab(7);
        atexit(gfx_console_release);
        if (gfx_open(&D.g, NULL) != 0) {
            fprintf(stderr, "无法打开 framebuffer\n");
            return 1;
        }
        input_open(&D.in, D.g.w, D.g.h);
        signal(SIGINT, on_signal);
        signal(SIGTERM, on_signal);
    } else {
        if (D.W <= 0 || D.H <= 0) { D.W = 1280; D.H = 800; }
        if (gfx_offscreen(&D.g, D.W, D.H) != 0) return 1;
    }
    D.W = D.g.w;
    D.H = D.g.h;

    /* 系统设置 */
    load_settings();

    scan_apps();
    layout();

    if (rpath) {
        if (rs == 1) { D.screen = SC_DRAWER; D.drawer_p = 1.0f; }
        else if (rs == 2) { D.screen = SC_QS; D.qs_p = 1.0f; }
        else if (rs == 3) { D.screen = SC_MENU; D.menu_app = 0; }
        draw_frame();
        gfx_save_bmp(&D.g, rpath);
        gfx_close(&D.g);
        return 0;
    }

    ui_init(&D.u, &D.g);
    int64_t last = mono_ms();

    while (!g_quit && !D.quit) {
        in_event_t ev[32];
        int n = input_poll(&D.in, ev, 32, 16);
        for (int i = 0; i < n; i++) ui_feed(&D.u, &ev[i]);
        handle_events(ev, n);
        ui_begin(&D.u);

        /* 动画推进 */
        int64_t now = mono_ms();
        float dt = (float)(now - last);
        if (dt > 60) dt = 60;
        last = now;

        float dp_target = (D.screen == SC_DRAWER) ? 1.0f : 0.0f;
        float qp_target = (D.screen == SC_QS) ? 1.0f : 0.0f;
        D.drawer_p = lerp(D.drawer_p, dp_target, 0.28f);
        D.qs_p = lerp(D.qs_p, qp_target, 0.30f);
        if (fabsf(D.drawer_p - dp_target) < 0.004f) D.drawer_p = dp_target;
        if (fabsf(D.qs_p - qp_target) < 0.004f) D.qs_p = qp_target;

        if (!D.page_drag && D.page_ofs != 0) {
            D.page_ofs *= 0.72f;
            if (fabsf(D.page_ofs) < 0.8f) D.page_ofs = 0;
            D.dirty = 1;
        }

        draw_frame();
        gfx_flush(&D.g);
        ui_end(&D.u);
    }

    input_close(&D.in);
    gfx_close(&D.g);
    gfx_console_release();
    return 0;
}
