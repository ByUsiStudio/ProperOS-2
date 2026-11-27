/* ProperOS 25.0.0 Mobile · gfx 图形基础库
 *
 * 直接操作 framebuffer 的 2D 绘图与文本渲染层，供安装器与桌面共用。
 * 设计约束：无 X11 / 无 Wayland / 无 OpenGL，纯 CPU 光栅化，静态链接。
 *
 * 抗锯齿：圆角矩形与圆走有符号距离场（SDF），逐像素算覆盖率；
 * 文本走 8 位 alpha 图集，缩放时双线性取样。
 *
 * 开发者：ByUsi Studio · 北啊呢
 */
#ifndef PROPEROS_GFX_H
#define PROPEROS_GFX_H

#include <stdint.h>
#include <stddef.h>

/* ---------------- 颜色 ---------------- */
/* 0xRRGGBB */
#define GFX_RGB(r, g, b) (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

#define C_WHITE      0xFFFFFF
#define C_BLACK      0x000000
#define C_BG         0xF5F6FA   /* 桌面浅底 */
#define C_SURFACE    0xFFFFFF   /* 卡片 */
#define C_PRIMARY    0x3E7BFF   /* 品牌主色（取自原桌面主题） */
#define C_PRIMARY_D  0x2D63E0
#define C_TEXT       0x1B1D22
#define C_TEXT_2     0x60656F
#define C_TEXT_3     0x9AA0AB
#define C_LINE       0xE6E9EF
#define C_DANGER     0xE5484D
#define C_OK         0x2FA84F
#define C_WARN       0xE8A33D
#define C_DARK_BG    0x14161B
#define C_DARK_CARD  0x1E2129

/* ---------------- 画布 ---------------- */
typedef struct {
    int fd;
    uint8_t *mem;
    size_t size;
    int w, h, bpp, line;
    uint32_t *buf;          /* 32 位后缓冲，所有绘制先落这里 */
    void *wl;               /* 非空表示走 Wayland 客户端输出（见 wl.h） */
    /* 裁剪区（含左闭右开） */
    int cx0, cy0, cx1, cy1;
} gfx_t;

int  gfx_open(gfx_t *g, const char *dev);
/* 控制台接管：切到 vt 号所在终端并进入 KD_GRAPHICS，
 * 之后 fbcon 不再往 /dev/fb0 绘制（不清屏、不画光标），GUI 才能独占屏幕。 */
int  gfx_console_grab(int vt);
void gfx_console_release(void);
int  gfx_offscreen(gfx_t *g, int w, int h);
int  gfx_save_bmp(gfx_t *g, const char *path);
void gfx_close(gfx_t *g);
void gfx_flush(gfx_t *g);
void gfx_set_clip(gfx_t *g, int x, int y, int w, int h);
void gfx_reset_clip(gfx_t *g);

/* ---------------- 基础绘制 ---------------- */
void gfx_px(gfx_t *g, int x, int y, uint32_t c);
void gfx_px_a(gfx_t *g, int x, int y, uint32_t c, int a);
void gfx_fill(gfx_t *g, int x, int y, int w, int h, uint32_t c);
void gfx_fill_a(gfx_t *g, int x, int y, int w, int h, uint32_t c, int a);
void gfx_rect(gfx_t *g, int x, int y, int w, int h, int thick, uint32_t c);
void gfx_vgrad(gfx_t *g, int x, int y, int w, int h, uint32_t top, uint32_t bot);

/* 圆角矩形：SDF 抗锯齿；radius 为像素 */
void gfx_round(gfx_t *g, int x, int y, int w, int h, int r, uint32_t c);
void gfx_round_a(gfx_t *g, int x, int y, int w, int h, int r, uint32_t c, int a);
void gfx_round_outline(gfx_t *g, int x, int y, int w, int h, int r, int thick, uint32_t c);

/* 阴影：以圆角矩形为形状，向外按高斯衰减 */
void gfx_shadow(gfx_t *g, int x, int y, int w, int h, int r, int spread, int alpha);

void gfx_circle(gfx_t *g, int cx, int cy, int r, uint32_t c);
void gfx_glow(gfx_t *g, int cx, int cy, int r, uint32_t c, int alpha);
void gfx_circle_a(gfx_t *g, int cx, int cy, int r, uint32_t c, int a);
void gfx_ring(gfx_t *g, int cx, int cy, int r, int thick, uint32_t c, int a);
void gfx_line(gfx_t *g, float x0, float y0, float x1, float y1, float thick, uint32_t c);

/* ---------------- 图标 ---------------- */
/* 内置矢量图标：电话/信息/浏览器/相机/设置/文件/终端/音乐/相册/
 * 时钟/天气/地图/邮件/计算器/录音/笔记/商店/关于/搜索/信号/电池 */
void gfx_icon(gfx_t *g, const char *id, int x, int y, int size, uint32_t color);
int  gfx_icon_known(const char *id);

/* ---------------- 文本 ---------------- */
int  gfx_text_h(int scale);
int  gfx_text_w(const char *s, int scale);

/* ---------------- 任意字号文本 ----------------
 * 图集只有 37 px 一档（整数字号放大会块状）。界面需要 18/20/22/40 这样
 * 的密度，所以这里按目标字高做区域平均（缩小）或盒式取样（放大），
 * 保持 CJK 笔画不糊、边缘仍是灰阶。
 * px 为行高，基线位置与整数版一致（图集自带行框）。 */
void gfx_text_px(gfx_t *g, int x, int y, const char *s, uint32_t c, int px);
void gfx_text_px_a(gfx_t *g, int x, int y, const char *s, uint32_t c, int alpha, int px);
int  gfx_text_w_px(const char *s, int px);
void gfx_text_center_px(gfx_t *g, int cx, int y, const char *s, uint32_t c, int px);
void gfx_text_right_px(gfx_t *g, int rx, int y, const char *s, uint32_t c, int px);
void gfx_text_fit_px(gfx_t *g, int x, int y, int maxw, const char *s, uint32_t c, int px);
int  gfx_text_wrap_px(gfx_t *g, int x, int y, int maxw, const char *s, uint32_t c,
                      int px, int line_gap);
void gfx_text(gfx_t *g, int x, int y, const char *s, uint32_t c, int scale);
void gfx_text_a(gfx_t *g, int x, int y, const char *s, uint32_t c, int alpha, int scale);
void gfx_text_center(gfx_t *g, int cx, int y, const char *s, uint32_t c, int scale);
void gfx_text_right(gfx_t *g, int rx, int y, const char *s, uint32_t c, int scale);
/* 超宽时截断并追加省略号 */
void gfx_text_fit(gfx_t *g, int x, int y, int maxw, const char *s, uint32_t c, int scale);
/* 自动换行，返回占用高度 */
int  gfx_text_wrap(gfx_t *g, int x, int y, int maxw, const char *s, uint32_t c,
                   int scale, int line_gap);

/* ---------------- 工具 ---------------- */
uint32_t gfx_mix(uint32_t a, uint32_t b, int t);      /* t: 0..255，0=a */
uint32_t gfx_lighten(uint32_t c, int amount);
uint32_t gfx_darken(uint32_t c, int amount);
int      gfx_headless(void);                           /* 无 framebuffer 时用于单元测试 */

#endif /* PROPEROS_GFX_H */
