/* ProperOS 2.5 Mobile · 图形安装器（properos-setup）
 *
 * 界面语言参考 Ubuntu 桌面安装程序（Subiquity / Flutter 版）：
 *   · 茄子紫（#2C001E）背景 + 居中白色窗口 + 左侧步骤栏
 *   · 橙色主按钮（#E95420）、细边框次按钮、圆点单选、勾选步骤
 *   · 竖屏手机折叠为「顶部标题 + 分段进度条 + 白色内容页」
 *   · 排版使用 19/20/22/34/40 px 多级字号（gfx_text_px），贴近桌面发行版密度
 *
 * 实际写入仍由 /usr/local/sbin/properos-install 完成，通过管道读取
 * "##P 百分比|说明" 进度行。触摸与键盘均可操作。
 *
 * 开发者：ByUsi Studio · 北啊呢
 */
#define _GNU_SOURCE
#include "../gfx/gfx.h"
#include "../gfx/ui.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <linux/input.h>
#include <linux/vt.h>
#include <time.h>
#include <unistd.h>

#define MAX_DISKS 8

/* ---------------- Ubuntu 色板 ---------------- */
#define C_AU        0x2C001E
#define C_AU_L      0x4A0B2E
#define C_ORANGE    0xE95420
#define C_ORANGE_D  0xC74312
#define C_UB_TEXT   0x232323
#define C_UB_SUB    0x616161
#define C_UB_MUTE   0x8F8F8F
#define C_UB_LINE   0xE2E2E2
#define C_UB_SIDE   0xF7F5F5
#define C_UB_SEL    0xFFF3ED

/* 字号 */
#define FS_TITLE 40
#define FS_BODY  22
#define FS_SMALL 20
#define FS_TINY  19

typedef struct {
    char dev[64];
    char model[64];
    unsigned long long bytes;
    int removable;
} disk_t;

typedef enum {
    PG_WELCOME = 0,
    PG_DISK,
    PG_CONFIG,
    PG_CONFIRM,
    PG_PROGRESS,
    PG_DONE,
    PG_ERROR,
} page_t;

typedef struct {
    gfx_t g;
    ui_t ui;
    page_t page;
    disk_t disks[MAX_DISKS];
    int ndisks;
    int sel;
    char hostname[64];
    char pass[64];
    int no_password;
    int focus;
    int kb_open;
    int pct;
    char step[160];
    char tail[400];
    char logs[3][160];
    int nlogs;
    pid_t child;
    int pfd;
    int finished;
    char err[256];
    int64_t done_at;
    int64_t started_at;
} inst_t;

static inst_t I;
static volatile sig_atomic_t g_quit;

static void take_display(void) { gfx_console_grab(7); }
static void release_display(void) { gfx_console_release(); }

/* ---------------- 磁盘枚举 ---------------- */

static void scan_disks(inst_t *it)
{
    it->ndisks = 0;
    DIR *d = opendir("/sys/block");
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d)) && it->ndisks < MAX_DISKS) {
        if (de->d_name[0] == '.') continue;
        if (!strncmp(de->d_name, "loop", 4) || !strncmp(de->d_name, "ram", 3) ||
            !strncmp(de->d_name, "zram", 4) || !strncmp(de->d_name, "nbd", 3) ||
            !strncmp(de->d_name, "sr", 2) || !strncmp(de->d_name, "fd", 2) ||
            !strncmp(de->d_name, "dm-", 3) || !strncmp(de->d_name, "md", 2) ||
            !strncmp(de->d_name, "pmem", 4))
            continue;

        char p[256];
        snprintf(p, sizeof(p), "/sys/block/%s/size", de->d_name);
        FILE *f = fopen(p, "r");
        if (!f) continue;
        unsigned long long sectors = 0;
        if (fscanf(f, "%llu", &sectors) != 1) { fclose(f); continue; }
        fclose(f);
        if (sectors == 0) continue;
        if (sectors * 512ULL < 2ULL * 1000 * 1000 * 1000) continue;

        disk_t *dk = &it->disks[it->ndisks];
        memset(dk, 0, sizeof(*dk));
        snprintf(dk->dev, sizeof(dk->dev), "/dev/%s", de->d_name);
        dk->bytes = sectors * 512ULL;

        snprintf(p, sizeof(p), "/sys/block/%s/device/model", de->d_name);
        f = fopen(p, "r");
        if (f) {
            if (fgets(dk->model, sizeof(dk->model), f)) {
                size_t n = strlen(dk->model);
                while (n > 0 && (dk->model[n - 1] == '\n' || dk->model[n - 1] == ' '))
                    dk->model[--n] = '\0';
            }
            fclose(f);
        }
        if (!dk->model[0]) snprintf(dk->model, sizeof(dk->model), "Block Device");

        snprintf(p, sizeof(p), "/sys/block/%s/removable", de->d_name);
        f = fopen(p, "r");
        if (f) { int r = 0; if (fscanf(f, "%d", &r) == 1) dk->removable = r; fclose(f); }
        it->ndisks++;
    }
    closedir(d);
}

static void human_size(unsigned long long b, char *out, size_t n)
{
    if (b >= 1000000000ULL) snprintf(out, n, "%llu GB", b / 1000000000ULL);
    else if (b >= 1000000ULL) snprintf(out, n, "%llu MB", b / 1000000ULL);
    else snprintf(out, n, "%llu KB", b / 1000ULL);
}

static int secure_boot_state(void)
{
    FILE *f = fopen("/sys/firmware/efi/efivars/SecureBoot-"
                    "8be4df61-93ca-11d2-aa0d-00e098032b8c", "rb");
    if (!f) return -1;
    unsigned char b[8] = {0};
    size_t n = fread(b, 1, sizeof(b), f);
    fclose(f);
    if (n < 5) return -1;
    return b[4] ? 1 : 0;
}

/* ---------------- 安装子进程 ---------------- */

