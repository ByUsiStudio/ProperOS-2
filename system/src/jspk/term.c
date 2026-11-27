/* ProperOS 25.0.0 Mobile · 真终端实现
 *
 * 结构：
 *   master pty ──read──► VT 解析器 ──► 字符网格 ──► gfx 光栅化
 *        ▲                                   │
 *        └────── 键盘/软键盘 ────────────────┘
 *
 * 网格保留 1000 行历史，PgUp/PgDn 回看；宽度按 Unicode 东亚宽度占两格，
 * 中文文件名与 ls 输出不会错位。
 *
 * 开发者：ByUsi Studio · 北啊呢
 */
#define _GNU_SOURCE
#include "term.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define TERM_HIST 1000

#define ATTR_BOLD 0x01
#define ATTR_REV  0x02
#define ATTR_UL   0x04
#define ATTR_DIM  0x08
#define ATTR_WIDE 0x10     /* 宽字符占用的第二个格子 */

typedef struct {
    uint32_t ch;
    uint32_t fg, bg;
    uint8_t attr;
} tcell_t;

struct term_s {
    int fd;
    pid_t pid;
    int cols, rows;
    tcell_t *grid;          /* TERM_HIST * cols */

    int hist_top;           /* 视口第一行在 grid 中的下标 */
    int cx, cy;             /* 光标（视口坐标，0 基） */
    int wrap_pending;
    int scroll_top, scroll_bot;
    uint32_t cur_fg, cur_bg;
    int attr;
    int save_cx, save_cy;
    int cursor_on;

    /* 解析器 */
    int st;                 /* 0 普通 1 ESC 2 CSI 3 OSC 4 字符集 */
    int priv;
    int params[16];
    int nparams;
    int pcur, pset;

    /* UTF-8 */
    int u8_need;
    uint32_t u8_cp;

    int started, exited;
    int font_px;
    int view_lines;         /* 向上回看的行数 */
    int dirty;

    /* 软键盘状态 */
    int kb_ctrl, kb_shift, kb_layout;
};

/* ---------------- 颜色 ---------------- */

static const uint32_t PAL16[16] = {
    0x0C0C0C, 0xC50F1F, 0x13A10E, 0xC19C00, 0x0037DA, 0x881798, 0x3A96DD, 0xCCCCCC,
    0x767676, 0xE74856, 0x16C60C, 0xF9F1A5, 0x3B78FF, 0xB4009E, 0x61D6D6, 0xF2F2F2,
};

static uint32_t xterm256(int idx)
{
    if (idx < 0) idx = 0;
    if (idx < 16) return PAL16[idx];
    if (idx < 232) {
        int n = idx - 16;
        int r = n / 36, g = (n / 6) % 6, b = n % 6;
        int R = r ? r * 40 + 55 : 0;
        int G = g ? g * 40 + 55 : 0;
        int B = b ? b * 40 + 55 : 0;
        return ((uint32_t)R << 16) | ((uint32_t)G << 8) | (uint32_t)B;
    }
    int v = 8 + (idx - 232) * 10;
    if (v > 255) v = 255;
    return ((uint32_t)v << 16) | ((uint32_t)v << 8) | (uint32_t)v;
}

/* 粗略东亚宽度：终端里只需要区分 1 与 2 */
static int cp_width(uint32_t cp)
{
    if (cp == 0) return 0;
    if (cp < 0x20) return 0;
    if (cp < 0x7F) return 1;
    if (cp < 0xA0) return 1;
    if ((cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x2E80 && cp <= 0x303E) ||
        (cp >= 0x3041 && cp <= 0x33FF) || (cp >= 0x3400 && cp <= 0x4DBF) ||
        (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0xA000 && cp <= 0xA4CF) ||
        (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF) ||
        (cp >= 0xFE30 && cp <= 0xFE4F) || (cp >= 0xFF00 && cp <= 0xFF60) ||
        (cp >= 0xFFE0 && cp <= 0xFFE6) || (cp >= 0x1F300 && cp <= 0x1F9FF) ||
        (cp >= 0x20000 && cp <= 0x3FFFD))
        return 2;
    return 1;
}

