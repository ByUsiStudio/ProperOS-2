/* ProperOS 25.0.0 Mobile · Wayland 输出后端
 *
 * .jspk 应用既可以独占 framebuffer（无桌面环境时），也可以作为普通
 * Wayland 客户端跑在 Plasma Mobile 里。两种模式共用同一套绘制代码：
 * 这里只负责「把 gfx 的后缓冲送到 wl_surface」以及「把 pointer/touch
 * 变成 in_event_t」。
 *
 * 开发者：ByUsi Studio · 北啊呢
 */
#ifndef PROPEROS_WL_H
#define PROPEROS_WL_H

#include "gfx.h"
#include "input.h"

/* 当前进程是否跑在 Wayland 会话里 */
int  wl_active(void);

/* 建全屏窗口并等首次 configure；成功返回 0，失败返回 -1（调用方回落 fbdev） */
int  wl_open(gfx_t *g, const char *title);

/* 提交一帧 */
void wl_flush(gfx_t *g);

/* 关闭窗口（gfx_close 调用） */
void wl_close(gfx_t *g);

/* 取输入事件，签名与 input_poll 一致 */
int  wl_poll_events(input_t *in, in_event_t *out, int max, int timeout_ms);

#endif /* PROPEROS_WL_H */
