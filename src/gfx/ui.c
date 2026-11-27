/* ProperOS 25.0.0 Mobile · 即时模式控件层实现
 *
 * 开发者：ByUsi Studio · 北啊呢
 */
#define _GNU_SOURCE
#include "ui.h"

#include <math.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int64_t mono_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void ui_init(ui_t *u, gfx_t *g)
{
    memset(u, 0, sizeof(*u));
    u->g = g;
    u->px = -1;
    u->py = -1;
    u->focus = -1;
    u->block_from_y = -1;
    u->now_ms = mono_ms();
}

void ui_clear(ui_t *u)
{
    u->tap_pending = 0;
    u->long_pending = 0;
    u->swipe_pending = 0;
    u->move_pending = 0;
    u->key_pending = 0;
}

void ui_feed(ui_t *u, const in_event_t *ev)
{
    switch (ev->kind) {
    case IN_DOWN:
        u->down = 1;
        u->px = ev->x; u->py = ev->y;
        break;
    case IN_MOVE:
        u->px = ev->x; u->py = ev->y;
        u->move_pending = 1;
        break;
    case IN_UP:
        u->down = 0;
        break;
    case IN_TAP:
        u->px = ev->x; u->py = ev->y;
        u->tap_pending = 1;
        u->tap_x = ev->x; u->tap_y = ev->y;
        break;
    case IN_LONG:
        u->px = ev->x; u->py = ev->y;
        u->long_pending = 1;
        u->long_x = ev->x; u->long_y = ev->y;
        break;
    case IN_SWIPE:
        u->swipe_pending = 1;
        u->swipe_dx = ev->dx; u->swipe_dy = ev->dy;
        u->px = ev->x; u->py = ev->y;
        break;
    case IN_KEY:
        u->key_pending = 1;
        u->key_code = ev->key;
        break;
    default:
        break;
    }
}

void ui_begin(ui_t *u)
{
    u->now_ms = mono_ms();
}

void ui_end(ui_t *u)
{
    /* 未消费的点击/长按在帧末丢弃，避免穿透到下一帧 */
    ui_clear(u);
}

int ui_hit(ui_t *u, int x, int y, int w, int h)
{
    if (!u->tap_pending) return 0;
    if (u->block_from_y >= 0 && u->tap_y > u->block_from_y) return 0;
    if (u->tap_x < x || u->tap_y < y || u->tap_x >= x + w || u->tap_y >= y + h) return 0;
    u->tap_pending = 0;
    return 1;
}

int ui_hit_long(ui_t *u, int x, int y, int w, int h)
{
    if (!u->long_pending) return 0;
    if (u->block_from_y >= 0 && u->long_y > u->block_from_y) return 0;
    if (u->long_x < x || u->long_y < y || u->long_x >= x + w || u->long_y >= y + h) return 0;
    u->long_pending = 0;
    return 1;
}

int ui_hover(ui_t *u, int x, int y, int w, int h)
{
    return u->px >= x && u->py >= y && u->px < x + w && u->py < y + h;
}

int ui_take_key(ui_t *u)
{
    if (!u->key_pending) return 0;
    u->key_pending = 0;
    return u->key_code;
}

/* ================= 基础块 ================= */

void ui_card(ui_t *u, int x, int y, int w, int h, int r)
{
    uint32_t bg = u->dark ? C_DARK_CARD : C_SURFACE;
    if (!u->dark) gfx_shadow(u->g, x, y, w, h, r, 14, 26);
    gfx_round(u->g, x, y, w, h, r, bg);
}

void ui_dim(ui_t *u, int alpha)
{
    gfx_fill_a(u->g, 0, 0, u->g->w, u->g->h, u->dark ? 0x000000 : 0x0B1220, alpha);
}

/* ================= 按钮 ================= */