static int utf8_encode(uint32_t cp, char *out)
{
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

/* ---------------- 网格 ---------------- */

static tcell_t *row_edit(term_t *t, int y)
{
    return &t->grid[(size_t)(t->hist_top + y) * t->cols];
}

static tcell_t *row_view(term_t *t, int y)
{
    int idx = t->hist_top - t->view_lines + y;
    if (idx < 0) idx = 0;
    if (idx >= TERM_HIST) idx = TERM_HIST - 1;
    return &t->grid[(size_t)idx * t->cols];
}

static void clear_row(term_t *t, int y)
{
    tcell_t *r = row_edit(t, y);
    for (int i = 0; i < t->cols; i++) {
        r[i].ch = 0;
        r[i].fg = t->cur_fg;
        r[i].bg = t->cur_bg;
        r[i].attr = 0;
    }
}

static void clear_all(term_t *t)
{
    for (int i = 0; i < TERM_HIST; i++) {
        tcell_t *r = &t->grid[(size_t)i * t->cols];
        for (int c = 0; c < t->cols; c++) {
            r[c].ch = 0;
            r[c].fg = t->cur_fg;
            r[c].bg = t->cur_bg;
            r[c].attr = 0;
        }
    }
    t->hist_top = 0;
    t->cx = t->cy = 0;
    t->view_lines = 0;
    t->wrap_pending = 0;
    t->scroll_top = 0;
    t->scroll_bot = t->rows - 1;
}

static void scroll_up(term_t *t, int n)
{
    if (n <= 0) return;
    if (t->scroll_top == 0 && t->scroll_bot == t->rows - 1) {
        for (int i = 0; i < n; i++) {
            if (t->hist_top + t->rows < TERM_HIST) {
                t->hist_top++;
                clear_row(t, t->rows - 1);
            } else {
                memmove(t->grid, t->grid + t->cols,
                        (size_t)(TERM_HIST - 1) * t->cols * sizeof(tcell_t));
                tcell_t *r = &t->grid[(size_t)(TERM_HIST - 1) * t->cols];
                for (int c = 0; c < t->cols; c++) {
                    r[c].ch = 0;
                    r[c].fg = t->cur_fg;
                    r[c].bg = t->cur_bg;
                    r[c].attr = 0;
                }
            }
        }
        if (t->view_lines > 0) t->view_lines = 0;
        return;
    }
    int a = t->scroll_top, b = t->scroll_bot, cnt = b - a + 1;
    if (n > cnt) n = cnt;
    memmove(row_edit(t, a), row_edit(t, a + n), (size_t)(cnt - n) * t->cols * sizeof(tcell_t));
    for (int i = cnt - n; i < cnt; i++) clear_row(t, a + i);
}

static void scroll_down(term_t *t, int n)
{
    if (n <= 0) return;
    int a = t->scroll_top, b = t->scroll_bot, cnt = b - a + 1;
    if (n > cnt) n = cnt;
    memmove(row_edit(t, a + n), row_edit(t, a), (size_t)(cnt - n) * t->cols * sizeof(tcell_t));
    for (int i = 0; i < n; i++) clear_row(t, a + i);
}

static void linefeed(term_t *t)
{
    t->wrap_pending = 0;
    if (t->cy == t->scroll_bot) scroll_up(t, 1);
    else if (t->cy < t->rows - 1) t->cy++;
}

static void put_cell(term_t *t, uint32_t cp)
{
    int w = cp_width(cp);
    if (w == 0) return;
    if (t->wrap_pending) {
        t->cx = 0;
        linefeed(t);
    }
    if (t->cx + w > t->cols) {
        t->cx = 0;
        linefeed(t);
    }
    tcell_t *r = row_edit(t, t->cy);
    r[t->cx].ch = cp;
    r[t->cx].fg = t->cur_fg;
    r[t->cx].bg = t->cur_bg;
    r[t->cx].attr = (uint8_t)t->attr;
    if (w == 2 && t->cx + 1 < t->cols) {
        r[t->cx + 1].ch = 0;
        r[t->cx + 1].fg = t->cur_fg;
        r[t->cx + 1].bg = t->cur_bg;
        r[t->cx + 1].attr = (uint8_t)(t->attr | ATTR_WIDE);
    }
    t->cx += w;
    if (t->cx >= t->cols) {
        t->cx = t->cols - 1;
        t->wrap_pending = 1;
    }
}

/* ---------------- SGR ---------------- */

static void sgr(term_t *t)
{
    if (t->nparams == 0) {
        t->cur_fg = 0;
        t->cur_bg = 0;
        t->attr = 0;
        return;
    }
    for (int i = 0; i < t->nparams; i++) {
        int p = t->params[i];
        switch (p) {
        case 0: t->cur_fg = 0; t->cur_bg = 0; t->attr = 0; break;
        case 1: t->attr |= ATTR_BOLD; break;
        case 2: t->attr |= ATTR_DIM; break;
        case 3: break;
        case 4: t->attr |= ATTR_UL; break;
        case 7: t->attr |= ATTR_REV; break;
        case 22: t->attr &= ~(ATTR_BOLD | ATTR_DIM); break;
        case 23: break;
        case 24: t->attr &= ~ATTR_UL; break;
        case 27: t->attr &= ~ATTR_REV; break;
        case 39: t->cur_fg = 0; break;
        case 49: t->cur_bg = 0; break;
        case 38:
        case 48: {
            int isfg = (p == 38);
            if (i + 1 < t->nparams && t->params[i + 1] == 5 && i + 2 < t->nparams) {
                uint32_t c = xterm256(t->params[i + 2]);
                if (isfg) t->cur_fg = c; else t->cur_bg = c;
                i += 2;
            } else if (i + 1 < t->nparams && t->params[i + 1] == 2 && i + 4 < t->nparams) {
                uint32_t c = ((uint32_t)(t->params[i + 2] & 255) << 16) |
                             ((uint32_t)(t->params[i + 3] & 255) << 8) |
                             (uint32_t)(t->params[i + 4] & 255);
                if (isfg) t->cur_fg = c; else t->cur_bg = c;
                i += 4;
            }
            break;
        }
        default:
            if (p >= 30 && p <= 37) t->cur_fg = PAL16[p - 30];
            else if (p >= 40 && p <= 47) t->cur_bg = PAL16[p - 40];
            else if (p >= 90 && p <= 97) t->cur_fg = PAL16[p - 90 + 8];
            else if (p >= 100 && p <= 107) t->cur_bg = PAL16[p - 100 + 8];
            break;
        }
    }
}

/* ---------------- CSI 派发 ---------------- */

static int pget(term_t *t, int i, int def)
{
    if (i >= t->nparams) return def;
    if (t->params[i] == -1) return def;   /* 空参数 */
    return t->params[i];
}

static void erase_display(term_t *t, int mode)
{
    if (mode == 0) {
        tcell_t *r = row_edit(t, t->cy);
        for (int c = t->cx; c < t->cols; c++) { r[c].ch = 0; r[c].attr = 0; }
        for (int y = t->cy + 1; y < t->rows; y++) clear_row(t, y);
    } else if (mode == 1) {
        for (int y = 0; y < t->cy; y++) clear_row(t, y);
        tcell_t *r = row_edit(t, t->cy);
        for (int c = 0; c <= t->cx && c < t->cols; c++) { r[c].ch = 0; r[c].attr = 0; }
    } else if (mode == 2) {
        for (int y = 0; y < t->rows; y++) clear_row(t, y);
    } else if (mode == 3) {
        clear_all(t);
    }
    t->view_lines = 0;
}

static void erase_line(term_t *t, int mode)
{
    tcell_t *r = row_edit(t, t->cy);
    int from = 0, to = t->cols - 1;
    if (mode == 0) from = t->cx;
    else if (mode == 1) to = t->cx;
    if (to >= t->cols) to = t->cols - 1;
    for (int c = from; c <= to; c++) { r[c].ch = 0; r[c].attr = 0; }
}

static void csi_dispatch(term_t *t, char final)
{
    switch (final) {
    case 'A':
        t->cy -= pget(t, 0, 1);
        if (t->cy < 0) t->cy = 0;
        break;
    case 'B':
    case 'e':
        t->cy += pget(t, 0, 1);
        if (t->cy > t->rows - 1) t->cy = t->rows - 1;
        break;
    case 'C':
    case 'a':
        t->cx += pget(t, 0, 1);
        if (t->cx > t->cols - 1) t->cx = t->cols - 1;
        break;
    case 'D':
        t->cx -= pget(t, 0, 1);
        if (t->cx < 0) t->cx = 0;
        break;
    case 'E':
        t->cy += pget(t, 0, 1);
        if (t->cy > t->rows - 1) t->cy = t->rows - 1;
        t->cx = 0;
        break;
    case 'F':
        t->cy -= pget(t, 0, 1);
        if (t->cy < 0) t->cy = 0;
        t->cx = 0;
        break;
    case 'G':
    case '`':
        t->cx = pget(t, 0, 1) - 1;
        if (t->cx < 0) t->cx = 0;
        if (t->cx > t->cols - 1) t->cx = t->cols - 1;
        break;
    case 'd':
        t->cy = pget(t, 0, 1) - 1;
        if (t->cy < 0) t->cy = 0;
        if (t->cy > t->rows - 1) t->cy = t->rows - 1;
        break;
    case 'H':
    case 'f': {
        int y = pget(t, 0, 1) - 1, x = pget(t, 1, 1) - 1;
        if (y < 0) y = 0;
        if (x < 0) x = 0;
        if (y > t->rows - 1) y = t->rows - 1;
        if (x > t->cols - 1) x = t->cols - 1;
        t->cy = y;
        t->cx = x;
        break;
    }
    case 'J': erase_display(t, pget(t, 0, 0)); break;
    case 'K': erase_line(t, pget(t, 0, 0)); break;
    case 'L':
        if (t->cy >= t->scroll_top && t->cy <= t->scroll_bot) {
            int n = pget(t, 0, 1), a = t->cy, b = t->scroll_bot, cnt = b - a + 1;
            if (n > cnt) n = cnt;
            memmove(row_edit(t, a + n), row_edit(t, a),
                    (size_t)(cnt - n) * t->cols * sizeof(tcell_t));
            for (int i = 0; i < n; i++) clear_row(t, a + i);
        }
        break;
    case 'M':
        if (t->cy >= t->scroll_top && t->cy <= t->scroll_bot) {
            int n = pget(t, 0, 1), a = t->cy, b = t->scroll_bot, cnt = b - a + 1;
            if (n > cnt) n = cnt;
            memmove(row_edit(t, a), row_edit(t, a + n),
                    (size_t)(cnt - n) * t->cols * sizeof(tcell_t));
            for (int i = cnt - n; i < cnt; i++) clear_row(t, a + i);
        }
        break;
    case 'P': {
        int n = pget(t, 0, 1);
        if (n > t->cols - t->cx) n = t->cols - t->cx;
        tcell_t *r = row_edit(t, t->cy);
        memmove(r + t->cx, r + t->cx + n, (size_t)(t->cols - t->cx - n) * sizeof(tcell_t));
        for (int i = t->cols - n; i < t->cols; i++) { r[i].ch = 0; r[i].attr = 0; }
        break;
    }
    case '@': {
        int n = pget(t, 0, 1);
        if (n > t->cols - t->cx) n = t->cols - t->cx;
        tcell_t *r = row_edit(t, t->cy);
        memmove(r + t->cx + n, r + t->cx, (size_t)(t->cols - t->cx - n) * sizeof(tcell_t));
        for (int i = 0; i < n; i++) { r[t->cx + i].ch = 0; r[t->cx + i].attr = 0; }
        break;
    }
    case 'X': {
        int n = pget(t, 0, 1);
        tcell_t *r = row_edit(t, t->cy);
        for (int i = 0; i < n && t->cx + i < t->cols; i++) { r[t->cx + i].ch = 0; r[t->cx + i].attr = 0; }
        break;
    }
    case 'S': scroll_up(t, pget(t, 0, 1)); break;
    case 'T': scroll_down(t, pget(t, 0, 1)); break;
    case 'm': sgr(t); break;
    case 'r': {
        int a = pget(t, 0, 1) - 1, b = pget(t, 1, t->rows) - 1;
        if (a < 0) a = 0;
        if (b > t->rows - 1) b = t->rows - 1;
        if (a < b) { t->scroll_top = a; t->scroll_bot = b; }
        t->cx = 0;
        t->cy = 0;
        break;
    }
    case 's':
        t->save_cx = t->cx;
        t->save_cy = t->cy;
        break;
    case 'u':
        t->cx = t->save_cx;
        t->cy = t->save_cy;
        break;
    case 'h':
    case 'l': {
        int on = (final == 'h');
        for (int i = 0; i < t->nparams; i++) {
            int p = t->params[i];
            if (t->priv && p == 25) t->cursor_on = on;
            if (t->priv && (p == 1049 || p == 1047 || p == 47)) {
                if (on) { for (int y = 0; y < t->rows; y++) clear_row(t, y); t->cx = t->cy = 0; }
            }
        }
        break;
    }
    default:
        break;
    }
}

/* ---------------- 解析 ---------------- */

static void esc_dispatch(term_t *t, char c)
{
    switch (c) {
    case '[': t->st = 2; t->nparams = 0; t->pcur = -1; t->pset = 0; t->priv = 0; break;
    case ']': t->st = 3; break;
    case '(':
    case ')':
    case '*':
    case '+': t->st = 4; break;
    case '7': t->save_cx = t->cx; t->save_cy = t->cy; break;
    case '8': t->cx = t->save_cx; t->cy = t->save_cy; break;
    case 'D': linefeed(t); break;
    case 'E': t->cx = 0; linefeed(t); break;
    case 'M':
        if (t->cy == t->scroll_top) scroll_down(t, 1);
        else if (t->cy > 0) t->cy--;
        break;
    case 'c': clear_all(t); break;
    default: break;
    }
}

void term_feed(term_t *t, const void *data, size_t n)
{
    const unsigned char *p = data;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = p[i];
        t->dirty = 1;

        if (t->st == 3) {           /* OSC：吃到 BEL 或 ST */
            if (c == 0x07) t->st = 0;
            else if (c == 0x1b) t->st = 5;
            continue;
        }
        if (t->st == 5) {           /* OSC 后的 ESC \ */
            t->st = (c == '\\') ? 0 : 3;
            continue;
        }
        if (t->st == 4) { t->st = 0; continue; }   /* 字符集选择：跳过一字节 */

        if (t->st == 2) {           /* CSI */
            if (c >= '0' && c <= '9') {
                if (!t->pset) { t->pcur = 0; t->pset = 1; }
                t->pcur = t->pcur * 10 + (c - '0');
            } else if (c == ';' || c == ':') {
                if (t->nparams < 16) t->params[t->nparams++] = t->pset ? t->pcur : -1;
                t->pcur = -1;
                t->pset = 0;
            } else if (c == '?') {
                t->priv = 1;
            } else if (c >= 0x40 && c <= 0x7e) {
                if (t->nparams < 16) t->params[t->nparams++] = t->pset ? t->pcur : -1;
                t->st = 0;
                csi_dispatch(t, (char)c);
            } else if (c == 0x1b) {
                t->st = 1;
            }
            continue;
        }
        if (t->st == 1) {
            t->st = 0;
            esc_dispatch(t, (char)c);
            continue;
        }
        if (t->st == 6) {          /* 未使用 */
            t->st = 0;
            continue;
        }

        /* UTF-8 / 控制字符 */
        if (t->u8_need) {
            if ((c & 0xC0) == 0x80) {
                t->u8_cp = (t->u8_cp << 6) | (c & 0x3F);
                if (--t->u8_need == 0) put_cell(t, t->u8_cp);
            } else {
                t->u8_need = 0;
                put_cell(t, 0xFFFD);
                i--;    /* 重新处理该字节 */
            }
            continue;
        }
        if (c == 0x1b) { t->st = 1; continue; }
        if (c < 0x20) {
            switch (c) {
            case 0x07: break;                       /* BEL：忽略 */
            case 0x08:
                if (t->cx > 0) t->cx--;
                t->wrap_pending = 0;
                break;
            case 0x09: {
                int nx = (t->cx / 8 + 1) * 8;
                if (nx > t->cols - 1) nx = t->cols - 1;
                t->cx = nx;
                break;
            }
            case 0x0A:
            case 0x0B:
            case 0x0C: linefeed(t); break;
            case 0x0D: t->cx = 0; t->wrap_pending = 0; break;
            default: break;
            }
            continue;
        }
        if (c < 0x80) { put_cell(t, c); continue; }
        if ((c & 0xE0) == 0xC0) { t->u8_cp = c & 0x1F; t->u8_need = 1; }
        else if ((c & 0xF0) == 0xE0) { t->u8_cp = c & 0x0F; t->u8_need = 2; }
        else if ((c & 0xF8) == 0xF0) { t->u8_cp = c & 0x07; t->u8_need = 3; }
        else put_cell(t, 0xFFFD);
    }
}

