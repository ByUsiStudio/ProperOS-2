/* ProperOS 2.5 Mobile · 输入抽象实现
 *
 * 开发者：ByUsi Studio · 北啊呢
 */
#define _GNU_SOURCE
#include "input.h"

#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

#define LONG_PRESS_MS 520
#define SWIPE_MIN_PX  36
#define EDGE_MIN_PX   30
#define MOVE_SLOP     6

static int64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int test_bit(int bit, const unsigned long *arr)
{
    return (arr[bit / (int)(8 * sizeof(unsigned long))] >> (bit % (int)(8 * sizeof(unsigned long)))) & 1UL;
}

static void read_caps(idev_t *d)
{
    unsigned long evbit[(EV_MAX + 8 * (int)sizeof(unsigned long)) / (8 * (int)sizeof(unsigned long)) + 1] = {0};
    if (ioctl(d->fd, EVIOCGBIT(0, sizeof(evbit)), evbit) < 0) return;
    d->has_abs = test_bit(EV_ABS, evbit);
    d->has_rel = test_bit(EV_REL, evbit);
    d->has_btn = test_bit(EV_KEY, evbit);

    if (d->has_abs) {
        struct input_absinfo ai;
        d->min_x = 0; d->max_x = 0x7fffffff;
        d->min_y = 0; d->max_y = 0x7fffffff;
        if (ioctl(d->fd, EVIOCGABS(ABS_X), &ai) == 0 && ai.maximum > ai.minimum) {
            d->min_x = ai.minimum; d->max_x = ai.maximum;
        } else if (ioctl(d->fd, EVIOCGABS(ABS_MT_POSITION_X), &ai) == 0 && ai.maximum > ai.minimum) {
            d->min_x = ai.minimum; d->max_x = ai.maximum;
        }
        if (ioctl(d->fd, EVIOCGABS(ABS_Y), &ai) == 0 && ai.maximum > ai.minimum) {
            d->min_y = ai.minimum; d->max_y = ai.maximum;
        } else if (ioctl(d->fd, EVIOCGABS(ABS_MT_POSITION_Y), &ai) == 0 && ai.maximum > ai.minimum) {
            d->min_y = ai.minimum; d->max_y = ai.maximum;
        }
    }
}

