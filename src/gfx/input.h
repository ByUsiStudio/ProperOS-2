/* ProperOS 2.5 Mobile · 输入抽象（evdev）
 *
 * 统一处理触摸屏、绝对坐标数位板、相对鼠标与键盘，输出语义化事件。
 * 上层无需关心设备类型：一个手指、一只鼠标、一块触摸板都以 TAP/SWIPE/LONG 表达。
 *
 * 触摸完善度：
 *  - 多指追踪（ABS_MT_SLOT / TRACKING_ID），事件带 fingers 数量
 *  - 滑动速度 vx/vy（px/ms），供惯性滚动与甩动翻页
 *  - 边缘手势：左右边缘内滑（返回）、顶部下拉（快捷设置）、底部上滑（应用抽屉）
 *  - 长按在移动超过阈值时自动取消，避免滚动误触发
 *
 * 开发者：ByUsi Studio · 北啊呢
 */
#ifndef PROPEROS_INPUT_H
#define PROPEROS_INPUT_H

#include <stdint.h>

typedef enum {
    IN_NONE = 0,
    IN_DOWN,
    IN_UP,
    IN_MOVE,
    IN_TAP,
    IN_LONG,
    IN_SWIPE,
    IN_KEY,
    /* 语义手势：由边缘/位置判定，桌面与 jspk 应用直接消费 */
    IN_EDGE_LEFT,    /* 从左边缘向右滑 */
    IN_EDGE_RIGHT,   /* 从右边缘向左滑 */
    IN_PULL_DOWN,    /* 顶部下拉 */
    IN_PULL_UP,      /* 底部上滑 */
} in_kind_t;

typedef struct {
    in_kind_t kind;
    int x, y;       /* 当前（或抬起时）坐标 */
    int x0, y0;     /* 起始坐标 */
    int dx, dy;     /* 位移 */
    int vx, vy;     /* 速度，px/ms（带符号） */
    int key;        /* 键码（IN_KEY） */
    int fingers;    /* 参与手指数（>=1） */
    int edge;       /* -1 左边缘 / +1 右边缘 / 0 非边缘起手 */
    int64_t ms;     /* 事件时间 */
} in_event_t;

#define IN_MAX_FINGERS 10

typedef struct {
    int fd;
    int has_abs, has_btn, has_rel;
    int x, y;
    int down;
    int moved;
    int long_fired;
    int64_t down_ms;
    int x0, y0;
    int min_x, max_x, min_y, max_y;

    /* 多指 */
    int slot;
    int nfingers;
    int sdown[IN_MAX_FINGERS];
    int sx[IN_MAX_FINGERS], sy[IN_MAX_FINGERS];

    /* 速度估计 */
    int last_x, last_y;
    int64_t last_ms;
    int vx, vy;
    int edge;
    int dirty;      /* 本轮 SYN_REPORT 前有坐标更新 */
} idev_t;

typedef struct {
    idev_t dev[16];
    int n;
    int w, h;
    int mouse_x, mouse_y;
} input_t;

int  input_open(input_t *in, int w, int h);
void input_close(input_t *in);
/* 等待并取事件；timeout_ms<0 表示阻塞。返回事件数量。 */
int  input_poll(input_t *in, in_event_t *out, int max, int timeout_ms);

#endif /* PROPEROS_INPUT_H */