/* ---------------- 生命周期 ---------------- */

term_t *term_new(void)
{
    term_t *t = calloc(1, sizeof(*t));
    if (!t) return NULL;
    t->fd = -1;
    t->font_px = 24;
    t->cursor_on = 1;
    t->rows = 24;
    t->cols = 80;
    return t;
}

void term_free(term_t *t)
{
    if (!t) return;
    if (t->fd >= 0) {
        if (t->pid > 0 && !t->exited) {
            kill(-t->pid, SIGHUP);
            kill(t->pid, SIGHUP);
        }
        close(t->fd);
    }
    if (t->pid > 0) {
        int st = 0;
        waitpid(t->pid, &st, WNOHANG);
    }
    free(t->grid);
    free(t);
}

static void term_layout(term_t *t, int cols, int rows)
{
    if (cols < 20) cols = 20;
    if (rows < 5) rows = 5;
    if (cols == t->cols && rows == t->rows && t->grid) return;

    tcell_t *old = t->grid;
    int oc = t->cols, orows = t->rows;
    tcell_t *ng = calloc((size_t)TERM_HIST * (size_t)cols, sizeof(tcell_t));
    if (!ng) return;

    for (size_t i = 0; i < (size_t)TERM_HIST * (size_t)cols; i++) {
        ng[i].fg = 0;
        ng[i].bg = 0;
        ng[i].attr = 0;
    }
    /* 保留最后几行内容，避免尺寸变化时整屏空白 */
    if (old) {
        int keep = orows < rows ? orows : rows;
        /* 光标所在行之前的有效内容优先保留；内容不足一屏时从第 0 行开始 */
        int first = (t->cy + 1 > keep) ? (t->cy + 1 - keep) : 0;
        for (int r = 0; r < keep; r++) {
            int sr = first + r;
            if (sr >= orows) break;
            tcell_t *src = &old[(size_t)(t->hist_top + sr) * oc];
            tcell_t *dst = &ng[(size_t)r * cols];
            int c = oc < cols ? oc : cols;
            memcpy(dst, src, (size_t)c * sizeof(tcell_t));
        }
        free(old);
        t->hist_top = 0;
        t->cy = t->cy - first;
        if (t->cy < 0) t->cy = 0;
        if (t->cy >= rows) t->cy = rows - 1;
        if (t->cx >= cols) t->cx = cols - 1;
        if (t->cx < 0) t->cx = 0;
    } else {
        t->hist_top = 0;
        t->cy = 0;
        t->cx = 0;
    }
    t->grid = ng;
    t->cols = cols;
    t->rows = rows;
    t->scroll_top = 0;
    t->scroll_bot = rows - 1;
    t->view_lines = 0;
    t->wrap_pending = 0;

    if (t->fd >= 0) {
        struct winsize ws;
        memset(&ws, 0, sizeof(ws));
        ws.ws_col = (unsigned short)cols;
        ws.ws_row = (unsigned short)rows;
        ioctl(t->fd, TIOCSWINSZ, &ws);
        if (t->pid > 0) kill(t->pid, SIGWINCH);
    }
}