static void start_install(inst_t *it)
{
    int p[2];
    if (pipe(p) < 0) { snprintf(it->err, sizeof(it->err), "无法创建管道"); it->page = PG_ERROR; return; }

    pid_t pid = fork();
    if (pid < 0) { snprintf(it->err, sizeof(it->err), "无法启动安装进程"); it->page = PG_ERROR; return; }
    if (pid == 0) {
        close(p[0]);
        dup2(p[1], STDOUT_FILENO);
        int nullfd = open("/dev/null", O_WRONLY);
        if (nullfd >= 0) dup2(nullfd, STDIN_FILENO);
        char *argv[16];
        int a = 0;
        argv[a++] = "/usr/local/sbin/properos-install";
        argv[a++] = "--disk";
        argv[a++] = it->disks[it->sel].dev;
        argv[a++] = "--hostname";
        argv[a++] = it->hostname[0] ? it->hostname : "properos";
        if (it->no_password || !it->pass[0]) argv[a++] = "--blank-password";
        else { argv[a++] = "--password"; argv[a++] = it->pass; }
        argv[a++] = "--yes";
        argv[a++] = "--progress";
        argv[a++] = NULL;
        execv(argv[0], argv);
        _exit(127);
    }
    close(p[1]);
    it->child = pid;
    it->pfd = p[0];
    fcntl(it->pfd, F_SETFL, O_NONBLOCK);
    it->pct = 0;
    it->started_at = (int64_t)time(NULL);
    snprintf(it->step, sizeof(it->step), "正在准备安装…");
    it->page = PG_PROGRESS;
}

