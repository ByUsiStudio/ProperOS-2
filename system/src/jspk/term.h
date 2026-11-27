/* ProperOS 25.0.0 Mobile · 真终端
 *
 * 不是把按键回显到文本框里：这里 openpty 出一个真正的伪终端，fork 一个
 * 登录 shell（bash/dash），把 master 端的字节流按 VT100/xterm 解析成
 * 字符网格；键盘输入写回 master。ls、vim、apt、top 都能照常工作。
 *
 * 开发者：ByUsi Studio · 北啊呢
 */
#ifndef PROPEROS_TERM_H
#define PROPEROS_TERM_H

#include <stddef.h>
#include <stdint.h>
#include "gfx.h"
#include "ui.h"

typedef struct term_s term_t;

term_t *term_new(void);
void    term_free(term_t *t);

/* 首帧绘制时按可用区域自动决定列数/行数并启动 shell（只启动一次） */
void    term_set_font(term_t *t, int px);
int     term_draw(term_t *t, gfx_t *g, int x, int y, int w, int h);
int     term_height_hint(term_t *t);

/* 把 master 端可读的数据全部解析进网格；主循环每帧调用 */
void    term_poll(term_t *t);
int     term_running(term_t *t);

/* 输入 */
void    term_send(term_t *t, const void *data, size_t n);
void    term_send_str(term_t *t, const char *s);
void    term_key(term_t *t, int keycode);       /* evdev 键码 */
void    term_scroll(term_t *t, int lines);      /* 负值回看历史，0 回到底部 */

/* 终端专用键盘（含 Esc/Tab/Ctrl/方向键/Enter），返回 1 表示本帧产生了输入 */
int     term_keyboard(ui_t *u, term_t *t, int x, int y, int w, int h);
int     term_kb_height(int h);

/* 供单元测试/离屏渲染：直接喂字节，不启动 shell */
void    term_feed(term_t *t, const void *data, size_t n);
/* 取一行文本（调试与测试用），返回实际长度 */
int     term_line_text(term_t *t, int row, char *out, size_t n);

#endif /* PROPEROS_TERM_H */