static void term_ensure_grid(term_t *t, int cols, int rows)
{
    if (!t->grid) term_layout(t, cols, rows);
    else if (cols != t->cols || rows != t->rows) term_layout(t, cols, rows);
}

static int spawn_shell(term_t *t)
{
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0) return -1;
    if (grantpt(master) != 0 || unlockpt(master) != 0) { close(master); return -1; }
    char *slave = ptsname(master);
    if (!slave) { close(master); return -1; }

    const char *home = getenv("HOME");
    if (!home || !home[0]) home = "/data/home";

    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        int s = open(slave, O_RDWR);
        if (s < 0) _exit(1);
        ioctl(s, TIOCSCTTY, 0);
        dup2(s, 0);
        dup2(s, 1);
        dup2(s, 2);
        if (s > 2) close(s);
        close(master);

        struct winsize ws;
        memset(&ws, 0, sizeof(ws));
        ws.ws_col = (unsigned short)t->cols;
        ws.ws_row = (unsigned short)t->rows;
        ioctl(0, TIOCSWINSZ, &ws);

        setenv("TERM", "xterm-256color", 1);
        setenv("COLORTERM", "truecolor", 1);
        setenv("LANG", "C.UTF-8", 1);
        setenv("HOME", home, 1);
        setenv("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin", 1);
        setenv("PROMPT_COMMAND", "", 1);
        chdir(home);

        const char *sh = getenv("SHELL");
        if (!sh || !sh[0] || access(sh, X_OK) != 0) sh = "/bin/bash";
        if (access(sh, X_OK) != 0) sh = "/bin/sh";
        execl(sh, sh, "-l", (char *)NULL);
        execl("/bin/sh", "sh", "-l", (char *)NULL);
        _exit(127);
    }
    if (pid < 0) { close(master); return -1; }

    int fl = fcntl(master, F_GETFL);
    fcntl(master, F_SETFL, fl | O_NONBLOCK);
    t->fd = master;
    t->pid = pid;
    t->started = 1;
    t->exited = 0;
    return 0;
}