static void poll_install(inst_t *it)
{
    if (it->pfd < 0) return;
    char buf[1024];
    ssize_t n;
    while ((n = read(it->pfd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        char *line = buf;
        while (line && *line) {
            char *nl = strchr(line, '\n');
            if (nl) *nl = '\0';
            if (!strncmp(line, "##P ", 4)) {
                int pct = atoi(line + 4);
                char *bar = strchr(line + 4, '|');
                if (pct >= 0 && pct <= 100) it->pct = pct;
                if (bar) {
                    snprintf(it->step, sizeof(it->step), "%s", bar + 1);
                    snprintf(it->tail, sizeof(it->tail), "%s", bar + 1);
                    if (it->nlogs == 0 || strcmp(it->logs[it->nlogs - 1], bar + 1)) {
                        if (it->nlogs == 3) {
                            memmove(it->logs[0], it->logs[1], sizeof(it->logs[0]) * 2);
                            it->nlogs = 2;
                        }
                        snprintf(it->logs[it->nlogs], sizeof(it->logs[0]), "%s", bar + 1);
                        it->nlogs++;
                    }
                }
            } else if (!strncmp(line, "##L ", 4)) {
                snprintf(it->tail, sizeof(it->tail), "%s", line + 4);
            }
            line = nl ? nl + 1 : NULL;
        }
    }
    if (n == 0) {
        close(it->pfd);
        it->pfd = -1;
        int st = 0;
        if (it->child > 0) waitpid(it->child, &st, 0);
        it->child = -1;
        if (WIFEXITED(st) && WEXITSTATUS(st) == 0) {
            it->pct = 100;
            snprintf(it->step, sizeof(it->step), "安装完成");
            it->done_at = (int64_t)time(NULL);
            it->page = PG_DONE;
        } else {
            snprintf(it->err, sizeof(it->err),
                     "安装进程异常退出（状态 %d）。请检查目标磁盘是否可用，或查看串口日志。", st);
            it->page = PG_ERROR;
        }
        it->finished = 1;
    }
}

/* ---------------- 窗口度量 ---------------- */

typedef struct {
    int wide;
    int wx, wy, ww, wh;
    int rail_w;
    int cx, cw;
    int header_h;
    int body_y, foot_y;
    int btn_h;
} frame_t;

static frame_t frame_metrics(void)
{
    frame_t f;
    memset(&f, 0, sizeof(f));
    int W = I.g.w, H = I.g.h;

    f.wide = (W >= 900 && W * 10 >= H * 12);
    if (f.wide) {
        int m = W / 22;
        if (m < 24) m = 24;
        if (m > 40) m = 40;
        int ww = W - 2 * m, wh = H - 2 * m;
        if (ww > 1040) ww = 1040;
        if (wh > 690) wh = 690;
        f.wx = (W - ww) / 2;
        f.wy = (H - wh) / 2;
        f.ww = ww;
        f.wh = wh;
        f.rail_w = ww < 880 ? 236 : 288;
        f.cx = f.wx + f.rail_w + 40;
        f.cw = f.ww - f.rail_w - 80;
        f.header_h = 0;
        f.body_y = f.wy + 36 + FS_TITLE + 8 + FS_BODY + 22;
        f.btn_h = 48;
    } else {
        f.wx = 0; f.wy = 0; f.ww = W; f.wh = H;
        f.rail_w = 0;
        f.cx = W * 7 / 100;
        f.cw = W - f.cx * 2;
        f.header_h = 84 + 34 + 6 + FS_SMALL + 22;
        if (f.header_h < H * 15 / 100) f.header_h = H * 15 / 100;
        if (f.header_h > H / 3) f.header_h = H / 3;
        f.body_y = f.header_h + 20;
        f.btn_h = 52;
    }
    f.foot_y = f.wy + f.wh - 26 - f.btn_h;
    return f;
}

/* ---------------- Ubuntu 风格控件 ---------------- */

enum { UB_PRIMARY = 0, UB_SECOND, UB_GHOST };

static int ub_btn(int x, int y, int w, int h, const char *label, int kind)
{
    int hov = ui_hover(&I.ui, x, y, w, h);
    int hit = ui_hit(&I.ui, x, y, w, h);
    uint32_t bg = 0, fg = C_WHITE, bd = 0;

    switch (kind) {
    case UB_PRIMARY: bg = hov ? C_ORANGE_D : C_ORANGE; break;
    case UB_SECOND:  bg = hov ? 0xF3F3F3 : C_WHITE; fg = C_UB_TEXT; bd = 0xC9C9C9; break;
    default:         fg = hov ? C_ORANGE_D : C_ORANGE; break;
    }
    if (bg) gfx_round(&I.g, x, y, w, h, 6, bg);
    if (bd) gfx_round_outline(&I.g, x, y, w, h, 6, 1, bd);
    if (kind == UB_GHOST && (hov || I.ui.down)) gfx_round_a(&I.g, x, y, w, h, 6, C_ORANGE, 20);
    gfx_text_center_px(&I.g, x + w / 2, y + (h - FS_BODY) / 2 - 2, label, fg, FS_BODY);
    return hit;
}

static int ub_text_w(const char *s, int px) { return gfx_text_w_px(s, px); }

static int ub_btn_w(const char *label, int pad) { return ub_text_w(label, FS_BODY) + pad; }

static int ub_next(const frame_t *f, const char *label)
{
    int w = ub_btn_w(label, 56);
    return ub_btn(f->wx + f->ww - 40 - w, f->foot_y, w, f->btn_h, label, UB_PRIMARY);
}

static int ub_back(const frame_t *f, const char *label, int next_w)
{
    int w = ub_btn_w(label, 48);
    int x = f->wx + f->ww - 40 - next_w - 14 - w;
    return ub_btn(x, f->foot_y, w, f->btn_h, label, UB_SECOND);
}

static void ub_radio(int x, int y, int on, int size)
{
    int cy = y + size / 2;
    if (on) {
        gfx_circle(&I.g, x + size / 2, cy, size / 2, C_ORANGE);
        gfx_circle(&I.g, x + size / 2, cy, size / 5, C_WHITE);
    } else {
        gfx_ring(&I.g, x + size / 2, cy, size / 2 - 1, 2, 0x9E9E9E, 255);
    }
}

static void ub_checkbox(int x, int y, int on, int size)
{
    if (on) {
        gfx_round(&I.g, x, y, size, size, 5, C_ORANGE);
        gfx_icon(&I.g, "check", x + size / 5, y + size / 5, size * 3 / 5, C_WHITE);
    } else {
        gfx_round(&I.g, x, y, size, size, 5, C_WHITE);
        gfx_round_outline(&I.g, x, y, size, size, 5, 2, 0x9E9E9E);
    }
}

static void buf_trim_utf8(char *b)
{
    size_t n = strlen(b);
    while (n > 0 && ((unsigned char)b[n - 1] & 0xC0) == 0x80) n--;
    if (n > 0) n--;
    b[n] = '\0';
}

static int ub_input(int id, int x, int y, int w, int h, char *buf, int cap,
                    const char *ph, int password)
{
    int hit = ui_hit(&I.ui, x, y, w, h);
    if (hit) I.ui.focus = id;
    int active = (I.ui.focus == id);
    int hov = ui_hover(&I.ui, x, y, w, h);

    gfx_round(&I.g, x, y, w, h, 6, C_WHITE);
    gfx_round_outline(&I.g, x, y, w, h, 6, active ? 2 : 1,
                      active ? C_ORANGE : (hov ? 0xB5B5B5 : 0xCCCCCC));

    if (active && I.ui.key_pending) {
        int code = I.ui.key_code;
        I.ui.key_pending = 0;
        if (code == KEY_BACKSPACE) {
            buf_trim_utf8(buf);
        } else if (code != KEY_ENTER && code != KEY_KPENTER) {
            uint32_t ch = ui_key_ascii(code);
            if (ch) {
                size_t len = strlen(buf);
                if ((int)len + 1 < cap) { buf[len] = (char)ch; buf[len + 1] = '\0'; }
            }
        }
    }

    int ty = y + (h - FS_BODY) / 2 - 2;
    int tx = x + 14;
    int len = (int)strlen(buf);
    if (len == 0) {
        gfx_text_fit_px(&I.g, tx, ty, w - 28, ph ? ph : "", C_UB_MUTE, FS_BODY);
    } else if (password) {
        char stars[64];
        int n = len > 32 ? 32 : len;
        for (int i = 0; i < n; i++) stars[i] = '*';
        stars[n] = '\0';
        gfx_text_px(&I.g, tx, ty, stars, C_UB_TEXT, FS_BODY);
    } else {
        gfx_text_fit_px(&I.g, tx, ty, w - 28, buf, C_UB_TEXT, FS_BODY);
    }

    if (active && ((I.ui.now_ms / 500) & 1)) {
        int cxp = tx + (len ? ub_text_w(buf, FS_BODY) : 0);
        if (cxp > x + w - 18) cxp = x + w - 18;
        gfx_fill(&I.g, cxp + 1, y + h / 2 - FS_BODY / 2, 2, FS_BODY, C_ORANGE);
    }
    return hit;
}

/* ---------------- 窗口外框 ---------------- */

static void draw_shell(const frame_t *f)
{
    int W = I.g.w, H = I.g.h;

    if (f->wide) {
        gfx_vgrad(&I.g, 0, 0, W, H, C_AU, 0x14000B);
        gfx_ring(&I.g, W - 40, H - 30, W / 5, 2, C_ORANGE, 40);
        gfx_ring(&I.g, W - 40, H - 30, W / 5 - 34, 2, C_ORANGE, 26);
        gfx_ring(&I.g, W - 40, H - 30, W / 5 - 68, 2, C_ORANGE, 16);
        gfx_glow(&I.g, W / 10, H, W / 2, 0x772953, 70);

        gfx_shadow(&I.g, f->wx, f->wy + 10, f->ww, f->wh, 14, 34, 120);
        gfx_round(&I.g, f->wx, f->wy, f->ww, f->wh, 14, C_WHITE);

        gfx_round(&I.g, f->wx, f->wy, f->rail_w, f->wh, 14, C_UB_SIDE);
        gfx_fill(&I.g, f->wx + f->rail_w - 14, f->wy, 14, f->wh, C_UB_SIDE);
        gfx_fill(&I.g, f->wx + f->rail_w, f->wy, 1, f->wh, C_UB_LINE);
    } else {
        gfx_vgrad(&I.g, 0, 0, W, f->header_h + 40, C_AU, C_AU_L);
        gfx_glow(&I.g, W * 84 / 100, 0, W * 62 / 100, 0x772953, 80);

        int st = f->header_h - 22;
        gfx_round(&I.g, 0, st, W, H - st, 24, C_WHITE);
        gfx_fill(&I.g, 0, st + 20, W, H - st - 20, C_WHITE);
    }
}

static const char *STEPS[4] = { "欢迎", "安装位置", "账户与设备", "确认安装" };

static void draw_rail(const frame_t *f, int step)
{
    int x = f->wx + 28;
    int y = f->wy + 30;

    gfx_round(&I.g, x, y, 34, 34, 9, C_ORANGE);
    gfx_icon(&I.g, "chip", x + 8, y + 8, 18, C_WHITE);
    gfx_text_px(&I.g, x + 44, y - 3, "ProperOS 2.5", C_UB_TEXT, FS_BODY);
    gfx_text_px(&I.g, x + 44, y + FS_BODY - 5, "安装程序", C_UB_MUTE, FS_TINY);

    int iy = f->wy + 128;
    for (int i = 0; i < 4; i++) {
        int active = (i == step), done = (i < step);
        if (active) gfx_fill(&I.g, f->wx, iy - 5, 3, 38, C_ORANGE);
        int cx = x + 12;
        if (done) {
            gfx_circle(&I.g, cx, iy + 11, 11, C_ORANGE);
            gfx_icon(&I.g, "check", cx - 7, iy + 4, 14, C_WHITE);
        } else if (active) {
            gfx_circle(&I.g, cx, iy + 11, 11, C_ORANGE);
            char n[8];
            snprintf(n, sizeof(n), "%d", i + 1);
            gfx_text_center_px(&I.g, cx, iy + 2, n, C_WHITE, FS_TINY);
        } else {
            gfx_ring(&I.g, cx, iy + 11, 10, 2, 0xC6C6C6, 255);
        }
        gfx_text_px(&I.g, x + 34, iy, STEPS[i],
                    active ? C_UB_TEXT : (done ? C_UB_SUB : C_UB_MUTE), FS_SMALL);

        if (i < step && ui_hit(&I.ui, f->wx + 8, iy - 6, f->rail_w - 16, 38))
            I.page = (page_t)i;
        iy += 50;
    }

    if (ub_btn(x, f->wy + f->wh - 26 - f->btn_h, 96, f->btn_h, "退出", UB_GHOST)) {
        sync();
        exit(0);
    }

    int sb = secure_boot_state();
    if (sb >= 0) {
        gfx_text_px(&I.g, x, f->wy + f->wh - 26 - f->btn_h - 34,
                    sb ? "安全启动：已启用" : "安全启动：未启用", C_UB_MUTE, FS_TINY);
    }
}

static void draw_phone_head(const frame_t *f, const char *title, const char *sub, int step)
{
    int W = I.g.w;
    int p = f->cx;

    int segw = (W - p * 2 - 5 * 3) / 4;
    for (int i = 0; i < 4; i++) {
        uint32_t c = (i <= step) ? C_ORANGE : 0x5E2445;
        gfx_round(&I.g, p + i * (segw + 5), 14, segw, 4, 2, c);
    }

    gfx_round(&I.g, p, 32, 34, 34, 9, C_ORANGE);
    gfx_icon(&I.g, "chip", p + 8, 40, 18, C_WHITE);
    gfx_text_px(&I.g, p + 44, 32, "ProperOS 2.5", C_WHITE, FS_BODY);
    char sb[32];
    snprintf(sb, sizeof(sb), "步骤 %d / 4", step > 3 ? 4 : step + 1);
    gfx_text_right_px(&I.g, W - p, 34, sb, 0xD9B7CB, FS_TINY);

    gfx_text_fit_px(&I.g, p, 78, W - p * 2, title, C_WHITE, 34);
    if (sub && sub[0])
        gfx_text_fit_px(&I.g, p, 78 + 34 + 4, W - p * 2, sub, 0xE0C4D2, FS_SMALL);
}

static int chrome(const char *title, const char *sub, int step)
{
    frame_t f = frame_metrics();
    draw_shell(&f);
    if (f.wide) {
        draw_rail(&f, step);
        gfx_text_px(&I.g, f.cx, f.wy + 36, title, C_UB_TEXT, FS_TITLE);
        if (sub && sub[0])
            gfx_text_px(&I.g, f.cx, f.wy + 36 + FS_TITLE + 8, sub, C_UB_SUB, FS_BODY);
    } else {
        draw_phone_head(&f, title, sub, step);
    }
    return f.body_y;
}

/* ---------------- 各页面 ---------------- */

/* 分区预览：ESP / 系统 / 数据 的比例条 + 图例，Ubuntu 安装器也有类似示意 */
static void partition_preview(const frame_t *f, int y)
{
    int bar_h = 14;
    int w = f->cw;
    int w_esp = w * 8 / 100;
    int w_sys = w * 34 / 100;
    int w_data = w - w_esp - w_sys;

    gfx_text_px(&I.g, f->cx, y, "分区预览", C_UB_MUTE, FS_SMALL);
    y += FS_SMALL + 10;

    gfx_round(&I.g, f->cx, y, w_esp, bar_h, 4, C_ORANGE);
    gfx_round(&I.g, f->cx + w_esp, y, w_sys, bar_h, 0, 0x772953);
    gfx_round(&I.g, f->cx + w_esp + w_sys, y, w_data, bar_h, 4, 0xCFC8BE);
    y += bar_h + 14;

    const char *names[3] = { "ESP", "系统", "/data" };
    const char *vals[3] = { "256 MiB", "4 GiB", "剩余空间" };
    uint32_t dots[3] = { C_ORANGE, 0x772953, 0xCFC8BE };
    int colw = w / 3;
    for (int i = 0; i < 3; i++) {
        int x = f->cx + i * colw;
        gfx_circle(&I.g, x + 6, y + FS_TINY / 2, 6, dots[i]);
        gfx_text_px(&I.g, x + 20, y - 2, names[i], C_UB_TEXT, FS_TINY);
        gfx_text_right_px(&I.g, x + colw - 14, y - 2, vals[i], C_UB_MUTE, FS_TINY);
    }
}

static int feature_rows(const frame_t *f, int y, int row, int tile, int fs_t, int fs_s)
{
    const char *icons[3] = { "shield", "chip", "lock" };
    const char *titles[3] = { "安全启动与完整性校验", "容器与沙箱运行时", "系统只读，数据独立" };
    const char *subs[3] = { "A/B 双槽，启动失败自动回滚", "rootful podman + papp 应用沙箱",
                            "/data 与系统分区分离，可单独清空" };
    for (int i = 0; i < 3; i++) {
        if (y + row > f->foot_y - 12) break;
        gfx_round(&I.g, f->cx, y + 4, tile, tile, tile / 4, 0xFDEDE6);
        gfx_icon(&I.g, icons[i], f->cx + tile / 4, y + 4 + tile / 4, tile / 2, C_ORANGE);
        int tx = f->cx + tile + 16;
        gfx_text_fit_px(&I.g, tx, y + 2, f->cx + f->cw - tx, titles[i], C_UB_TEXT, fs_t);
        gfx_text_fit_px(&I.g, tx, y + 2 + fs_t + 4, f->cx + f->cw - tx, subs[i],
                        C_UB_SUB, fs_s);
        y += row;
    }
    return y;
}

static void page_welcome(inst_t *it)
{
    frame_t f = frame_metrics();
    int y = chrome(f.wide ? "安装 ProperOS 2.5" : "欢迎",
                   f.wide ? "面向手机的 Linux 发行版 · ARM64 · ByUsi Studio"
                          : "四步完成，约 3 分钟", 0);

    if (f.wide) {
        y += gfx_text_wrap_px(&I.g, f.cx, y, f.cw,
                              "安装程序会引导你选择磁盘、设置设备名称与 root 账户，"
                              "然后把系统写入设备。开始前请确认重要数据已备份。",
                              C_UB_SUB, FS_BODY, 8) + 26;
        feature_rows(&f, y, 68, 40, FS_BODY, FS_TINY);

        /* 底部一行系统要求，填充纵向空白（Ubuntu 也会在欢迎页列出最低配置） */
        const char *req[3] = { "存储 ≥ 8 GB", "内存 ≥ 1 GB", "固件 UEFI · ARM64" };
        int ry = f.foot_y - 40;
        int colw = f.cw / 3;
        for (int i = 0; i < 3; i++)
            gfx_text_px(&I.g, f.cx + i * colw, ry, req[i], C_UB_MUTE, FS_TINY);
    } else {
        int block = 120 + 20 + 40 + 10 + FS_TINY + 40 + 3 * 100;
        int avail = f.foot_y - 14 - y;
        if (avail > block) y += (avail - block) / 2;

        int logo = 120;
        int lx = f.wx + f.ww / 2 - logo / 2;
        gfx_shadow(&I.g, lx, y + 6, logo, logo, 30, 14, 26);
        gfx_round(&I.g, lx, y, logo, logo, 30, C_ORANGE);
        gfx_icon(&I.g, "chip", lx + 34, y + 34, 52, C_WHITE);
        y += logo + 20;
        gfx_text_center_px(&I.g, f.wx + f.ww / 2, y, "ProperOS 2.5", C_UB_TEXT, 40);
        y += 40 + 10;
        gfx_text_center_px(&I.g, f.wx + f.ww / 2, y, "ARM64 · Linux 7.3.6 · ByUsi Studio",
                           C_UB_SUB, FS_TINY);
        y += FS_TINY + 40;
        feature_rows(&f, y, 100, 48, FS_BODY, FS_TINY);
    }

    int nw = ub_btn_w("开始安装", 56);
    if (ub_next(&f, "开始安装")) {
        scan_disks(it);
        if (it->ndisks == 0) {
            snprintf(it->err, sizeof(it->err),
                     "未找到容量大于 2 GB 的磁盘。请确认安装介质已挂载，或改用预装镜像。");
            it->page = PG_ERROR;
        } else {
            it->page = PG_DISK;
        }
    }
    if (!f.wide && ub_back(&f, "退出", nw)) { sync(); exit(0); }
}

static void page_disk(inst_t *it)
{
    frame_t f = frame_metrics();
    int y = chrome("安装位置", "选择要写入的磁盘，磁盘上的数据将被清除", 1);

    gfx_text_px(&I.g, f.cx, y, "安装类型", C_UB_MUTE, FS_SMALL);
    y += FS_SMALL + 8;

    int opt_h = f.wide ? 72 : 84;
    gfx_round(&I.g, f.cx, y, f.cw, opt_h, 7, C_UB_SEL);
    ub_radio(f.cx + 16, y + opt_h / 2 - 10, 1, 20);
    gfx_text_fit_px(&I.g, f.cx + 48, y + 11, f.cw - 64, "清除整个磁盘并安装 ProperOS",
                    C_UB_TEXT, FS_BODY);
    gfx_text_fit_px(&I.g, f.cx + 48, y + 40, f.cw - 64,
                    "ESP 256 MiB · 系统 4 GiB · 剩余空间作 /data", C_UB_SUB, FS_TINY);
    y += opt_h + 8;

    gfx_round_outline(&I.g, f.cx, y, f.cw, opt_h, 7, 1, 0xEDEDED);
    ub_radio(f.cx + 16, y + opt_h / 2 - 10, 0, 20);
    gfx_text_px(&I.g, f.cx + 48, y + 11, "手动分区（高级）", C_UB_MUTE, FS_BODY);
    gfx_text_fit_px(&I.g, f.cx + 48, y + 40, f.cw - 64,
                    "在终端执行 parted 自行布局，安装后用 bootctl 切换 A/B 槽",
                    C_UB_MUTE, FS_TINY);
    y += opt_h + 20;

    gfx_text_px(&I.g, f.cx, y, "目标磁盘", C_UB_MUTE, FS_SMALL);
    char cnt[32];
    snprintf(cnt, sizeof(cnt), "共 %d 个", it->ndisks);
    gfx_text_right_px(&I.g, f.cx + f.cw, y, cnt, C_UB_MUTE, FS_TINY);
    y += FS_SMALL + 8;

    /* 底部预留分区预览（矮屏没有空间就不画） */
    int preview_h = 90;
    int area = f.foot_y - 10 - y;
    int with_preview = area > it->ndisks * 88 + preview_h + 20;
    if (with_preview) area -= preview_h + 16;
    int row_h = f.wide ? 72 : 88;
    if (it->ndisks > 0 && it->ndisks * row_h > area) row_h = area / it->ndisks;
    if (row_h > (f.wide ? 72 : 88)) row_h = f.wide ? 72 : 88;
    if (row_h < 58) row_h = 58;
    int shown = (area > 0) ? area / row_h : 0;
    if (shown > it->ndisks) shown = it->ndisks;

    for (int i = 0; i < shown; i++) {
        int ry = y + i * row_h;
        int sel = (i == it->sel);
        if (sel) gfx_round(&I.g, f.cx, ry, f.cw, row_h - 6, 7, C_UB_SEL);
        else if (ui_hover(&I.ui, f.cx, ry, f.cw, row_h - 6))
            gfx_round(&I.g, f.cx, ry, f.cw, row_h - 6, 7, 0xF4F4F4);
        ub_radio(f.cx + 16, ry + (row_h - 6) / 2 - 10, sel, 20);

        char hs[32], sub[160];
        human_size(it->disks[i].bytes, hs, sizeof(hs));
        snprintf(sub, sizeof(sub), "%s · %s%s", hs, it->disks[i].model,
                 it->disks[i].removable ? " · 可移动介质" : "");
        gfx_text_px(&I.g, f.cx + 48, ry + 6, it->disks[i].dev, C_UB_TEXT, FS_BODY);
        gfx_text_fit_px(&I.g, f.cx + 48, ry + 32, f.cw - 66, sub, C_UB_SUB, FS_TINY);

        if (ui_hit(&I.ui, f.cx, ry, f.cw, row_h - 6)) it->sel = i;
    }

    if (with_preview)
        partition_preview(&f, y + shown * row_h + 10);

    if (it->ndisks == 0) {
        gfx_text_px(&I.g, f.cx, y + 6, "未找到可安装的磁盘", C_DANGER, FS_BODY);
        gfx_text_wrap_px(&I.g, f.cx, y + 6 + FS_BODY + 8, f.cw,
                         "请确认虚拟机已挂载硬盘，或在真机上检查 eMMC / UFS 是否被内核识别。",
                         C_UB_SUB, FS_TINY, 6);
    }

    int nw = ub_btn_w("继续", 56);
    if (ub_next(&f, "继续")) {
        if (it->ndisks > 0) it->page = PG_CONFIG;
    }
    if (ub_back(&f, "上一步", nw)) it->page = PG_WELCOME;
}

static void page_config(inst_t *it)
{
    frame_t f = frame_metrics();

    if (it->kb_open && I.ui.tap_pending && I.ui.tap_y < I.g.h * 62 / 100) {
        it->kb_open = 0;
        I.ui.tap_pending = 0;
    }

    int y0 = chrome("账户与设备", "设置设备名称与 root 账户", 2);
    int compact = !f.wide && f.wh < 1100;
    int ih = compact ? 60 : 52;
    int gap = compact ? 26 : 22;

    int y = y0;
    if (!f.wide) {
        int block = (FS_BODY + 6 + ih + gap) * 2 + 22 + 20;
        int avail = (f.foot_y - 110) - y0;
        if (avail > block) y = y0 + (avail - block) / 2;
    }

    gfx_text_px(&I.g, f.cx, y, "设备名称", C_UB_TEXT, FS_BODY);
    gfx_text_right_px(&I.g, f.cx + f.cw, y + 2, "用于局域网显示", C_UB_MUTE, FS_TINY);
    y += FS_BODY + 6;
    if (ub_input(1, f.cx, y, f.cw, ih, it->hostname, sizeof(it->hostname),
                 "例如 properos", 0)) {
        it->focus = 0;
        it->kb_open = 1;
    }
    y += ih + gap;

    gfx_text_px(&I.g, f.cx, y, "root 密码", C_UB_TEXT, FS_BODY);
    gfx_text_right_px(&I.g, f.cx + f.cw, y + 2, "可留空", C_UB_MUTE, FS_TINY);
    y += FS_BODY + 6;
    if (ub_input(2, f.cx, y, f.cw, ih, it->pass, sizeof(it->pass),
                 it->no_password ? "已选择不设置密码" : "输入密码", 1)) {
        it->focus = 1;
        it->kb_open = 1;
    }
    y += ih + (compact ? 14 : 20);

    int chk = 22;
    ub_checkbox(f.cx, y + 2, it->no_password, chk);
    gfx_text_px(&I.g, f.cx + chk + 14, y - 2, "不设置 root 密码，开机直接进入桌面",
                C_UB_TEXT, FS_BODY);
    if (ui_hit(&I.ui, f.cx - 6, y - 6, f.cw, chk + 16)) {
        it->no_password = !it->no_password;
        if (it->no_password) it->pass[0] = '\0';
    }
    y += chk + 20;

    int avail_n = f.foot_y - 6 - y;
    if (avail_n >= 64) {
        int note_h = avail_n > 88 ? 88 : avail_n;
        int ny = f.foot_y - 6 - note_h;      /* 贴底，避免中段出现空洞 */
        if (ny < y) ny = y;
        gfx_round(&I.g, f.cx, ny, f.cw, note_h, 7, 0xFAF7F5);
        gfx_fill(&I.g, f.cx, ny + 10, 3, note_h - 20, C_ORANGE);
        gfx_text_wrap_px(&I.g, f.cx + 18, ny + (note_h - FS_TINY * 2) / 2 - 2, f.cw - 36,
                         "密码仅用于本地终端登录。留空时系统启动后直接进入桌面，"
                         "之后可在设置中补设。", C_UB_SUB, FS_TINY, 6);
    }

    if (it->kb_open) {
        int kh = I.g.h * 32 / 100;
        int ky = I.g.h - kh;
        gfx_fill_a(&I.g, 0, 0, I.g.w, I.g.h, 0x000000, 60);
        gfx_round(&I.g, 0, ky, I.g.w, kh + 20, 20, 0xF2F2F2);
        gfx_fill(&I.g, 0, ky + kh - 20, I.g.w, 40, 0xF2F2F2);
        gfx_round(&I.g, I.g.w / 2 - 20, ky + 9, 40, 4, 2, 0xC2C2C2);
        char *target = it->focus == 0 ? it->hostname : it->pass;
        int cap = it->focus == 0 ? (int)sizeof(it->hostname) : (int)sizeof(it->pass);
        if (it->no_password && it->focus == 1) { /* 不设置密码时不输入 */ }
        else if (ui_keyboard(&I.ui, 8, ky + 24, I.g.w - 16, kh - 32, target, cap)) {
            it->kb_open = 0;
        }
        return;
    }

    int nw = ub_btn_w("继续", 56);
    if (ub_next(&f, "继续")) {
        if (!it->hostname[0]) snprintf(it->hostname, sizeof(it->hostname), "properos");
        it->page = PG_CONFIRM;
    }
    if (ub_back(&f, "上一步", nw)) it->page = PG_DISK;
}

static void page_confirm(inst_t *it)
{
    frame_t f = frame_metrics();
    int y = chrome("确认安装", "核对以下信息，点击“现在安装”后开始写入", 3);

    char hs[32];
    human_size(it->disks[it->sel].bytes, hs, sizeof(hs));

    const char *keys[5] = { "系统版本", "目标磁盘", "磁盘容量", "设备名称", "root 密码" };
    char vals[5][160];
    snprintf(vals[0], sizeof(vals[0]), "ProperOS 2.5 · ARM64");
    snprintf(vals[1], sizeof(vals[1]), "%s", it->disks[it->sel].dev);
    snprintf(vals[2], sizeof(vals[2]), "%s · %s", hs, it->disks[it->sel].model);
    snprintf(vals[3], sizeof(vals[3]), "%s", it->hostname[0] ? it->hostname : "properos");
    snprintf(vals[4], sizeof(vals[4]), "%s",
             (it->no_password || !it->pass[0]) ? "不设置（自动登录）" : "已设置");

    int rows = 5;
    int row_h = (f.foot_y - 110 - y) / rows;     /* 竖屏自动撑开到 84 px */
    if (row_h > 84) row_h = 84;
    if (row_h < 42) row_h = 42;

    for (int i = 0; i < rows; i++) {
        int ry = y + i * row_h;
        if (i) gfx_fill(&I.g, f.cx, ry, f.cw, 1, 0xEFEFEF);
        gfx_text_px(&I.g, f.cx, ry + row_h / 2 - FS_BODY / 2 - 2, keys[i], C_UB_SUB, FS_BODY);
        gfx_text_right_px(&I.g, f.cx + f.cw, ry + row_h / 2 - FS_BODY / 2 - 2, vals[i],
                          C_UB_TEXT, FS_BODY);
    }
    y += rows * row_h + 14;

    int whh = FS_TINY * 2 + 26;
    if (y + whh > f.foot_y - 8) whh = FS_TINY + 20;
    gfx_round(&I.g, f.cx, y, f.cw, whh, 7, 0xFDF2EC);
    gfx_fill(&I.g, f.cx, y, 4, whh, C_ORANGE);
    gfx_text_px(&I.g, f.cx + 20, y + 12, "目标磁盘上的分区与数据将被全部覆盖", 0x8A3B10, FS_BODY);
    if (whh > FS_TINY + 28)
        gfx_text_px(&I.g, f.cx + 20, y + 12 + FS_BODY + 4, "请确认重要数据已备份",
                    C_UB_SUB, FS_TINY);

    int nw = ub_btn_w("现在安装", 56);
    if (ub_next(&f, "现在安装")) start_install(it);
    if (ub_back(&f, "上一步", nw)) it->page = PG_CONFIG;
}

static const char *INST_STEPS[4] = { "分区与格式化", "写入系统文件", "配置引导", "完成收尾" };

static int inst_step_index(int pct)
{
    if (pct < 14) return 0;
    if (pct < 72) return 1;
    if (pct < 94) return 2;
    return 3;
}

static int inst_step_done(int pct, int i)
{
    static const int th[5] = { 0, 14, 72, 94, 101 };
    return pct >= th[i + 1];
}

static void page_progress(inst_t *it)
{
    frame_t f = frame_metrics();
    int y = chrome("正在安装", "正在写入系统，请勿关闭设备", 4);

    char pb[16];
    snprintf(pb, sizeof(pb), "%d%%", it->pct);

    gfx_text_px(&I.g, f.cx, y, it->step[0] ? it->step : "正在安装…", C_UB_TEXT, FS_BODY);
    gfx_text_right_px(&I.g, f.cx + f.cw, y, pb, C_ORANGE, FS_BODY);
    y += FS_BODY + 10;

    int bar_h = 8;
    gfx_round(&I.g, f.cx, y, f.cw, bar_h, bar_h / 2, 0xEDEDED);
    int fill = f.cw * (it->pct > 100 ? 100 : it->pct) / 100;
    if (fill > 2) gfx_round(&I.g, f.cx, y, fill, bar_h, bar_h / 2, C_ORANGE);
    y += bar_h + 26;

    int cur = inst_step_index(it->pct);
    int list_y = y;
    for (int i = 0; i < 4; i++) {
        int done = inst_step_done(it->pct, i);
        int active = (i == cur) && !done;
        int cy = list_y + i * 40;
        if (done) {
            gfx_circle(&I.g, f.cx + 10, cy + 10, 10, C_ORANGE);
            gfx_icon(&I.g, "check", f.cx + 4, cy + 4, 12, C_WHITE);
        } else if (active) {
            int pulse = (int)((I.ui.now_ms / 140) % 8);
            gfx_ring(&I.g, f.cx + 10, cy + 10, 9 + pulse / 3, 2, C_ORANGE, 200);
            gfx_circle(&I.g, f.cx + 10, cy + 10, 3, C_ORANGE);
        } else {
            gfx_ring(&I.g, f.cx + 10, cy + 10, 9, 2, 0xDCDCDC, 255);
        }
        gfx_text_px(&I.g, f.cx + 34, cy - 2, INST_STEPS[i],
                    done ? C_UB_SUB : (active ? C_UB_TEXT : C_UB_MUTE), FS_SMALL);
    }
    y = list_y + 4 * 40 + 10;

    int lmax = f.wide ? 92 : 152;
    int lh = f.foot_y - 6 - y;
    if (lh > lmax) lh = lmax;
    if (!f.wide) y = f.foot_y - 6 - lh;      /* 竖屏把日志贴底，上方留白更自然 */
    if (lh >= 78) {
        gfx_round(&I.g, f.cx, y, f.cw, lh, 7, 0xF7F7F7);
        gfx_text_px(&I.g, f.cx + 14, y + 10, "安装日志", C_UB_MUTE, FS_TINY);
        int ty = y + 10 + FS_TINY + 8;
        int shown = it->nlogs;
        if (shown > 3) shown = 3;
        if (shown == 0) {
            gfx_text_fit_px(&I.g, f.cx + 14, ty, f.cw - 28, "等待安装进程输出…",
                            C_UB_SUB, FS_TINY);
        } else {
            for (int i = 0; i < shown; i++) {
                if (ty + FS_TINY > y + lh - 8) break;
                gfx_text_fit_px(&I.g, f.cx + 14, ty, f.cw - 28, it->logs[i], C_UB_SUB,
                                FS_TINY);
                ty += FS_TINY + 4;
            }
        }
    } else if (lh >= 30) {
        gfx_text_fit_px(&I.g, f.cx, y + 4, f.cw,
                        it->tail[0] ? it->tail : "等待安装进程输出…", C_UB_MUTE, FS_TINY);
    }

    poll_install(it);
}

static void page_done(inst_t *it)
{
    frame_t f = frame_metrics();
    int y = chrome("安装完成", "系统已写入设备", 4);

    int r = f.wide ? 52 : 60;
    int cxx = f.wx + f.ww / 2;
    int cy = y + r + 8;
    gfx_circle(&I.g, cxx, cy, r, 0xFDEBE3);
    gfx_icon(&I.g, "check", cxx - r / 2, cy - r / 2, r, C_ORANGE);

    y = cy + r + 24;
    gfx_text_center_px(&I.g, cxx, y, "请移除安装介质，然后重新启动设备", C_UB_TEXT, FS_BODY);
    y += FS_BODY + 8;
    gfx_text_center_px(&I.g, cxx, y, "首次启动会初始化 /data 并生成桌面快捷方式",
                       C_UB_SUB, FS_TINY);
    y += FS_TINY + 12;

    int elapsed = it->done_at && it->started_at ? (int)(it->done_at - it->started_at) : 0;
    if (elapsed > 0 && y < f.foot_y - 40) {
        char tb[64];
        snprintf(tb, sizeof(tb), "用时 %d 分 %d 秒", elapsed / 60, elapsed % 60);
        gfx_text_center_px(&I.g, cxx, y, tb, C_UB_MUTE, FS_TINY);
    }

    if (ub_next(&f, "立即重新启动")) {
        sync();
        if (system("reboot -f") != 0) { /* 无 init 时直接退出 */ }
    }
}

static void page_error(inst_t *it)
{
    frame_t f = frame_metrics();
    int y = chrome("安装未完成", "遇到问题，安装已停止", 4);

    int r = f.wide ? 34 : 40;
    int cy = y + r;
    gfx_circle(&I.g, f.cx + r, cy, r, 0xFBE9E9);
    gfx_icon(&I.g, "close", f.cx + r / 2, cy - r / 2, r, C_DANGER);

    int tx = f.cx + r * 2 + 18;
    gfx_text_px(&I.g, tx, cy - FS_BODY - 2, "无法继续安装", C_UB_TEXT, FS_BODY);
    gfx_text_wrap_px(&I.g, tx, cy + 4, f.cx + f.cw - tx,
                     it->err[0] ? it->err : "未知错误", C_UB_SUB, FS_TINY, 6);

    if (ub_next(&f, "返回首页")) it->page = PG_WELCOME;
}

/* ---------------- 主循环 ---------------- */

static void on_signal(int s) { (void)s; g_quit = 1; }

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--version")) {
            printf("properos-setup 1.1 (ProperOS 2.5)\n");
            return 0;
        }
    }

    int rw = 0, rh = 0;
    const char *rpath = NULL;
    int rpage = -1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--render") && i + 3 < argc) {
            if (sscanf(argv[i + 1], "%dx%d", &rw, &rh) == 2) {
                rpath = argv[i + 2];
                rpage = atoi(argv[i + 3]);
            }
        }
    }

    memset(&I, 0, sizeof(I));
    if (!rpath) {
        take_display();
        atexit(release_display);
    }
    if (rpath) {
        if (gfx_offscreen(&I.g, rw, rh) < 0) return 2;
    } else if (gfx_open(&I.g, NULL) < 0) {
        fprintf(stderr, "properos-setup: 无法打开 framebuffer\n");
        return 2;
    }
    ui_init(&I.ui, &I.g);
    I.page = PG_WELCOME;
    I.pfd = -1;
    I.child = -1;
    snprintf(I.hostname, sizeof(I.hostname), "properos");

    if (rpath) {
        scan_disks(&I);
        I.sel = 0;
        snprintf(I.step, sizeof(I.step), "正在复制系统文件");
        I.pct = 42;
        snprintf(I.tail, sizeof(I.tail), "正在复制系统文件");
        snprintf(I.logs[0], sizeof(I.logs[0]), "创建分区表并格式化 ESP");
        snprintf(I.logs[1], sizeof(I.logs[1]), "写入系统镜像（squashfs → ext4）");
        snprintf(I.logs[2], sizeof(I.logs[2]), "正在复制系统文件");
        I.nlogs = 3;
        ui_begin(&I.ui);
        switch (rpage) {
        case 0: page_welcome(&I); break;
        case 1: page_disk(&I); break;
        case 2: page_config(&I); break;
        case 3: page_confirm(&I); break;
        case 4: page_progress(&I); break;
        case 5: page_done(&I); break;
        case 6:
            snprintf(I.err, sizeof(I.err), "未找到容量大于 2 GB 的磁盘");
            page_error(&I);
            break;
        default: page_welcome(&I); break;
        }
        gfx_save_bmp(&I.g, rpath);
        gfx_close(&I.g);
        printf("rendered page %d -> %s\n", rpage, rpath);
        return 0;
    }

    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);

    input_t in;
    input_open(&in, I.g.w, I.g.h);

    int dirty = 1;
    while (!g_quit) {
        in_event_t ev[32];
        int n = input_poll(&in, ev, 32, 40);
        for (int i = 0; i < n; i++) ui_feed(&I.ui, &ev[i]);

        if (I.page == PG_PROGRESS) dirty = 1;
        if (n > 0) dirty = 1;

        if (dirty) {
            ui_begin(&I.ui);
            switch (I.page) {
            case PG_WELCOME:  page_welcome(&I); break;
            case PG_DISK:     page_disk(&I); break;
            case PG_CONFIG:   page_config(&I); break;
            case PG_CONFIRM:  page_confirm(&I); break;
            case PG_PROGRESS: page_progress(&I); break;
            case PG_DONE:     page_done(&I); break;
            case PG_ERROR:    page_error(&I); break;
            }
            gfx_flush(&I.g);
            ui_end(&I.ui);
            dirty = 0;
        }
    }

    input_close(&in);
    gfx_close(&I.g);
    release_display();
    return 0;
}