int ui_button(ui_t *u, int x, int y, int w, int h, const char *label, btn_kind_t kind)
{
    int hov = ui_hover(u, x, y, w, h);
    int hit = ui_hit(u, x, y, w, h);
    int r = h / 2;
    if (r > 26) r = 26;

    uint32_t bg, fg;
    switch (kind) {
    case BTN_PRIMARY:   bg = C_PRIMARY; fg = C_WHITE; break;
    case BTN_DANGER:    bg = C_DANGER; fg = C_WHITE; break;
    case BTN_SECONDARY: bg = u->dark ? 0x2A2F3A : 0xEDF1F8; fg = u->dark ? C_WHITE : C_TEXT; break;
    case BTN_TONAL:     bg = u->dark ? 0x24304A : 0xE4EDFF; fg = C_PRIMARY; break;
    default:            bg = 0; fg = C_PRIMARY; break;
    }

    if (kind == BTN_GHOST) {
        if (hov || u->down) {
            gfx_round_a(u->g, x, y, w, h, r, C_PRIMARY, hov ? 20 : 32);
        }
        gfx_text_center(u->g, x + w / 2, y + (h - gfx_text_h(1)) / 2, label, fg, 1);
        return hit;
    }

    if (kind == BTN_PRIMARY) gfx_shadow(u->g, x, y, w, h, r, 10, hov ? 34 : 22);
    gfx_round(u->g, x, y, w, h, r, bg);
    if (u->down && hov) {
        gfx_round_a(u->g, x, y, w, h, r, 0x000000, 18);
    }
    gfx_text_center(u->g, x + w / 2, y + (h - gfx_text_h(1)) / 2, label, fg, 1);
    return hit;
}

int ui_button_icon(ui_t *u, int x, int y, int w, int h, const char *icon,
                   const char *label, btn_kind_t kind)
{
    int hit = ui_button(u, x, y, w, h, "", kind);
    uint32_t fg = (kind == BTN_PRIMARY || kind == BTN_DANGER) ? C_WHITE : C_PRIMARY;
    int isize = h * 40 / 100;
    int ix = x + w / 2 - isize / 2;
    int iy = y + (h - isize) / 2 - (label && label[0] ? 6 : 0);
    if (icon) gfx_icon(u->g, icon, ix, iy, isize, fg);
    if (label && label[0])
        gfx_text_center(u->g, x + w / 2, iy + isize + 2, label, fg, 1);
    return hit;
}

int ui_icon_button(ui_t *u, int x, int y, int size, const char *icon, uint32_t fg,
                   uint32_t bg, int round)
{
    int hov = ui_hover(u, x, y, size, size);
    int hit = ui_hit(u, x, y, size, size);
    int r = round >= 0 ? round : size / 2;
    if (bg) {
        gfx_round(u->g, x, y, size, size, r, bg);
        if (hov && u->down) gfx_round_a(u->g, x, y, size, size, r, 0x000000, 16);
        else if (hov) gfx_round_a(u->g, x, y, size, size, r, 0xFFFFFF, 12);
    } else if (hov) {
        gfx_round_a(u->g, x, y, size, size, r, C_PRIMARY, u->down ? 34 : 20);
    }
    int isz = size * 56 / 100;
    gfx_icon(u->g, icon, x + (size - isz) / 2, y + (size - isz) / 2, isz, fg);
    return hit;
}

/* ================= 列表 ================= */

int ui_list(ui_t *u, int x, int y, int w, int h, const ui_item_t *items, int n, int selected)
{
    int row_h = h / (n > 0 ? n : 1);
    if (row_h > 88) row_h = 88;
    int used = row_h * n;
    int oy = y + (h - used) / 2;      /* 行数少时整组垂直居中 */
    int ret = -1;
    for (int i = 0; i < n; i++) {
        int ry = oy + i * row_h;
        int hov = ui_hover(u, x, ry, w, row_h);
        int hit = ui_hit(u, x, ry, w, row_h);
        int cur = (i == selected);
        if (cur) {
            gfx_round(u->g, x, ry + 3, w, row_h - 6, 14, u->dark ? 0x24304A : 0xE9F0FF);
        } else if (hov) {
            gfx_round_a(u->g, x, ry + 3, w, row_h - 6, 14,
                        u->dark ? 0xFFFFFF : 0x0B1220, u->dark ? 10 : 6);
        }
        int ty = ry + row_h / 2 - gfx_text_h(1) / 2;
        int tx = x + 20;
        if (items[i].icon) {
            int isz = row_h - 26 > 54 ? 54 : row_h - 26;
            if (isz < 20) isz = 20;
            gfx_round(u->g, x + 16, ry + (row_h - isz) / 2, isz, isz, isz / 3,
                      items[i].tint ? items[i].tint : C_PRIMARY);
            gfx_icon(u->g, items[i].icon, x + 16 + isz / 5, ry + (row_h - isz) / 2 + isz / 5,
                     isz * 3 / 5, C_WHITE);
            tx = x + 16 + isz + 14;
        }
        uint32_t tc = u->dark ? C_WHITE : C_TEXT;
        if (items[i].sub && items[i].sub[0] && row_h >= 72) {
            gfx_text_fit(u->g, tx, ry + row_h / 2 - gfx_text_h(1) - 2, w - (tx - x) - 60,
                         items[i].title, tc, 1);
            gfx_text_fit(u->g, tx, ry + row_h / 2 + 2, w - (tx - x) - 60,
                         items[i].sub, C_TEXT_3, 1);
        } else {
            gfx_text_fit(u->g, tx, ty, w - (tx - x) - 60, items[i].title, tc, 1);
        }
        if (cur) {
            gfx_circle(u->g, x + w - 28, ry + row_h / 2, 8, C_PRIMARY);
            gfx_icon(u->g, "check", x + w - 36, ry + row_h / 2 - 8, 16, C_WHITE);
        }
        if (hit) ret = i;
    }
    return ret;
}