void term_poll(term_t *t)
{
    if (!t || t->fd < 0 || t->exited) return;
    char buf[8192];
    for (;;) {
        ssize_t n = read(t->fd, buf, sizeof(buf));
        if (n > 0) { term_feed(t, buf, (size_t)n); continue; }
        if (n == 0) { t->exited = 1; break; }
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        if (errno == EINTR) continue;
        t->exited = 1;
        break;
    }
    if (t->exited && t->pid > 0) {
        int st = 0;
        waitpid(t->pid, &st, WNOHANG);
    }
}

int term_running(term_t *t)
{
    return t && t->started && !t->exited;
}

void term_send(term_t *t, const void *data, size_t n)
{
    if (!t || t->fd < 0 || t->exited) return;
    const char *p = data;
    while (n > 0) {
        ssize_t w = write(t->fd, p, n);
        if (w > 0) { p += w; n -= (size_t)w; continue; }
        if (w < 0 && (errno == EAGAIN || errno == EINTR)) {
            struct timespec ts = { 0, 2000000 };
            nanosleep(&ts, NULL);
            continue;
        }
        break;
    }
    t->view_lines = 0;
}

void term_send_str(term_t *t, const char *s)
{
    term_send(t, s, strlen(s));
}

void term_scroll(term_t *t, int lines)
{
    if (!t || !t->grid) return;
    t->view_lines += lines;
    if (t->view_lines < 0) t->view_lines = 0;
    if (t->view_lines > t->hist_top) t->view_lines = t->hist_top;
}

