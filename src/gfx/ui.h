/* ProperOS 2.5 Mobile · 即时模式控件层
 *
 * 事件驱动的 immediate-mode UI：每帧按数据重绘，控件函数返回"本次是否被点击"。
 * 不保存控件树，布局由调用方以绝对坐标给出，适合固定版式的手机界面。
 *
 * 开发者：ByUsi Studio · 北啊呢
 */
#ifndef PROPEROS_UI_H
#define PROPEROS_UI_H

#include "gfx.h"
#include "input.h"

typedef enum {
    BTN_PRIMARY = 0,
    BTN_SECONDARY,
    BTN_GHOST,
    BTN_DANGER,
    BTN_TONAL,
} btn_kind_t;

typedef struct {
    const char *title;
    const char *sub;
    const char *icon;       /* gfx_icon id，可为 NULL */
    uint32_t tint;          /* 图标底色 */
} ui_item_t;

typedef struct {
    gfx_t *g;
    int px, py;             /* 指针位置 */
    int down;
    int tap_pending;
    int tap_x, tap_y;
    int long_pending;
    int long_x, long_y;
    int swipe_pending;
    int swipe_dx, swipe_dy;
    int move_pending;
    int key_pending;
    int key_code;
    int dark;
    int focus;              /* 输入框焦点 id */
    int block_from_y;       /* >0 时，该 y 以下不响应点击（模态层遮挡） */
    int64_t now_ms;

    /* 软键盘状态 */
    int kb_layout;
    int kb_shift;
    char *kb_buf;
    int kb_cap;
    int kb_len;
} ui_t;

void ui_init(ui_t *u, gfx_t *g);
void ui_feed(ui_t *u, const in_event_t *ev);
void ui_begin(ui_t *u);
void ui_end(ui_t *u);
int  ui_take_key(ui_t *u);
void ui_clear(ui_t *u);

/* 命中测试：返回 1 表示本次点击落在矩形内（会消费该点击） */
int  ui_hit(ui_t *u, int x, int y, int w, int h);
int  ui_hit_long(ui_t *u, int x, int y, int w, int h);
int  ui_hover(ui_t *u, int x, int y, int w, int h);

/* ---------------- 控件 ---------------- */
int  ui_button(ui_t *u, int x, int y, int w, int h, const char *label, btn_kind_t kind);
int  ui_button_icon(ui_t *u, int x, int y, int w, int h, const char *icon,
                    const char *label, btn_kind_t kind);
int  ui_icon_button(ui_t *u, int x, int y, int size, const char *icon, uint32_t fg,
                    uint32_t bg, int round);
int  ui_list(ui_t *u, int x, int y, int w, int h, const ui_item_t *items, int n, int selected);
int  ui_switch(ui_t *u, int x, int y, int on, int *out_on);
int  ui_slider(ui_t *u, int x, int y, int w, int value, int *out_value);
int  ui_input(ui_t *u, int id, int x, int y, int w, int h, char *buf, int cap,
              const char *placeholder, int password, const char *icon);
/* 把 evdev 键码转成可打印字符；0 表示非字符键（回车/退格等另行处理） */
uint32_t ui_key_ascii(int code);

void ui_progress(ui_t *u, int x, int y, int w, int h, int pct);
void ui_progress_ring(ui_t *u, int cx, int cy, int r, int thick, int pct, uint32_t color);

/* 软键盘：返回 1 表示按下"完成"。内部直接修改 buf。 */
int  ui_keyboard(ui_t *u, int x, int y, int w, int h, char *buf, int cap);

/* 通用卡片/对话框底板 */
void ui_card(ui_t *u, int x, int y, int w, int h, int r);
void ui_dim(ui_t *u, int alpha);

#endif /* PROPEROS_UI_H */