/* ================= 开关 / 滑块 ================= */

int ui_switch(ui_t *u, int x, int y, int on, int *out_on)
{
    int w = 52, h = 32;
    int hit = ui_hit(u, x - 6, y - 6, w + 12, h + 12);
    uint32_t bg = on ? C_PRIMARY : (u->dark ? 0x3A3F4A : 0xD4D9E2);
    gfx_round(u->g, x, y, w, h, h / 2, bg);
    int kx = on ? x + w - h + 4 : x + 4;
    gfx_circle(u->g, kx + (h - 8) / 2, y + h / 2, (h - 8) / 2, C_WHITE);
    if (hit) { on = !on; if (out_on) *out_on = on; return 1; }
    if (out_on) *out_on = on;
    return 0;
}

int ui_slider(ui_t *u, int x, int y, int w, int value, int *out_value)
{
    int h = 8;
    if (value < 0) value = 0;
    if (value > 100) value = 100;
    gfx_round(u->g, x, y, w, h, h / 2, u->dark ? 0x333844 : 0xE1E5EC);
    int fw = w * value / 100;
    if (fw > 0) gfx_round(u->g, x, y, fw, h, h / 2, C_PRIMARY);
    gfx_circle(u->g, x + fw, y + h / 2, 11, C_WHITE);
    gfx_ring(u->g, x + fw, y + h / 2, 11, 2, C_PRIMARY, 120);

    int hit = 0;
    if (u->down && u->py >= y - 22 && u->py <= y + h + 22 &&
        u->px >= x - 16 && u->px <= x + w + 16) {
        int v = (u->px - x) * 100 / (w > 0 ? w : 1);
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        value = v;
        hit = 1;
    }
    if (out_value) *out_value = value;
    return hit;
}

/* ================= 输入框 ================= */

uint32_t ui_key_ascii(int code)
{
    if (code >= KEY_A && code <= KEY_Z) return (uint32_t)('a' + (code - KEY_A));
    if (code >= KEY_1 && code <= KEY_9) return (uint32_t)('1' + (code - KEY_1));
    if (code == KEY_0) return '0';
    switch (code) {
    case KEY_SPACE: return ' ';
    case KEY_MINUS: return '-';
    case KEY_DOT: return '.';
    case KEY_SLASH: return '/';
    case KEY_COMMA: return ',';
    case KEY_EQUAL: return '=';
    case KEY_SEMICOLON: return ';';
    case KEY_APOSTROPHE: return '\'';
    case KEY_LEFTBRACE: return '[';
    case KEY_RIGHTBRACE: return ']';
    case KEY_BACKSLASH: return '\\';
    case KEY_GRAVE: return '`';
    case KEY_KP1: return '1';
    case KEY_KP2: return '2';
    case KEY_KP3: return '3';
    case KEY_KP4: return '4';
    case KEY_KP5: return '5';
    case KEY_KP6: return '6';
    case KEY_KP7: return '7';
    case KEY_KP8: return '8';
    case KEY_KP9: return '9';
    case KEY_KP0: return '0';
    case KEY_KPDOT: return '.';
    case KEY_KPMINUS: return '-';
    default: return 0;
    }
}