int term_height_hint(term_t *t)
{
    if (!t || t->font_px <= 0) return 0;
    return t->rows * (t->font_px + 4);
}

void term_set_font(term_t *t, int px)
{
    if (!t) return;
    if (px < 8) px = 8;
    t->font_px = px;
}

/* ---------------- 渲染 ---------------- */

static uint32_t cell_bg(const tcell_t *c)
{
    return (c->attr & ATTR_REV) ? c->fg : c->bg;
}

static uint32_t cell_fg(const tcell_t *c)
{
    return (c->attr & ATTR_REV) ? c->bg : c->fg;
}

int term_draw(term_t *t, gfx_t *g, int x, int y, int w, int h)
{
    if (!t || !g) return 0;
    int px = t->font_px;
    /* 图集不是等宽字体：取数字宽度作单元格基准（数字在终端里最密），
     * 字符左对齐绘制，视觉上接近等宽终端，列与列不会互相挤压。 */
    int cw = gfx_text_w_px("0123456789", px) / 10;
    if (cw < px / 3) cw = px / 3;
    if (cw < 4) cw = 4;
    cw += 1;
    int ch = px + 3;
    int cols = (w - 10) / cw;
    int rows = (h - 10) / ch;
    if (cols < 20) cols = 20;
    if (rows < 5) rows = 5;

    term_ensure_grid(t, cols, rows);
    if (!t->started) spawn_shell(t);
    if (!t->grid) return 0;

    const uint32_t bg0 = 0x0B0D11;
    const uint32_t fg0 = 0xE6E8EC;

    gfx_fill(g, x, y, w, h, bg0);

    int ox = x + 5, oy = y + 5;
    for (int r = 0; r < t->rows && (oy + r * ch + ch) <= y + h; r++) {
        tcell_t *row = row_view(t, r);
        for (int c = 0; c < t->cols; c++) {
            uint32_t b = cell_bg(&row[c]);
            if (b && b != bg0)
                gfx_fill(g, ox + c * cw, oy + r * ch, cw, ch, b);
            if (row[c].attr & ATTR_UL)
                gfx_fill(g, ox + c * cw, oy + r * ch + ch - 2, cw, 1,
                         cell_fg(&row[c]) ? cell_fg(&row[c]) : fg0);
        }
    }

    for (int r = 0; r < t->rows && (oy + r * ch + ch) <= y + h; r++) {
        tcell_t *row = row_view(t, r);
        for (int c = 0; c < t->cols; c++) {
            uint32_t cp = row[c].ch;
            if (!cp) continue;
            char b[8];
            int n = utf8_encode(cp, b);
            b[n] = '\0';
            uint32_t fg = cell_fg(&row[c]);
            if (!fg) fg = fg0;
            if (row[c].attr & ATTR_DIM) fg = gfx_mix(fg, bg0, 110);
            int tw = gfx_text_w_px(b, px);
            int tx = ox + c * cw + 1;
            if (cp_width(cp) == 2 || tw >= cw) tx = ox + c * cw;
            gfx_text_px(g, tx, oy + r * ch + 2, b, fg, px);
            if (row[c].attr & ATTR_BOLD)
                gfx_text_px(g, tx + 1, oy + r * ch + 2, b, fg, px);
        }
    }

    /* 光标：只在看最新输出时显示 */
    if (t->cursor_on && t->view_lines == 0 && !t->exited) {
        int cx = ox + t->cx * cw, cy = oy + t->cy * ch;
        gfx_fill_a(g, cx, cy + 1, cw - 1, ch - 2, 0x9FB4FF, 96);
        tcell_t *c = &row_edit(t, t->cy)[t->cx];
        if (c->ch) {
            char b[8];
            int n = utf8_encode(c->ch, b);
            b[n] = '\0';
            gfx_text_px(g, cx + 1, cy + 2, b, 0x0B0D11, px);
        }
    }

    if (t->view_lines > 0) {
        char b[64];
        snprintf(b, sizeof(b), "回看 %d 行 · 向下滑回底", t->view_lines);
        int bw = gfx_text_w_px(b, 14) + 20;
        gfx_round(g, x + w - bw - 8, y + 8, bw, 26, 13, 0x232733);
        gfx_text_px(g, x + w - bw + 2, y + 14, b, 0xC9D2E4, 14);
    } else if (t->exited) {
        gfx_text_px(g, x + 12, y + h - 28, "会话已结束", 0xE5484D, 16);
    }
    return 0;
}