int input_open(input_t *in, int w, int h)
{
    memset(in, 0, sizeof(*in));
    in->w = w; in->h = h;
    in->mouse_x = w / 2;
    in->mouse_y = h / 2;

    for (int i = 0; i < 16; i++) {
        char p[64];
        snprintf(p, sizeof(p), "/dev/input/event%d", i);
        int fd = open(p, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        idev_t *d = &in->dev[in->n];
        memset(d, 0, sizeof(*d));
        d->fd = fd;
        read_caps(d);
        if (!d->has_abs && !d->has_rel && !d->has_btn) { close(fd); continue; }
        d->x = w / 2;
        d->y = h / 2;
        in->n++;
    }
    return in->n;
}

void input_close(input_t *in)
{
    for (int i = 0; i < in->n; i++) close(in->dev[i].fd);
    in->n = 0;
}

static void push(in_event_t *out, int *n, int max, in_kind_t k, idev_t *d, int64_t now)
{
    if (*n >= max) return;
    in_event_t *e = &out[(*n)++];
    memset(e, 0, sizeof(*e));
    e->kind = k;
    e->x = d->x; e->y = d->y;
    e->x0 = d->x0; e->y0 = d->y0;
    e->dx = d->x - d->x0; e->dy = d->y - d->y0;
    e->vx = d->vx; e->vy = d->vy;
    e->fingers = d->nfingers > 0 ? d->nfingers : 1;
    e->edge = d->edge;
    e->ms = now;
}

static void begin_press(input_t *in, idev_t *d, int64_t now)
{
    d->down = 1;
    d->moved = 0;
    d->long_fired = 0;
    d->x0 = d->x; d->y0 = d->y;
    d->down_ms = now;
    d->last_x = d->x; d->last_y = d->y; d->last_ms = now;
    d->vx = d->vy = 0;
    d->edge = 0;
    int ew = in->w / 22;
    if (d->x0 <= ew) d->edge = -1;
    else if (d->x0 >= in->w - 1 - ew) d->edge = 1;
}

/* 抬手：把位移归类成语义手势 */
static void end_press(input_t *in, idev_t *d, in_event_t *out, int *n, int max, int64_t now)
{
    d->down = 0;
    int dx = d->x - d->x0, dy = d->y - d->y0;
    int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    int th = in->h / 12;

    if (d->edge == -1 && dx >= EDGE_MIN_PX) {
        push(out, n, max, IN_EDGE_LEFT, d, now);
    } else if (d->edge == 1 && dx <= -EDGE_MIN_PX) {
        push(out, n, max, IN_EDGE_RIGHT, d, now);
    } else if (d->y0 >= in->h - th && dy <= -EDGE_MIN_PX) {
        push(out, n, max, IN_PULL_UP, d, now);
    } else if (d->y0 <= th && dy >= EDGE_MIN_PX) {
        push(out, n, max, IN_PULL_DOWN, d, now);
    } else if (adx >= SWIPE_MIN_PX || ady >= SWIPE_MIN_PX) {
        push(out, n, max, IN_SWIPE, d, now);
    } else if (!d->long_fired) {
        push(out, n, max, IN_TAP, d, now);
    }
    push(out, n, max, IN_UP, d, now);
}

/* 更新速度估计：位移 / 时间，做一阶平滑，避免抖动 */
static void track_velocity(idev_t *d, int64_t now)
{
    int64_t dt = now - d->last_ms;
    if (dt <= 0) return;
    int ivx = (int)((d->x - d->last_x) * 1000 / dt);
    int ivy = (int)((d->y - d->last_y) * 1000 / dt);
    d->vx = (d->vx * 2 + ivx) / 3;
    d->vy = (d->vy * 2 + ivy) / 3;
    d->last_x = d->x; d->last_y = d->y; d->last_ms = now;
}

/* 位置写入：统一做坐标映射与越界钳制 */
static void set_x(input_t *in, idev_t *d, int raw)
{
    int span = d->max_x - d->min_x;
    int v = span > 0 ? (raw - d->min_x) * (in->w - 1) / span : raw;
    if (v < 0) v = 0;
    if (v >= in->w) v = in->w - 1;
    d->x = v;
}

static void set_y(input_t *in, idev_t *d, int raw)
{
    int span = d->max_y - d->min_y;
    int v = span > 0 ? (raw - d->min_y) * (in->h - 1) / span : raw;
    if (v < 0) v = 0;
    if (v >= in->h) v = in->h - 1;
    d->y = v;
}

static void on_move(input_t *in, idev_t *d, in_event_t *out, int *n, int max, int64_t now)
{
    if (abs(d->x - d->x0) > MOVE_SLOP || abs(d->y - d->y0) > MOVE_SLOP) d->moved = 1;
    track_velocity(d, now);
    push(out, n, max, IN_MOVE, d, now);
}

int input_poll(input_t *in, in_event_t *out, int max, int timeout_ms)
{
    int n = 0;
    fd_set fds;
    FD_ZERO(&fds);
    int maxfd = -1;
    for (int i = 0; i < in->n; i++) {
        FD_SET(in->dev[i].fd, &fds);
        if (in->dev[i].fd > maxfd) maxfd = in->dev[i].fd;
    }
    struct timeval tv, *ptv = NULL;
    if (timeout_ms >= 0) {
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        ptv = &tv;
    }
    if (maxfd >= 0) {
        int r = select(maxfd + 1, &fds, NULL, NULL, ptv);
        (void)r;
    } else if (ptv) {
        select(0, NULL, NULL, NULL, ptv);
    } else {
        return 0;
    }
    int64_t now = now_ms();

    struct input_event ev;
    for (int i = 0; i < in->n; i++) {
        idev_t *d = &in->dev[i];
        while (read(d->fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
            if (ev.type == EV_ABS) {
                if (ev.code == ABS_MT_SLOT) {
                    if (ev.value >= 0 && ev.value < IN_MAX_FINGERS) d->slot = ev.value;
                } else if (ev.code == ABS_MT_TRACKING_ID) {
                    int s = d->slot;
                    if (ev.value >= 0) {
                        if (!d->sdown[s]) {
                            d->sdown[s] = 1;
                            d->sx[s] = d->x; d->sy[s] = d->y;
                            d->nfingers++;
                        }
                        if (d->nfingers == 1) begin_press(in, d, now);
                    } else if (d->sdown[s]) {
                        d->sdown[s] = 0;
                        if (d->nfingers > 0) d->nfingers--;
                        if (d->nfingers == 0 && d->down) end_press(in, d, out, &n, max, now);
                    }
                } else if (ev.code == ABS_MT_POSITION_X || ev.code == ABS_X) {
                    set_x(in, d, ev.value);
                    if (ev.code == ABS_MT_POSITION_X && d->nfingers > 1) d->sx[d->slot] = d->x;
                    d->dirty = 1;
                } else if (ev.code == ABS_MT_POSITION_Y || ev.code == ABS_Y) {
                    set_y(in, d, ev.value);
                    if (ev.code == ABS_MT_POSITION_Y && d->nfingers > 1) d->sy[d->slot] = d->y;
                    d->dirty = 1;
                } else if (ev.code == ABS_PRESSURE && ev.value == 0 && d->down && d->nfingers <= 1) {
                    /* 部分屏幕不发 BTN_TOUCH，用压力归零判定抬起 */
                    end_press(in, d, out, &n, max, now);
                }
            } else if (ev.type == EV_REL) {
                if (ev.code == REL_X) in->mouse_x += ev.value;
                else if (ev.code == REL_Y) in->mouse_y += ev.value;
                if (in->mouse_x < 0) in->mouse_x = 0;
                if (in->mouse_x >= in->w) in->mouse_x = in->w - 1;
                if (in->mouse_y < 0) in->mouse_y = 0;
                if (in->mouse_y >= in->h) in->mouse_y = in->h - 1;
                d->x = in->mouse_x; d->y = in->mouse_y;
                d->dirty = 1;
                if (d->down) on_move(in, d, out, &n, max, now);
            } else if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
                if (d->dirty) {
                    d->dirty = 0;
                    if (d->down && !d->has_btn) on_move(in, d, out, &n, max, now);
                    else if (d->down && d->has_abs) on_move(in, d, out, &n, max, now);
                }
            } else if (ev.type == EV_KEY) {
                if (ev.code == BTN_TOUCH || ev.code == BTN_LEFT || ev.code == BTN_MOUSE) {
                    if (ev.value == 1 && !d->down) {
                        d->nfingers = 1;
                        begin_press(in, d, now);
                        push(out, &n, max, IN_DOWN, d, now);
                    } else if (ev.value == 0 && d->down) {
                        d->nfingers = 0;
                        end_press(in, d, out, &n, max, now);
                    }
                } else if (ev.value == 1) {
                    if (n < max) {
                        push(out, &n, max, IN_KEY, d, now);
                        out[n - 1].key = ev.code;
                    }
                }
            }
        }
    }

    /* 长按检测：按下未抬起、未移动、超过阈值 */
    for (int i = 0; i < in->n; i++) {
        idev_t *d = &in->dev[i];
        if (d->down && !d->long_fired && !d->moved && now - d->down_ms >= LONG_PRESS_MS) {
            d->long_fired = 1;
            push(out, &n, max, IN_LONG, d, now);
        }
    }
    return n;
}