static void buf_backspace(char *buf)
{
    size_t len = strlen(buf);
    if (len == 0) return;
    size_t i = len - 1;
    while (i > 0 && ((unsigned char)buf[i] & 0xC0) == 0x80) i--;
    buf[i] = '\0';
}

int ui_input(ui_t *u, int id, int x, int y, int w, int h, char *buf, int cap,
             const char *placeholder, int password, const char *icon)
{
    int hov = ui_hover(u, x, y, w, h);
    int clicked = ui_hit(u, x, y, w, h);
    if (clicked) u->focus = id;

    int active = (u->focus == id);
    gfx_round(u->g, x, y, w, h, 14, u->dark ? 0x242A35 : 0xF1F3F8);
    if (active) gfx_round_outline(u->g, x, y, w, h, 14, 2, C_PRIMARY);
    else if (hov) gfx_round_outline(u->g, x, y, w, h, 14, 1, u->dark ? 0x3B4250 : 0xD9DEE8);

    int tx = x + 16;
    if (icon) {
        gfx_icon(u->g, icon, x + 14, y + (h - 20) / 2, 20, C_TEXT_3);
        tx = x + 46;
    }

    /* 键盘输入 */
    if (active && u->key_pending) {
        int code = u->key_code;
        u->key_pending = 0;
        if (code == KEY_BACKSPACE) {
            buf_backspace(buf);
        } else if (code != KEY_ENTER && code != KEY_KPENTER) {
            uint32_t ch = ui_key_ascii(code);
            if (ch) {
                size_t len = strlen(buf);
                if ((int)len + 1 < cap) { buf[len] = (char)ch; buf[len + 1] = '\0'; }
            }
        }
    }

    char disp[256];
    int len = (int)strlen(buf);
    if (len == 0) {
        gfx_text(u->g, tx, y + (h - gfx_text_h(1)) / 2, placeholder ? placeholder : "",
                 C_TEXT_3, 1);
    } else if (password) {
        int n = len > 24 ? 24 : len;
        for (int i = 0; i < n; i++) disp[i] = '*';
        disp[n] = '\0';
        gfx_text(u->g, tx, y + (h - gfx_text_h(1)) / 2, disp, C_TEXT, 1);
    } else {
        gfx_text_fit(u->g, tx, y + (h - gfx_text_h(1)) / 2, w - (tx - x) - 16, buf, C_TEXT, 1);
    }

    /* 光标 */
    if (active && ((u->now_ms / 500) & 1)) {
        int cw = 2;
        int cx = x + w - 14;
        if (!password && len > 0) {
            int tw = gfx_text_w(buf, 1);
            cx = tx + (tw > w - (tx - x) - 30 ? w - (tx - x) - 24 : tw) + 1;
        }
        gfx_fill(u->g, cx, y + h / 2 - gfx_text_h(1) / 2, cw, gfx_text_h(1), C_PRIMARY);
    }
    return clicked;
}

/* ================= 进度 ================= */

void ui_progress(ui_t *u, int x, int y, int w, int h, int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    gfx_round(u->g, x, y, w, h, h / 2, u->dark ? 0x2A2F3A : 0xE3E7EF);
    int fw = w * pct / 100;
    if (fw > h) {
        gfx_round(u->g, x, y, fw, h, h / 2, C_PRIMARY);
        gfx_round(u->g, x, y, fw, h / 2, h / 4, gfx_lighten(C_PRIMARY, 40));
    } else if (fw > 0) {
        gfx_round(u->g, x, y, h, h, h / 2, C_PRIMARY);
    }
}

void ui_progress_ring(ui_t *u, int cx, int cy, int r, int thick, int pct, uint32_t color)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    gfx_ring(u->g, cx, cy, r, thick, u->dark ? 0x2A2F3A : 0xE3E7EF, 255);
    float end = -90.f + 360.f * (float)pct / 100.f;
    int pad = r + thick;
    for (int j = cy - pad; j <= cy + pad; j++) {
        for (int i = cx - pad; i <= cx + pad; i++) {
            float dx = (float)i + 0.5f - (float)cx;
            float dy = (float)j + 0.5f - (float)cy;
            float d = sqrtf(dx * dx + dy * dy);
            float cov = 1.f - fabsf(d - (float)r) / ((float)thick * 0.5f);
            if (cov <= 0.f) continue;
            float ang = atan2f(dy, dx) * 180.f / (float)M_PI;
            if (ang < -90.f) ang += 360.f;
            if (ang > end) continue;
            if (cov > 1.f) cov = 1.f;
            gfx_px_a(u->g, i, j, color, (int)(cov * 255.f));
        }
    }
}