/* ---------------- 键盘 ---------------- */

enum {
    TK_ESC = -1, TK_TAB = -2, TK_CTRL = -3, TK_SHIFT = -4, TK_LEFT = -5,
    TK_RIGHT = -6, TK_UP = -7, TK_DOWN = -8, TK_ENTER = -9, TK_BACK = -10,
    TK_LAYOUT = -11, TK_PGUP = -12, TK_PGDN = -13,
};

static const char *KB_LOWER[4] = { "1234567890", "qwertyuiop", "asdfghjkl'", "zxcvbnm" };
static const char *KB_UPPER[4] = { "1234567890", "QWERTYUIOP", "ASDFGHJKL\"", "ZXCVBNM" };
static const char *KB_SYMBOL[4] = { "!@#$%^&*()", "-_=+[]{}\\|", ";:\"`~<>/?", ",.:;!?" };

static void send_special(term_t *t, int code)
{
    switch (code) {
    case TK_ESC: term_send(t, "\x1b", 1); break;
    case TK_TAB: term_send(t, "\t", 1); break;
    case TK_ENTER: term_send(t, "\r", 1); break;
    case TK_BACK: term_send(t, "\x7f", 1); break;
    case TK_LEFT: term_send(t, "\x1b[D", 3); break;
    case TK_RIGHT: term_send(t, "\x1b[C", 3); break;
    case TK_UP: term_send(t, "\x1b[A", 3); break;
    case TK_DOWN: term_send(t, "\x1b[B", 3); break;
    case TK_PGUP: term_scroll(t, -(t->rows - 2)); break;
    case TK_PGDN: term_scroll(t, t->rows - 2); break;
    default: break;
    }
}

int term_kb_height(int h)
{
    int kh = h * 46 / 100;
    if (kh < 340) kh = 340;
    if (kh > 520) kh = 520;
    return kh;
}