/* ================= 软键盘 ================= */

static const char *KB_ROWS_LOWER[3] = { "qwertyuiop", "asdfghjkl", "zxcvbnm" };
static const char *KB_ROWS_UPPER[3] = { "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM" };
static const char *KB_ROWS_NUM[3]   = { "1234567890", "-/:;()¥&@", ".,?!'\"#+" };

static void kb_key(ui_t *u, int x, int y, int w, int h, const char *label, int highlight, int *pressed)
{
    int hit = ui_hit(u, x, y, w, h);
    uint32_t bg = highlight ? C_PRIMARY : (u->dark ? 0x2C313C : 0xFFFFFF);
    uint32_t fg = highlight ? C_WHITE : (u->dark ? C_WHITE : C_TEXT);
    if (u->down && ui_hover(u, x, y, w, h)) bg = highlight ? C_PRIMARY_D : (u->dark ? 0x3A404D : 0xE8ECF5);
    gfx_round(u->g, x + 2, y + 3, w - 4, h - 6, 8, bg);
    if (!highlight && !u->dark) gfx_round_outline(u->g, x + 2, y + 3, w - 4, h - 6, 8, 1, 0xE2E6EE);
    gfx_text_center(u->g, x + w / 2, y + h / 2 - gfx_text_h(1) / 2, label, fg, 1);
    if (hit) *pressed = 1;
}

int ui_keyboard(ui_t *u, int x, int y, int w, int h, char *buf, int cap)
{
    int done = 0;
    int kw = w / 10;
    int kh = h / 4;
    const char **rows = u->kb_layout == 0 ? (u->kb_shift ? KB_ROWS_UPPER : KB_ROWS_LOWER) : KB_ROWS_NUM;
    size_t len = strlen(buf);

    for (int r = 0; r < 3; r++) {
        const char *row = rows[r];
        int n = (int)strlen(row);
        int total = n;
        int off = 0;
        if (r == 1) off = kw / 2;
        if (r == 2) total = 10;      /* 1.5 + 7 + 1.5 个标准键宽 */
        int kx = x + off + (w - total * kw) / 2;
        if (r == 2) {
            /* shift */
            int p = 0;
            kb_key(u, kx, y + r * kh, kw * 3 / 2, kh, u->kb_shift ? "↑" : "↑", 0, &p);
            if (p) u->kb_shift = !u->kb_shift;
            kx += kw * 3 / 2;
        }
        for (int i = 0; i < n; i++) {
            char lbl[2] = { row[i], 0 };
            int p = 0;
            kb_key(u, kx + i * kw, y + r * kh, kw, kh, lbl, 0, &p);
            if (p && (int)len + 1 < cap) {
                buf[len++] = row[i];
                buf[len] = '\0';
                if (u->kb_shift && u->kb_layout == 0) { u->kb_shift = 0; }
            }
        }
        if (r == 2) {
            int bx = kx + n * kw;
            int p = 0;
            kb_key(u, bx, y + r * kh, kw * 3 / 2, kh, "←", 0, &p);
            if (p && len > 0) {
                size_t k = len - 1;
                while (k > 0 && ((unsigned char)buf[k] & 0xC0) == 0x80) k--;
                buf[k] = '\0';
                len = k;
            }
        }
    }

    /* 第四行：123 / 空格 / . / 完成 */
    int ry = y + 3 * kh;
    int p = 0;
    kb_key(u, x, ry, kw * 2, kh, u->kb_layout == 0 ? "123" : "ABC", 0, &p);
    if (p) u->kb_layout = !u->kb_layout;

    p = 0;
    kb_key(u, x + kw * 2, ry, kw * 5, kh, "空格", 0, &p);
    if (p && (int)len + 1 < cap) { buf[len++] = ' '; buf[len] = '\0'; }

    p = 0;
    kb_key(u, x + kw * 7, ry, kw, kh, ".", 0, &p);
    if (p && (int)len + 1 < cap) { buf[len++] = '.'; buf[len] = '\0'; }

    p = 0;
    kb_key(u, x + kw * 8, ry, kw * 2, kh, "完成", 1, &p);
    if (p) done = 1;

    return done;
}