int term_keyboard(ui_t *u, term_t *t, int x, int y, int w, int h)
{
    if (!u || !t || h < 120) return 0;
    int rows = 6;
    int kh = h / rows;
    int kw = w / 10;
    int hit = 0;

    gfx_t *g = u->g;
    gfx_fill(g, x, y, w, h, 0x1A1D24);
    gfx_fill_a(g, x, y, w, 1, 0x2E3340, 255);

    /* 第一行：Esc Tab Ctrl ↑ ↓ ← → PgUp PgDn ⌫ */
    const char *f1[10] = { "Esc", "Tab", "Ctrl", "↑", "↓", "←", "→", "PgUp", "PgDn", "Del" };
    int f1c[10] = { TK_ESC, TK_TAB, TK_CTRL, TK_UP, TK_DOWN, TK_LEFT, TK_RIGHT, TK_PGUP, TK_PGDN, TK_BACK };
    for (int i = 0; i < 10; i++) {
        int kx = x + i * kw, ky = y;
        int on = (f1c[i] == TK_CTRL && t->kb_ctrl) || (f1c[i] == TK_SHIFT && t->kb_shift);
        uint32_t bg = on ? 0x3E7BFF : 0x272B34;
        gfx_round(g, kx + 2, ky + 3, kw - 4, kh - 6, 8, bg);
        gfx_text_center_px(g, kx + kw / 2, ky + 3 + (kh - 6 - 20) / 2, f1[i],
                           on ? 0xFFFFFF : 0xD3D8E2, 20);
        if (ui_hit(u, kx, ky, kw, kh)) {
            if (f1c[i] == TK_CTRL) t->kb_ctrl = !t->kb_ctrl;
            else if (f1c[i] == TK_SHIFT) t->kb_shift = !t->kb_shift;
            else send_special(t, f1c[i]);
            hit = 1;
        }
    }

    const char **rowsrc = t->kb_layout == 0 ? (t->kb_shift ? KB_UPPER : KB_LOWER) : KB_SYMBOL;
    for (int r = 0; r < 4; r++) {
        const char *line = rowsrc[r];
        int n = (int)strlen(line);
        int ky = y + (r + 1) * kh;
        for (int i = 0; i < n && i < 10; i++) {
            int kx = x + i * kw;
            char lbl[2] = { line[i], 0 };
            gfx_round(g, kx + 2, ky + 3, kw - 4, kh - 6, 8, 0x30353F);
            gfx_text_center_px(g, kx + kw / 2, ky + 3 + (kh - 6 - 22) / 2, lbl, 0xF0F2F6, 22);
            if (ui_hit(u, kx, ky, kw, kh)) {
                unsigned char c = (unsigned char)line[i];
                if (t->kb_ctrl) {
                    unsigned char cc = (unsigned char)(c & 0x1F);
                    term_send(t, &cc, 1);
                    t->kb_ctrl = 0;
                } else {
                    term_send(t, &c, 1);
                }
                hit = 1;
            }
        }
        /* 第四行右侧 Shift 占三格 */
        if (r == 3) {
            int kx = x + 7 * kw;
            gfx_round(g, kx + 2, ky + 3, kw * 3 - 4, kh - 6, 8,
                      t->kb_shift ? 0x3E7BFF : 0x272B34);
            gfx_text_center_px(g, kx + kw * 3 / 2, ky + 3 + (kh - 6 - 20) / 2, "Shift",
                               t->kb_shift ? 0xFFFFFF : 0xD3D8E2, 18);
            if (ui_hit(u, kx, ky, kw * 3, kh)) { t->kb_shift = !t->kb_shift; hit = 1; }
        }
    }

    /* 末行：布局 / 空格 / 回车 */
    int ky = y + 5 * kh;
    gfx_round(g, x + 2, ky + 3, kw * 2 - 4, kh - 6, 8, 0x272B34);
    gfx_text_center_px(g, x + kw, ky + 3 + (kh - 6 - 20) / 2,
                       t->kb_layout == 0 ? "123" : "ABC", 0xD3D8E2, 20);
    if (ui_hit(u, x, ky, kw * 2, kh)) { t->kb_layout = !t->kb_layout; hit = 1; }

    gfx_round(g, x + kw * 2 + 2, ky + 3, kw * 5 - 4, kh - 6, 8, 0x30353F);
    gfx_text_center_px(g, x + kw * 2 + (kw * 5) / 2, ky + 3 + (kh - 6 - 20) / 2, "空格", 0xF0F2F6, 20);
    if (ui_hit(u, x + kw * 2, ky, kw * 5, kh)) { term_send(t, " ", 1); hit = 1; }

    gfx_round(g, x + kw * 7 + 2, ky + 3, kw * 3 - 4, kh - 6, 8, 0x3E7BFF);
    gfx_text_center_px(g, x + kw * 7 + (kw * 3) / 2, ky + 3 + (kh - 6 - 20) / 2, "回车", 0xFFFFFF, 20);
    if (ui_hit(u, x + kw * 7, ky, kw * 3, kh)) { term_send(t, "\r", 1); hit = 1; }

    return hit;
}

void term_key(term_t *t, int keycode)
{
    if (!t) return;
    switch (keycode) {
    case KEY_ENTER:
    case KEY_KPENTER: term_send(t, "\r", 1); return;
    case KEY_BACKSPACE: term_send(t, "\x7f", 1); return;
    case KEY_TAB: term_send(t, "\t", 1); return;
    case KEY_ESC: term_send(t, "\x1b", 1); return;
    case KEY_UP: term_send(t, "\x1b[A", 3); return;
    case KEY_DOWN: term_send(t, "\x1b[B", 3); return;
    case KEY_RIGHT: term_send(t, "\x1b[C", 3); return;
    case KEY_LEFT: term_send(t, "\x1b[D", 3); return;
    case KEY_HOME: term_send(t, "\x1b[H", 3); return;
    case KEY_END: term_send(t, "\x1b[F", 3); return;
    case KEY_PAGEUP: term_scroll(t, -(t->rows - 2)); return;
    case KEY_PAGEDOWN: term_scroll(t, t->rows - 2); return;
    default: break;
    }
    uint32_t c = ui_key_ascii(keycode);
    if (c) {
        char b[4];
        int n = utf8_encode(c, b);
        term_send(t, b, (size_t)n);
    }
}

int term_line_text(term_t *t, int row, char *out, size_t n)
{
    if (!t || !t->grid || row < 0 || row >= t->rows) { if (n) out[0] = '\0'; return 0; }
    tcell_t *r = row_edit(t, row);
    size_t used = 0;
    for (int c = 0; c < t->cols && used + 8 < n; c++) {
        if (!r[c].ch) { if (used) out[used++] = ' '; continue; }
        used += (size_t)utf8_encode(r[c].ch, out + used);
    }
    while (used > 0 && out[used - 1] == ' ') used--;
    out[used] = '\0';
    return (int)used;
}
