/* ProperOS 2.5 Mobile · .jspk 应用运行时
 *
 * 一个 .jspk 是 ZIP 容器：
 *   manifest.json   应用元信息（id/name/icon/color/entry/permissions）
 *   ui 目录下的 json  声明式界面（页面、控件、动作）
 *   res/*           资源（可选）
 *
 * 运行时负责：读包 → 解析界面 → 渲染 → 处理触摸 → 执行动作 → 持久化状态。
 * 这样系统应用（设置、文件、时钟、计算器…）全部以 .jspk 交付，
 * 不需要为每个应用编译一份 ELF，也让第三方可以只写 JSON 就出应用。
 *
 * 开发者：ByUsi Studio · 北啊呢
 */
#define _GNU_SOURCE
#include "gfx.h"
#include "ui.h"
#include "input.h"
#include "json.h"
#include "zip.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX_PAGES   48
#define MAX_STACK   12
#define MAX_VARS    96
#define MAX_ITEMS   192

/* ---------- 字号（按屏幕密度在运行时计算） ---------- */
static int FS_TINY = 13, FS_SMALL = 15, FS_BODY = 17;
static int FS_TITLE = 20, FS_H1 = 26, FS_HERO = 34;

#define JK_BG         0xF4F5F7
#define JK_CARD       0xFFFFFF
#define JK_TEXT       0x16181D
#define JK_SUB        0x6A7280
#define JK_MUTE       0x9AA1AC
#define JK_LINE       0xE4E7EC
#define JK_SEL        0xF0F2F5

#define JK_BG_D       0x121418
#define JK_CARD_D     0x1C1F25
#define JK_TEXT_D     0xEDEFF3
#define JK_SUB_D      0x9BA3B0
#define JK_LINE_D     0x2A2E36
#define JK_SEL_D      0x24282F

typedef struct {
    char key[40];
    char val[192];
} kv_t;

typedef struct {
    /* 包 */
    zip_t zip;
    jval_t *man;
    jval_t *doc;      /* 界面描述解析结果 */
    jval_t *pages;
    uint8_t *zipbuf;

    char id[96], name[80], icon[40], entry[96], developer[96], summary[160];
    char dir[256];
    uint32_t color;
    int allow_run, allow_url;

    /* 运行时 */
    gfx_t g;
    ui_t u;       /* 即时模式控件层 */
    input_t in;
    int dark;
    int wide;

    char stack[MAX_STACK][64];
    int depth;

    int scroll, scroll_max;
    int drag_active, drag_from, drag_y0;
    float fling;
    int content_top, content_bottom;

    kv_t vars[MAX_VARS];
    int nvars;

    char toast[160];
    int64_t toast_until;
    int quit;
    int redraw;

    /* 输入框 */
    int focus_id;
    char focus_key[40];
    int kb_h;

    int tty_w, tty_h;
    float dn;
} app_t;

static app_t A;

/* 把 dp 换算成像素 */
static int DP(int v)
{
    int r = (int)(v * A.dn);
    return r < 1 ? 1 : r;
}
static volatile sig_atomic_t g_quit;
static int g_debug;   /* PROPEROS_JSPK_DEBUG=1：把手势事件写进 /dev/kmsg */

/* 界面文本模板：{{time}} {{date}} {{battery}} {{wifi}} {{model}} */
static const char *tpl(const char *s)
{
    static char bufs[8][256];
    static int slot;
    if (!s) return "";
    if (!strstr(s, "{{")) return s;
    char *out = bufs[slot++ & 7];
    size_t used = 0;
    out[0] = '\0';
    const char *p = s;
    while (*p && used < sizeof(bufs[0]) - 1) {
        if (p[0] == '{' && p[1] == '{') {
            const char *end = strstr(p + 2, "}}");
            if (end) {
                size_t klen = (size_t)(end - (p + 2));
                char key[24];
                if (klen < sizeof(key)) {
                    memcpy(key, p + 2, klen);
                    key[klen] = '\0';
                    char val[64] = "";
                    if (!strcmp(key, "time")) {
                        time_t t = time(NULL);
                        struct tm tm;
                        localtime_r(&t, &tm);
                        snprintf(val, sizeof(val), "%02d:%02d", tm.tm_hour, tm.tm_min);
                    } else if (!strcmp(key, "date")) {
                        time_t t = time(NULL);
                        struct tm tm;
                        localtime_r(&t, &tm);
                        static const char *wd[] = { "周日", "周一", "周二", "周三", "周四", "周五", "周六" };
                        snprintf(val, sizeof(val), "%d年%d月%d日 %s", tm.tm_year + 1900,
                                 tm.tm_mon + 1, tm.tm_mday, wd[tm.tm_wday]);
                    } else if (!strcmp(key, "battery")) {
                        snprintf(val, sizeof(val), "%s", "87%");
                    } else if (!strcmp(key, "wifi")) {
                        snprintf(val, sizeof(val), "%s", "已连接");
                    } else if (!strcmp(key, "model")) {
                        snprintf(val, sizeof(val), "%s", "ProperOS Phone");
                    } else {
                        /* 其余键按应用状态变量插值（计算器、拨号盘等） */
                        for (int i = 0; i < A.nvars; i++)
                            if (!strcmp(A.vars[i].key, key)) {
                                snprintf(val, sizeof(val), "%s", A.vars[i].val);
                                break;
                            }
                    }
                    size_t vl = strlen(val);
                    if (used + vl < sizeof(bufs[0]) - 1) {
                        memcpy(out + used, val, vl);
                        used += vl;
                    }
                    p = end + 2;
                    continue;
                }
            }
        }
        out[used++] = *p++;
    }
    out[used] = '\0';
    return out;
}

static int64_t mono_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static uint32_t col(int light, int darkc)
{
    return A.dark ? (uint32_t)darkc : (uint32_t)light;
}

/* ================= 文件与状态 ================= */

static char *read_text(const char *path, size_t *len)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return NULL;
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size > (4 << 20)) { close(fd); return NULL; }
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

static void mkdir_p(const char *path)
{
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') { *p = '\0'; mkdir(tmp, 0755); *p = '/'; }
    }
    mkdir(tmp, 0755);
}

static void state_path(char *out, size_t n, const char *id)
{
    snprintf(out, n, "/data/home/.config/%s/state.conf", id);
}

static void settings_path(char *out, size_t n)
{
    snprintf(out, n, "/data/home/.config/properos/system.conf");
}

static const char *kv_file_get(const char *path, const char *key, char *out, size_t n)
{
    size_t len = 0;
    char *txt = read_text(path, &len);
    if (!txt) return NULL;
    const char *res = NULL;
    char *save = NULL;
    for (char *ln = strtok_r(txt, "\n", &save); ln; ln = strtok_r(NULL, "\n", &save)) {
        char *eq = strchr(ln, '=');
        if (!eq) continue;
        *eq = '\0';
        if (!strcmp(ln, key)) {
            snprintf(out, n, "%s", eq + 1);
            res = out;
            break;
        }
    }
    free(txt);
    return res;
}

static void kv_file_set(const char *path, const char *key, const char *val)
{
    size_t len = 0;
    char *txt = read_text(path, &len);
    char out[8192];
    size_t used = 0;
    int replaced = 0;
    out[0] = '\0';

    if (txt) {
        char *save = NULL;
        for (char *ln = strtok_r(txt, "\n", &save); ln; ln = strtok_r(NULL, "\n", &save)) {
            char *eq = strchr(ln, '=');
            if (eq) {
                *eq = '\0';
                if (!strcmp(ln, key)) {
                    used += (size_t)snprintf(out + used, sizeof(out) - used, "%s=%s\n", key, val);
                    replaced = 1;
                    continue;
                }
                used += (size_t)snprintf(out + used, sizeof(out) - used, "%s=%s\n", ln, eq + 1);
            }
        }
        free(txt);
    }
    if (!replaced) used += (size_t)snprintf(out + used, sizeof(out) - used, "%s=%s\n", key, val);

    char d[256];
    snprintf(d, sizeof(d), "%s", path);
    char *slash = strrchr(d, '/');
    if (slash) { *slash = '\0'; mkdir_p(d); }

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return;
    (void)write(fd, out, used);
    close(fd);
}

static const char *sys_get(const char *key, char *out, size_t n)
{
    char p[256];
    settings_path(p, sizeof(p));
    return kv_file_get(p, key, out, n);
}

static void sys_set(const char *key, const char *val)
{
    char p[256];
    settings_path(p, sizeof(p));
    kv_file_set(p, key, val);
}

static const char *var_get(app_t *a, const char *key, const char *def)
{
    for (int i = 0; i < a->nvars; i++)
        if (!strcmp(a->vars[i].key, key)) return a->vars[i].val;
    return def;
}

static int var_int(app_t *a, const char *key, int def)
{
    const char *v = var_get(a, key, NULL);
    return v ? atoi(v) : def;
}

static void var_set(app_t *a, const char *key, const char *val)
{
    if (!key || !key[0]) return;
    for (int i = 0; i < a->nvars; i++) {
        if (!strcmp(a->vars[i].key, key)) {
            snprintf(a->vars[i].val, sizeof(a->vars[i].val), "%s", val);
            return;
        }
    }
    if (a->nvars >= MAX_VARS) return;
    snprintf(a->vars[a->nvars].key, sizeof(a->vars[a->nvars].key), "%s", key);
    snprintf(a->vars[a->nvars].val, sizeof(a->vars[a->nvars].val), "%s", val);
    a->nvars++;
}

/* 与桌面 / 系统设置共用的键：这些键统一写 system.conf，
   否则同一个开关在桌面和设置应用里会各记一份，互相矛盾 */
static int is_sys_key(const char *k)
{
    static const char *keys[] = {
        "wifi", "bt", "dnd", "airplane", "auto_rotate", "battery_saver",
        "brightness", "volume", "dark", "wallpaper", "timeout", "flash",
        "hotspot", "mobile_data", "sound", "vibrate", NULL
    };
    for (int i = 0; keys[i]; i++)
        if (!strcmp(k, keys[i])) return 1;
    return 0;
}

static void kv_load_all(app_t *a, const char *path, int skip_sys)
{
    size_t len = 0;
    char *txt = read_text(path, &len);
    if (!txt) return;
    char *save = NULL;
    for (char *ln = strtok_r(txt, "\n", &save); ln; ln = strtok_r(NULL, "\n", &save)) {
        char *eq = strchr(ln, '=');
        if (!eq) continue;
        *eq = '\0';
        if (skip_sys && is_sys_key(ln)) continue;
        var_set(a, ln, eq + 1);
    }
    free(txt);
}

static void state_load(app_t *a)
{
    char p[256];
    state_path(p, sizeof(p), a->id);
    kv_load_all(a, p, 0);
    settings_path(p, sizeof(p));
    kv_load_all(a, p, 0);   /* 系统设置后读，同名键以系统为准 */
}

static void state_save_key(app_t *a, const char *key)
{
    char p[256];
    if (is_sys_key(key)) settings_path(p, sizeof(p));
    else state_path(p, sizeof(p), a->id);
    kv_file_set(p, key, var_get(a, key, ""));
}

/* ================= 动作 ================= */

static void toast(app_t *a, const char *msg)
{
    snprintf(a->toast, sizeof(a->toast), "%s", msg ? msg : "");
    a->toast_until = mono_ms() + 1900;
}

static void push_page(app_t *a, const char *page)
{
    if (!page || !page[0]) return;
    if (a->depth >= MAX_STACK) return;
    snprintf(a->stack[a->depth], sizeof(a->stack[0]), "%s", page);
    a->depth++;
    a->scroll = 0;
    a->fling = 0;
    a->focus_id = 0;
    a->redraw = 1;
}

static void pop_page(app_t *a)
{
    if (a->depth > 1) a->depth--;
    else a->quit = 1;
    a->scroll = 0;
    a->fling = 0;
    a->focus_id = 0;
    a->redraw = 1;
}

static jval_t *cur_page(app_t *a)
{
    if (a->depth <= 0) return NULL;
    jval_t *p = json_get(a->pages, a->stack[a->depth - 1]);
    if (!p && a->depth > 0) return json_get(a->pages, "root");
    return p;
}

static void run_shell_cmd(const char *cmd)
{
    if (!cmd || !cmd[0]) return;
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }
}

/* 简易表达式：支持 + - * / 与小数，供计算器使用 */
static double eval_expr(const char *s, int *ok)
{
    double stack[32];
    char ops[32];
    int sp = 0, op = 0;
    int expect_num = 1;
    *ok = 1;

    while (*s) {
        if (*s == ' ') { s++; continue; }
        if (expect_num) {
            if (*s == '-' || *s == '+') {
                int neg = (*s == '-');
                s++;
                double v = strtod(s, (char **)&s);
                if (neg) v = -v;
                if (sp < 32) stack[sp++] = v;
            } else if (*s == '(') {
                /* 简化：跳过括号 */
                s++;
                continue;
            } else if (*s >= '0' && *s <= '9') {
                double v = strtod(s, (char **)&s);
                if (sp < 32) stack[sp++] = v;
            } else {
                *ok = 0;
                return 0;
            }
            expect_num = 0;
            continue;
        }
        if (*s == ')' ) { s++; continue; }
        if (*s == '+' || *s == '-' || *s == '*' || *s == '/') {
            while (op > 0 && ((ops[op - 1] == '*' || ops[op - 1] == '/') ||
                              (*s != '*' && *s != '/'))) {
                if (sp < 2) { *ok = 0; return 0; }
                double b = stack[--sp], a2 = stack[--sp];
                char o = ops[--op];
                stack[sp++] = o == '+' ? a2 + b : o == '-' ? a2 - b : o == '*' ? a2 * b
                              : (b == 0 ? 0 : a2 / b);
            }
            if (op < 32) ops[op++] = *s;
            s++;
            expect_num = 1;
            continue;
        }
        *ok = 0;
        return 0;
    }
    while (op > 0) {
        if (sp < 2) { *ok = 0; return 0; }
        double b = stack[--sp], a2 = stack[--sp];
        char o = ops[--op];
        stack[sp++] = o == '+' ? a2 + b : o == '-' ? a2 - b : o == '*' ? a2 * b
                      : (b == 0 ? 0 : a2 / b);
    }
    if (sp != 1) { *ok = 0; return 0; }
    return stack[0];
}

static void fmt_num(double v, char *out, size_t n)
{
    if (v == (double)(long long)v && v > -1e15 && v < 1e15) snprintf(out, n, "%lld", (long long)v);
    else snprintf(out, n, "%.6g", v);
}

static void run_action(app_t *a, jval_t *act);

static void do_action_obj(app_t *a, jval_t *act)
{
    const char *type = json_str(json_get(act, "type"), "");
    const char *to = json_str(json_get(act, "to"), NULL);
    const char *key = json_str(json_get(act, "key"), NULL);

    if (!type[0] && to) { push_page(a, to); return; }

    if (!strcmp(type, "nav") || !strcmp(type, "navigate")) {
        push_page(a, to ? to : json_str(json_get(act, "page"), ""));
    } else if (!strcmp(type, "back")) {
        pop_page(a);
    } else if (!strcmp(type, "close") || !strcmp(type, "exit")) {
        a->quit = 1;
    } else if (!strcmp(type, "toast")) {
        toast(a, json_str(json_get(act, "text"), ""));
    } else if (!strcmp(type, "set")) {
        char v[192];
        jval_t *jv = json_get(act, "value");
        if (jv && jv->t == JNUM) fmt_num(jv->num, v, sizeof(v));
        else snprintf(v, sizeof(v), "%s", json_str(jv, ""));
        var_set(a, key, v);
        if (key) state_save_key(a, key);
        a->redraw = 1;
    } else if (!strcmp(type, "toggle")) {
        int nv = !var_int(a, key ? key : "", 0);
        char v[8];
        snprintf(v, sizeof(v), "%d", nv);
        var_set(a, key, v);
        if (key) state_save_key(a, key);
        a->redraw = 1;
    } else if (!strcmp(type, "setting")) {
        char v[192];
        jval_t *jv = json_get(act, "value");
        if (jv && jv->t == JNUM) fmt_num(jv->num, v, sizeof(v));
        else if (jv && jv->t == JBOOL) snprintf(v, sizeof(v), "%d", jv->b);
        else snprintf(v, sizeof(v), "%s", json_str(jv, "1"));
        sys_set(key ? key : "", v);
        if (key && !strcmp(key, "dark")) a->dark = atoi(v);
        a->redraw = 1;
    } else if (!strcmp(type, "run")) {
        if (a->allow_run) run_shell_cmd(json_str(json_get(act, "cmd"), ""));
        else toast(a, "该应用没有执行命令的权限");
    } else if (!strcmp(type, "url")) {
        const char *u = json_str(json_get(act, "url"), "");
        if (a->allow_url && u[0]) {
            char cmd[512];
            snprintf(cmd, sizeof(cmd), "properos-shell --open-url '%s' >/dev/null 2>&1 &", u);
            run_shell_cmd(cmd);
        } else {
            toast(a, u[0] ? u : "链接为空");
        }
    } else if (!strcmp(type, "copy")) {
        const char *t = json_str(json_get(act, "text"), "");
        mkdir_p("/data/home");
        int fd = open("/data/home/.clipboard", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) { (void)write(fd, t, strlen(t)); close(fd); }
        toast(a, "已复制");
    } else if (!strcmp(type, "calc")) {
        char ebuf[192];
        const char *expr = json_str(json_get(act, "expr"), NULL);
        if (!expr) {
            snprintf(ebuf, sizeof(ebuf), "%s", var_get(a, "expr", ""));
            expr = ebuf;
        }
        int ok = 0;
        double v = eval_expr(expr, &ok);
        char out[64];
        if (ok) fmt_num(v, out, sizeof(out));
        else snprintf(out, sizeof(out), "错误");
        var_set(a, key ? key : "result", out);
        if (key) state_save_key(a, key);
        a->redraw = 1;
    } else if (!strcmp(type, "call")) {
        const char *num = json_str(json_get(act, "number"), "");
        char msg[128];
        snprintf(msg, sizeof(msg), "正在呼叫 %s …", num);
        toast(a, msg);
    } else if (!strcmp(type, "append")) {
        char cur[192];
        snprintf(cur, sizeof(cur), "%s", var_get(a, key ? key : "expr", ""));
        const char *t = json_str(json_get(act, "text"), "");
        if (strlen(cur) + strlen(t) < sizeof(cur) - 1) strcat(cur, t);
        var_set(a, key ? key : "expr", cur);
        if (key) state_save_key(a, key);
        a->redraw = 1;
    } else if (!strcmp(type, "bs")) {
        char cur[192];
        snprintf(cur, sizeof(cur), "%s", var_get(a, key ? key : "expr", ""));
        size_t n = strlen(cur);
        while (n > 0 && ((unsigned char)cur[n - 1] & 0xC0) == 0x80) n--;
        if (n > 0) n--;
        cur[n] = '\0';
        var_set(a, key ? key : "expr", cur);
        if (key) state_save_key(a, key);
        a->redraw = 1;
    } else if (!strcmp(type, "clear")) {
        if (key) {
            var_set(a, key, "");
            state_save_key(a, key);
        } else {
            a->nvars = 0;
        }
        a->redraw = 1;
    }
}

static void run_action(app_t *a, jval_t *act)
{
    if (!act) return;
    if (act->t == JSTR) { push_page(a, act->str); return; }
    if (act->t == JOBJ) do_action_obj(a, act);
}

/* 控件上的动作：`to` 便捷字段 + `action` 对象 */
static jval_t *widget_action(jval_t *w)
{
    jval_t *act = json_get(w, "action");
    if (act) return act;
    return NULL;
}

static void widget_activate(app_t *a, jval_t *w)
{
    jval_t *act = widget_action(w);
    if (act) { run_action(a, act); return; }
    const char *to = json_str(json_get(w, "to"), NULL);
    if (to && to[0]) push_page(a, to);
}

/* ================= 测量 ================= */

static int px_w(const char *s, int px) { return gfx_text_w_px(s, px); }

static int wrap_lines(const char *s, int maxw, int px)
{
    if (!s || !maxw) return 1;
    int lines = 1, w = 0;
    while (*s) {
        const char *q = s;
        unsigned char c = (unsigned char)*q;
        int bytes = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : 4;
        char tmp[8] = {0};
        memcpy(tmp, q, (size_t)(bytes < 8 ? bytes : 1));
        if (tmp[0] == '\n') { lines++; w = 0; s += 1; continue; }
        int adv = px_w(tmp, px);
        if (w + adv > maxw) { lines++; w = 0; }
        w += adv;
        s += bytes < 8 ? bytes : 1;
        if (lines > 200) break;
    }
    return lines;
}

static int has_sub(jval_t *w) { return tpl(json_str(json_get(w, "sub"), NULL)) != NULL; }

static int measure_widget(app_t *a, jval_t *w, int cw);

static int measure_children(app_t *a, jval_t *arr, int cw)
{
    int h = 0;
    int n = json_len(arr);
    for (int i = 0; i < n; i++) h += measure_widget(a, json_at(arr, i), cw);
    return h;
}

static int measure_widget(app_t *a, jval_t *w, int cw)
{
    if (!w || w->t != JOBJ) return 0;
    const char *t = json_str(json_get(w, "t"), "row");

    if (!strcmp(t, "group")) return FS_TINY + DP(20);
    if (!strcmp(t, "row") || !strcmp(t, "switch"))
        return has_sub(w) ? DP(72) : DP(62);
    if (!strcmp(t, "slider")) return DP(48);
    if (!strcmp(t, "button")) return DP(26) + DP(6);
    if (!strcmp(t, "text")) {
        int px = json_int(json_get(w, "px"), FS_BODY);
        int lines = wrap_lines(tpl(json_str(json_get(w, "text"), "")), cw, px);
        return lines * (px + 7) + 16;
    }
    if (!strcmp(t, "note")) {
        int lines = wrap_lines(tpl(json_str(json_get(w, "text"), "")), cw - 48, FS_SMALL);
        return lines * (FS_SMALL + 6) + 26;
    }
    if (!strcmp(t, "card")) {
        int inner = measure_children(a, json_get(w, "children"), cw - 32);
        int title = tpl(json_str(json_get(w, "title"), NULL)) ? FS_BODY + 12 : 0;
        return inner + title + 32;
    }
    if (!strcmp(t, "grid")) {
        int cols = json_int(json_get(w, "cols"), a->wide ? 6 : 4);
        int items = json_len(json_get(w, "items"));
        int rows = (items + cols - 1) / cols;
        int cell = DP(json_int(json_get(w, "cell"), a->wide ? 48 : 50));
        return rows * cell + DP(8);
    }
    if (!strcmp(t, "tiles")) {
        int cols = json_int(json_get(w, "cols"), 2);
        int items = json_len(json_get(w, "items"));
        int rows = (items + cols - 1) / cols;
        return rows * (DP(json_int(json_get(w, "cell"), 46)) + DP(6)) + DP(5);
    }
    if (!strcmp(t, "progress")) return DP(40);
    if (!strcmp(t, "input")) return DP(46);
    if (!strcmp(t, "divider")) return DP(13);
    if (!strcmp(t, "spacer")) return DP(json_int(json_get(w, "h"), 8));
    if (!strcmp(t, "image")) return DP(112);
    if (!strcmp(t, "keyvalue")) return DP(28);
    if (!strcmp(t, "stats")) return DP(62);
    return 64;
}

/* ================= 绘制 ================= */

static void draw_icon_tile(app_t *a, int x, int y, int size, const char *icon, uint32_t tint)
{
    gfx_round(&a->g, x, y, size, size, size / 4, tint);
    int pad = size / 4;
    gfx_icon(&a->g, icon && icon[0] ? icon : "store", x + pad, y + pad, size - pad * 2, C_WHITE);
}

/* 列表行右侧的 chevron */
static void draw_chevron(int x, int y)
{
    uint32_t c = col(JK_MUTE, 0x6B7280);
    gfx_line(&A.g, x, y - 6, x + 6, y, 1.6f, c);
    gfx_line(&A.g, x + 6, y, x, y + 6, 1.6f, c);
}

static void draw_switch_at(app_t *a, int x, int y, int on)
{
    int w = DP(52), h = DP(32);
    uint32_t bg = on ? a->color : col(0xD5D9E0, 0x3A3F49);
    gfx_round(&a->g, x, y, w, h, h / 2, bg);
    int kr = h / 2 - DP(4);
    int kx = on ? x + w - DP(4) - kr : x + DP(4) + kr;
    gfx_circle(&a->g, kx, y + h / 2, kr, C_WHITE);
}

/* 按下高亮：用即时模式指针位置判断 */
static int pressed(app_t *a, int x, int y, int w, int h)
{
    return a->u.down && a->u.px >= x && a->u.px < x + w &&
           a->u.py >= y && a->u.py < y + h;
}

static int draw_widget(app_t *a, jval_t *w, int x, int y, int cw, int top, int bottom)
{
    if (!w || w->t != JOBJ) return 0;
    const char *t = json_str(json_get(w, "t"), "row");
    int h = measure_widget(a, w, cw);
    int vis = (y + h > top && y < bottom);

    uint32_t tc = col(JK_TEXT, JK_TEXT_D);
    uint32_t sc = col(JK_SUB, JK_SUB_D);
    uint32_t cardc = col(JK_CARD, JK_CARD_D);

    if (!vis) return h;

    if (!strcmp(t, "group")) {
        const char *title = tpl(json_str(json_get(w, "title"), ""));
        gfx_text_px(&a->g, x + 4, y + DP(11), title, sc, FS_TINY);
        return h;
    }

    if (!strcmp(t, "divider")) {
        gfx_fill_a(&a->g, x, y + h / 2, cw, 1, col(JK_LINE, JK_LINE_D), 255);
        return h;
    }

    if (!strcmp(t, "spacer")) return h;

    if (!strcmp(t, "text")) {
        int px = (int)(json_int(json_get(w, "px"), 16) * A.dn);
        const char *s = tpl(json_str(json_get(w, "text"), ""));
        const char *al = json_str(json_get(w, "align"), "left");
        const char *cs = json_str(json_get(w, "color"), NULL);
        uint32_t cc = cs ? (uint32_t)strtoul(cs[0] == '#' ? cs + 1 : cs, NULL, 16) : tc;
        /* empty：变量为空时显示的占位文字（例如拨号盘的号码框） */
        if (!s || !s[0]) {
            const char *ph = json_str(json_get(w, "empty"), NULL);
            if (ph && ph[0]) { s = ph; cc = sc; if (px > FS_BODY) px = FS_BODY; }
        }
        if (!strcmp(al, "right"))
            gfx_text_right_px(&a->g, x + cw - 2, y + DP(4), s, cc, px);
        else if (!strcmp(al, "center"))
            gfx_text_center_px(&a->g, x + cw / 2, y + DP(4), s, cc, px);
        else
            gfx_text_wrap_px(&a->g, x + 2, y + DP(4), cw - 4, s, cc, px, DP(4));
        return h;
    }

    if (!strcmp(t, "note")) {
        const char *s = tpl(json_str(json_get(w, "text"), ""));
        uint32_t tint = col(0xFDF3E9, 0x2A2118);
        gfx_round(&a->g, x, y + 2, cw, h - 4, DP(5), tint);
        gfx_round(&a->g, x + DP(6), y + DP(7), DP(2), h - DP(14), DP(1), col(0xE08A2E, 0xE0A050));
        gfx_text_wrap_px(&a->g, x + DP(13), y + DP(7), cw - DP(22), s,
                         col(0x8A5A1E, 0xD8B98A), FS_SMALL, DP(3));
        return h;
    }

    if (!strcmp(t, "card")) {
        gfx_round(&a->g, x, y + 2, cw, h - 4, 14, cardc);
        int cy = y + 16;
        const char *title = tpl(json_str(json_get(w, "title"), NULL));
        if (title) {
            gfx_text_px(&a->g, x + 16, cy, title, tc, FS_BODY);
            cy += FS_BODY + 12;
        }
        jval_t *ch = json_get(w, "children");
        int n = json_len(ch);
        for (int i = 0; i < n; i++) {
            jval_t *c = json_at(ch, i);
            cy += draw_widget(a, c, x + 16, cy, cw - 32, top, bottom);
        }
        return h;
    }

    if (!strcmp(t, "row") || !strcmp(t, "switch")) {
        int row_h = has_sub(w) ? DP(72) : DP(62);
        const char *title = tpl(json_str(json_get(w, "title"), ""));
        const char *sub = tpl(json_str(json_get(w, "sub"), NULL));
        const char *icon = json_str(json_get(w, "icon"), NULL);
        const char *value = tpl(json_str(json_get(w, "value"), NULL));
        const char *key = json_str(json_get(w, "key"), NULL);
        int is_switch = !strcmp(t, "switch") || json_bool(json_get(w, "toggle"), 0) || key;
        int have_action = widget_action(w) != NULL || json_str(json_get(w, "to"), NULL) != NULL;

        int hx = x + 2, hw = cw - 4;
        if (pressed(a, hx, y + 2, hw, row_h - 4))
            gfx_round(&a->g, hx, y + 2, hw, row_h - 4, 10, col(JK_SEL, JK_SEL_D));

        int isz = DP(28);
        int ix = x + DP(5);
        int ty = y + (row_h - (sub ? FS_BODY + FS_TINY + DP(3) : FS_BODY)) / 2;
        if (icon && icon[0]) {
            uint32_t tint = json_str(json_get(w, "tint"), NULL)
                                ? (uint32_t)strtoul(json_str(json_get(w, "tint"), ""), NULL, 16)
                                : a->color;
            draw_icon_tile(a, ix, y + (row_h - isz) / 2, isz, icon, tint);
            ix += isz + DP(7);
        }

        gfx_text_fit_px(&a->g, ix, ty, cw - (ix - x) - DP(45), title, tc, FS_BODY);
        if (sub)
            gfx_text_fit_px(&a->g, ix, ty + FS_BODY + DP(3), cw - (ix - x) - DP(45), sub,
                            sc, FS_TINY);

        int rx = x + cw - DP(8);
        if (is_switch && key) {
            int on = var_int(a, key, json_bool(json_get(w, "on"), 0));
            draw_switch_at(a, rx - DP(52), y + (row_h - DP(32)) / 2, on);
            if (ui_hit(&a->u, hx, y + 2, hw, row_h - 4)) {
                char v[8];
                snprintf(v, sizeof(v), "%d", !on);
                var_set(a, key, v);
                state_save_key(a, key);
                if (json_str(json_get(w, "setting"), NULL))
                    sys_set(json_str(json_get(w, "setting"), ""), v);
                a->redraw = 1;
            }
        } else if (value && value[0]) {
            gfx_text_right_px(&a->g, rx, ty + (FS_BODY - FS_SMALL) / 2, value, sc, FS_SMALL);
            if (have_action && ui_hit(&a->u, hx, y + 2, hw, row_h - 4))
                widget_activate(a, w);
        } else if (have_action) {
            draw_chevron(rx - DP(4), y + row_h / 2);
            if (ui_hit(&a->u, hx, y + 2, hw, row_h - 4)) widget_activate(a, w);
        }
        return h;
    }

    if (!strcmp(t, "slider")) {
        const char *title = tpl(json_str(json_get(w, "title"), ""));
        const char *key = json_str(json_get(w, "key"), "");
        int min = json_int(json_get(w, "min"), 0);
        int max = json_int(json_get(w, "max"), 100);
        int val = var_int(a, key, json_int(json_get(w, "value"), (min + max) / 2));
        if (val < min) val = min;
        if (val > max) val = max;

        gfx_text_px(&a->g, x + DP(2), y + DP(3), title, tc, FS_BODY);
        char vb[16];
        snprintf(vb, sizeof(vb), "%d", val);
        gfx_text_right_px(&a->g, x + cw - DP(2), y + DP(4), vb, sc, FS_SMALL);

        int sy = y + DP(24), sw = cw - DP(4), sh = DP(5);
        gfx_round(&a->g, x + DP(2), sy, sw, sh, sh / 2, col(0xDDE1E7, 0x333842));
        int fill = (max > min) ? (val - min) * sw / (max - min) : 0;
        if (fill > 0) gfx_round(&a->g, x + DP(2), sy, fill, sh, sh / 2, a->color);
        gfx_circle(&a->g, x + DP(2) + fill, sy + sh / 2, DP(7), C_WHITE);
        gfx_circle_a(&a->g, x + DP(2) + fill, sy + sh / 2, DP(7), a->color, 60);

        /* 拖动：只要按下点在滑块区域就跟随 */
        if (a->u.down && a->u.py >= sy - DP(10) && a->u.py <= sy + sh + DP(10) &&
            a->u.px >= x && a->u.px <= x + cw) {
            int nv = min + (a->u.px - (x + DP(2))) * (max - min) / (sw > 0 ? sw : 1);
            if (nv < min) nv = min;
            if (nv > max) nv = max;
            char v[16];
            snprintf(v, sizeof(v), "%d", nv);
            if (nv != val) {
                var_set(a, key, v);
                a->redraw = 1;
                const char *sk = json_str(json_get(w, "setting"), NULL);
                if (sk) sys_set(sk, v);
                state_save_key(a, key);
            }
        }
        return h;
    }

    if (!strcmp(t, "button")) {
        const char *label = tpl(json_str(json_get(w, "text"), ""));
        const char *kind = json_str(json_get(w, "kind"), "primary");
        int bh = DP(26);
        uint32_t bg = a->color, fg = C_WHITE;
        if (!strcmp(kind, "secondary")) { bg = col(0xE9ECF1, 0x2C313A); fg = tc; }
        else if (!strcmp(kind, "danger")) { bg = 0xD93B3B; }
        else if (!strcmp(kind, "ghost")) { bg = 0; fg = a->color; }
        if (pressed(a, x, y + 3, cw, bh)) bg = gfx_darken(bg, 24);
        if (bg) {
            gfx_shadow(&a->g, x, y + 5, cw, bh, DP(6), DP(4), a->dark ? 40 : 26);
            gfx_round(&a->g, x, y + 3, cw, bh, DP(6), bg);
        }
        gfx_text_center_px(&a->g, x + cw / 2, y + 3 + (bh - FS_BODY) / 2, label, fg, FS_BODY);
        if (ui_hit(&a->u, x, y + 3, cw, bh)) widget_activate(a, w);
        return h;
    }

    if (!strcmp(t, "grid")) {
        int cols = json_int(json_get(w, "cols"), a->wide ? 6 : 4);
        int cell = DP(json_int(json_get(w, "cell"), a->wide ? 48 : 50));
        jval_t *items = json_get(w, "items");
        int n = json_len(items);
        int icon = DP(json_int(json_get(w, "icon"), 24));
        for (int i = 0; i < n; i++) {
            int cx = x + (i % cols) * (cw / cols);
            int cy = y + (i / cols) * cell;
            jval_t *it = json_at(items, i);
            const char *name = tpl(json_str(json_get(it, "title"), ""));
            const char *ic = json_str(json_get(it, "icon"), "store");
            const char *tint_s = json_str(json_get(it, "tint"), NULL);
            uint32_t tint = tint_s ? (uint32_t)strtoul(tint_s + (tint_s[0] == '#' ? 1 : 0), NULL, 16)
                                   : a->color;
            int tx = cx + (cw / cols - icon) / 2;
            if (pressed(a, cx, cy, cw / cols, cell)) {
                gfx_round(&a->g, cx + 6, cy + 2, cw / cols - 12, cell - 4, 12, col(JK_SEL, JK_SEL_D));
            }
            draw_icon_tile(a, tx, cy + DP(5), icon, ic, tint);
            gfx_text_center_px(&a->g, cx + cw / cols / 2, cy + DP(5) + icon + DP(4),
                               name, tc, FS_TINY);
            if (ui_hit(&a->u, cx, cy, cw / cols, cell)) widget_activate(a, it);
        }
        return h;
    }

    if (!strcmp(t, "tiles")) {
        int cols = json_int(json_get(w, "cols"), 2);
        int cell = DP(json_int(json_get(w, "cell"), 46));
        int gap = DP(6);
        int tw = (cw - gap * (cols - 1)) / cols;
        jval_t *items = json_get(w, "items");
        int n = json_len(items);
        int cur_col = 0, cur_row = 0;
        for (int i = 0; i < n; i++) {
            jval_t *it = json_at(items, i);
            int span = json_int(json_get(it, "span"), 1);
            if (span < 1) span = 1;
            if (span > cols) span = cols;
            if (cur_col + span > cols) { cur_col = 0; cur_row++; }
            int cx = x + cur_col * (tw + gap);
            int cy = y + cur_row * (cell + gap);
            int iw = tw * span + gap * (span - 1);
            cur_col += span;
            const char *title = tpl(json_str(json_get(it, "title"), ""));
            const char *sub = tpl(json_str(json_get(it, "sub"), NULL));
            const char *ic = json_str(json_get(it, "icon"), NULL);
            const char *tint_s = json_str(json_get(it, "tint"), NULL);
            int on = json_str(json_get(it, "key"), NULL)
                         ? var_int(a, json_str(json_get(it, "key"), ""), 0)
                         : 1;
            uint32_t bg = tint_s ? (uint32_t)strtoul(tint_s + (tint_s[0] == '#' ? 1 : 0), NULL, 16)
                                 : (on ? a->color : col(0xE7EAF0, 0x2A2F38));
            if (bg == 0xFFFFFF) bg = col(0xFFFFFF, 0x262B33);
            if (pressed(a, cx, cy, iw, cell)) bg = gfx_darken(bg, 20);
            /* 亮底用深色字，避免白底白字 */
            int lum = (int)(((bg >> 16) & 255) * 299 + ((bg >> 8) & 255) * 587 + (bg & 255) * 114) / 1000;
            uint32_t fg = lum > 170 ? 0x16181D : C_WHITE;
            uint32_t fg2 = lum > 170 ? 0x6A7280 : 0xE6ECF7;
            gfx_round(&a->g, cx, cy, iw, cell, 14, bg);
            if (ic && ic[0]) {
                int isz = DP(15);
                gfx_icon(&a->g, ic, cx + DP(7), cy + DP(6), isz, fg);
            }
            int sub_h = sub ? FS_TINY + DP(2) : 0;
            int sub_y = cy + cell - DP(8) - FS_TINY;
            int title_y = sub_y - DP(2) - FS_SMALL;
            if (ic && ic[0])
                gfx_text_fit_px(&a->g, cx + DP(7), title_y, iw - DP(14), title,
                                fg, FS_SMALL);
            else {
                int lw = gfx_text_w_px(title, FS_TITLE);
                gfx_text_px(&a->g, cx + (iw - lw) / 2, cy + (cell - FS_TITLE) / 2 - 2,
                            title, fg, FS_TITLE);
            }
            if (sub)
                gfx_text_fit_px(&a->g, cx + DP(7), sub_y, iw - DP(14), sub,
                                fg2, FS_TINY);
            if (ui_hit(&a->u, cx, cy, iw, cell)) widget_activate(a, it);
        }
        return h;
    }

    if (!strcmp(t, "progress")) {
        int pct = json_int(json_get(w, "pct"), 0);
        const char *label = tpl(json_str(json_get(w, "text"), NULL));
        if (label) gfx_text_px(&a->g, x + 2, y + DP(4), label, tc, FS_SMALL);
        char pb[16];
        snprintf(pb, sizeof(pb), "%d%%", pct);
        gfx_text_right_px(&a->g, x + cw - 2, y + DP(4), pb, sc, FS_SMALL);
        int by = y + DP(20), bh = DP(5);
        gfx_round(&a->g, x + 2, by, cw - 4, bh, bh / 2, col(0xE1E5EB, 0x2C313A));
        int fw = (cw - 4) * (pct > 100 ? 100 : pct) / 100;
        if (fw > 2) gfx_round(&a->g, x + 2, by, fw, bh, bh / 2, a->color);
        return h;
    }

    if (!strcmp(t, "input")) {
        const char *label = tpl(json_str(json_get(w, "title"), NULL));
        const char *key = json_str(json_get(w, "key"), "");
        const char *ph = tpl(json_str(json_get(w, "hint"), ""));
        int id = (int)((uintptr_t)w & 0x7fffffff);
        int yy = y;
        if (label) {
            gfx_text_px(&a->g, x + DP(2), yy + 2, label, sc, FS_TINY);
            yy += FS_TINY + DP(4);
        }
        char *buf = (char *)var_get(a, key, "");
        int password = json_bool(json_get(w, "password"), 0);
        int focused = (a->focus_id == id && a->focus_key[0]);
        int ih = DP(26);
        gfx_round(&a->g, x, yy, cw, ih, DP(6), col(0xF0F2F5, 0x262B33));
        if (focused) gfx_round_outline(&a->g, x, yy, cw, ih, DP(6), 2, a->color);

        char shown[192];
        snprintf(shown, sizeof(shown), "%s", buf);
        if (password) {
            size_t n = strlen(buf);
            for (size_t i = 0; i < n && i < sizeof(shown) - 1; i++) shown[i] = '*';
        }
        if (shown[0])
            gfx_text_px(&a->g, x + 14, yy + 16, shown, tc, FS_BODY);
        else
            gfx_text_px(&a->g, x + 14, yy + 16, ph, col(JK_MUTE, 0x6B7280), FS_BODY);

        if (focused) {
            int cw2 = px_w(shown, FS_BODY);
            gfx_fill(&a->g, x + 15 + cw2, yy + 14, 2, 24, a->color);
        }

        if (ui_hit(&a->u, x, yy, cw, 52)) {
            a->focus_id = id;
            snprintf(a->focus_key, sizeof(a->focus_key), "%s", key);
            a->redraw = 1;
        }
        return h;
    }

    if (!strcmp(t, "image")) {
        const char *ic = json_str(json_get(w, "icon"), "store");
        const char *title = tpl(json_str(json_get(w, "title"), NULL));
        const char *sub = tpl(json_str(json_get(w, "sub"), NULL));
        int size = DP(44);
        draw_icon_tile(a, x + (cw - size) / 2, y + DP(6), size, ic, a->color);
        int ty = y + DP(6) + size + DP(7);
        if (title) { gfx_text_center_px(&a->g, x + cw / 2, ty, title, tc, FS_TITLE); ty += FS_TITLE + DP(4); }
        if (sub) gfx_text_center_px(&a->g, x + cw / 2, ty, sub, sc, FS_SMALL);
        return h;
    }

    if (!strcmp(t, "keyvalue")) {
        const char *k = json_str(json_get(w, "key"), "");
        const char *v = tpl(json_str(json_get(w, "value"), ""));
        int ky = y + (DP(28) - FS_SMALL) / 2 - 2;
        gfx_text_px(&a->g, x + DP(2), ky, k, sc, FS_SMALL);
        gfx_text_right_px(&a->g, x + cw - DP(2), ky, v, tc, FS_SMALL);
        gfx_fill_a(&a->g, x + 4, y + h - 1, cw - 8, 1, col(JK_LINE, JK_LINE_D), 255);
        return h;
    }

    if (!strcmp(t, "stats")) {
        jval_t *items = json_get(w, "items");
        int n = json_len(items);
        if (n <= 0) return h;
        int tw = cw / n;
        for (int i = 0; i < n; i++) {
            jval_t *it = json_at(items, i);
            const char *v = tpl(json_str(json_get(it, "value"), ""));
            const char *l = json_str(json_get(it, "label"), "");
            int cx = x + i * tw;
            gfx_round(&a->g, cx, y + 2, tw - DP(4), DP(48), DP(6), cardc);
            gfx_text_center_px(&a->g, cx + (tw - DP(4)) / 2, y + DP(10), v, a->color, FS_H1);
            gfx_text_center_px(&a->g, cx + (tw - DP(4)) / 2, y + DP(10) + FS_H1 + DP(6),
                               l, sc, FS_TINY);
        }
        return h;
    }

    /* 未知类型：按 row 处理，避免整页错位 */
    const char *title = tpl(json_str(json_get(w, "title"), ""));
    gfx_text_px(&a->g, x + 6, y + 16, title, tc, FS_BODY);
    return h;
}

/* ================= 页面 ================= */

static void draw_back_arrow(int x, int y, uint32_t c)
{
    gfx_line(&A.g, x + 7, y, x - 2, y + 8, 2.0f, c);
    gfx_line(&A.g, x - 2, y + 8, x + 7, y + 16, 2.0f, c);
}

static void draw_app_bar(app_t *a, jval_t *page, int W)
{
    const char *title = json_str(json_get(page, "title"), a->name);
    int H = json_int(json_get(page, "bar"), 62);
    uint32_t bg = col(JK_CARD, JK_CARD_D);
    gfx_fill(&a->g, 0, 0, W, H, bg);
    gfx_fill_a(&a->g, 0, H - 1, W, 1, col(JK_LINE, JK_LINE_D), 255);

    int back_x = 22;
    draw_back_arrow(back_x, H / 2 - 8, col(JK_TEXT, JK_TEXT_D));
    if (ui_hit(&a->u, 4, 6, 54, H - 12)) pop_page(a);

    gfx_text_px(&a->g, 56, H / 2 - FS_TITLE / 2 - 1, title, col(JK_TEXT, JK_TEXT_D), FS_TITLE);

    /* 右侧关闭 */
    int cx = W - 40;
    uint32_t cc = col(0x8A9099, 0x9BA3B0);
    gfx_line(&a->g, cx - 7, H / 2 - 7, cx + 7, H / 2 + 7, 2.0f, cc);
    gfx_line(&a->g, cx + 7, H / 2 - 7, cx - 7, H / 2 + 7, 2.0f, cc);
    if (ui_hit(&a->u, W - 64, 0, 64, H)) a->quit = 1;

    a->content_top = H;
}

static void draw_footer(app_t *a, jval_t *page, int W, int H)
{
    jval_t *foot = json_get(page, "footer");
    int n = json_len(foot);
    if (n <= 0) { a->content_bottom = H; return; }

    int fh = 76 + a->kb_h;
    gfx_fill(&a->g, 0, H - fh, W, fh, col(JK_CARD, JK_CARD_D));
    gfx_fill_a(&a->g, 0, H - fh, W, 1, col(JK_LINE, JK_LINE_D), 255);
    int gap = 12;
    int bw = (W - 24 - gap * (n - 1)) / n;
    for (int i = 0; i < n; i++) {
        jval_t *b = json_at(foot, i);
        const char *label = json_str(json_get(b, "text"), "");
        const char *kind = json_str(json_get(b, "kind"), i == n - 1 ? "primary" : "secondary");
        int bx = 12 + i * (bw + gap);
        int bh = 52;
        uint32_t bgc = a->color, fg = C_WHITE;
        if (!strcmp(kind, "secondary")) { bgc = col(0xE9ECF1, 0x2C313A); fg = col(JK_TEXT, JK_TEXT_D); }
        else if (!strcmp(kind, "danger")) bgc = 0xD93B3B;
        if (pressed(a, bx, H - fh + 12, bw, bh)) bgc = gfx_darken(bgc, 24);
        gfx_round(&a->g, bx, H - fh + 12, bw, bh, 12, bgc);
        gfx_text_center_px(&a->g, bx + bw / 2, H - fh + 12 + (bh - FS_BODY) / 2, label, fg, FS_BODY);
        if (ui_hit(&a->u, bx, H - fh + 12, bw, bh)) widget_activate(a, b);
    }
    a->content_bottom = H - fh;
}

static void draw_page(app_t *a)
{
    jval_t *page = cur_page(a);
    int W = a->g.w, H = a->g.h;

    gfx_fill(&a->g, 0, 0, W, H, col(JK_BG, JK_BG_D));
    if (!page) {
        gfx_text_center_px(&a->g, W / 2, H / 2, "界面文件缺失", JK_TEXT, FS_BODY);
        return;
    }

    draw_app_bar(a, page, W);
    draw_footer(a, page, W, H);

    int pad = a->wide ? (W - 760) / 2 : 16;
    if (pad < 12) pad = 12;
    int cw = W - pad * 2;
    jval_t *widgets = json_get(page, "widgets");
    int n = json_len(widgets);

    int total = measure_children(a, widgets, cw);
    int view_h = a->content_bottom - a->content_top;
    a->scroll_max = total > view_h ? total - view_h : 0;
    if (a->scroll > a->scroll_max) a->scroll = a->scroll_max;
    if (a->scroll < 0) a->scroll = 0;

    gfx_set_clip(&a->g, 0, a->content_top, W, view_h);
    int y = a->content_top + 12 - a->scroll;
    for (int i = 0; i < n; i++) {
        jval_t *w = json_at(widgets, i);
        int h = measure_widget(a, w, cw);
        /* 屏幕外不绘制，但保留高度，保证滚动位置稳定 */
        if (y + h >= a->content_top && y <= a->content_bottom)
            draw_widget(a, w, pad, y, cw, a->content_top, a->content_bottom);
        y += h;
        if (i > 400) break;
    }
    gfx_reset_clip(&a->g);

    /* 滚动条指示（内容较长时） */
    if (a->scroll_max > 0) {
        int bar_h = view_h * view_h / total;
        if (bar_h < 40) bar_h = 40;
        int by = a->content_top + (view_h - bar_h) * a->scroll / a->scroll_max;
        gfx_round_a(&a->g, W - 5, by, 3, bar_h, 2, col(0x9AA1AC, 0x6B7280), 120);
    }
}

static void draw_toast(app_t *a)
{
    if (!a->toast[0] || mono_ms() > a->toast_until) return;
    int tw = px_w(a->toast, FS_SMALL) + 44;
    if (tw > a->g.w - 40) tw = a->g.w - 40;
    int x = (a->g.w - tw) / 2;
    int y = a->g.h - a->kb_h - 96;
    gfx_round(&a->g, x, y, tw, 46, 23, 0x1F232B);
    gfx_text_fit_px(&a->g, x + 22, y + 14, tw - 44, a->toast, C_WHITE, FS_SMALL);
}

static void draw_keyboard(app_t *a)
{
    if (!a->focus_key[0]) { a->kb_h = 0; return; }
    int kb_h = a->g.h * 38 / 100;
    if (kb_h < 300) kb_h = 300;
    if (kb_h > 420) kb_h = 420;
    a->kb_h = kb_h;

    char *buf = (char *)var_get(a, a->focus_key, "");
    int cap = (int)sizeof(a->vars[0].val);
    /* var_get 返回的是只读指针，这里直接操作可变副本再写回 */
    char tmp[192];
    snprintf(tmp, sizeof(tmp), "%s", buf);

    int done = ui_keyboard(&a->u, 0, a->g.h - kb_h, a->g.w, kb_h, tmp, cap);
    if (strcmp(tmp, buf)) {
        var_set(a, a->focus_key, tmp);
        state_save_key(a, a->focus_key);
        a->redraw = 1;
    }
    if (done) a->focus_key[0] = '\0';
}

/* ================= 触摸 ================= */

static void handle_pointer(app_t *a, const in_event_t *ev, int n)
{
    for (int i = 0; i < n; i++) {
        const in_event_t *e = &ev[i];
        /* PROPEROS_JSPK_DEBUG=1 时把手势事件写进内核日志，方便在真机/串口上排查 */
        if (g_debug && e->kind != IN_MOVE && e->kind != IN_UP) {
            int f = open("/dev/kmsg", O_WRONLY);
            if (f >= 0) {
                dprintf(f, "properos-jspk[%s]: kind=%d x0=%d y0=%d dx=%d dy=%d\n",
                        a->id, (int)e->kind, e->x0, e->y0, e->dx, e->dy);
                close(f);
            }
        }
        switch (e->kind) {
        case IN_DOWN:
            a->drag_active = 0;
            a->drag_from = a->scroll;
            a->drag_y0 = e->y;
            a->fling = 0;
            break;
        case IN_MOVE:
            if (a->u.down && !a->drag_active) {
                int dy = e->y - a->drag_y0;
                if (dy > 12 || dy < -12) a->drag_active = 1;
            }
            if (a->drag_active) {
                a->scroll = a->drag_from - (e->y - a->drag_y0);
                if (a->scroll < 0) a->scroll = 0;
                if (a->scroll > a->scroll_max) a->scroll = a->scroll_max;
                a->fling = (float)(-e->vy);
                a->redraw = 1;
            }
            break;
        case IN_UP:
            if (a->drag_active) {
                a->drag_active = 0;
            }
            break;
        case IN_EDGE_LEFT:
        case IN_EDGE_RIGHT:
            /* 边缘内滑就是返回：清掉输入焦点并退栈，不能只清焦点 */
            a->focus_key[0] = '\0';
            pop_page(a);
            break;
        case IN_KEY:
            break;
        default:
            break;
        }
    }
}

/* ================= 主循环 ================= */

static void on_signal(int s) { (void)s; g_quit = 1; }

static int load_package(app_t *a, const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) return -1;

    char manpath[512];
    if (S_ISDIR(st.st_mode)) {
        snprintf(a->dir, sizeof(a->dir), "%s", path);
        snprintf(manpath, sizeof(manpath), "%s/manifest.json", path);
        size_t len = 0;
        char *txt = read_text(manpath, &len);
        if (!txt) return -1;
        a->man = json_parse(txt);
        free(txt);
        if (!a->man) return -1;
        snprintf(a->entry, sizeof(a->entry), "%s",
                 json_str(json_get(a->man, "entry"), "ui/main.json"));
        snprintf(manpath, sizeof(manpath), "%s/%s", path, a->entry);
        txt = read_text(manpath, &len);
        if (!txt) return -1;
        a->doc = json_parse(txt);
        free(txt);
        if (!a->doc) return -1;
    } else {
        if (zip_open_file(&a->zip, path) != 0) return -1;
        const zip_ent_t *e = zip_find(&a->zip, "manifest.json");
        uint8_t *out = NULL;
        size_t len = 0;
        if (!e || zip_read(&a->zip, e, &out, &len) != 0) return -1;
        a->man = json_parse((char *)out);
        free(out);
        if (!a->man) return -1;

        snprintf(a->entry, sizeof(a->entry), "%s",
                 json_str(json_get(a->man, "entry"), "ui/main.json"));
        e = zip_find(&a->zip, a->entry);
        if (!e) return -1;
        out = NULL;
        if (zip_read(&a->zip, e, &out, &len) != 0) return -1;
        a->doc = json_parse((char *)out);
        free(out);
        if (!a->doc) return -1;
    }

    snprintf(a->id, sizeof(a->id), "%s", json_str(json_get(a->man, "id"), "unknown"));
    snprintf(a->name, sizeof(a->name), "%s", json_str(json_get(a->man, "name"), "应用"));
    snprintf(a->icon, sizeof(a->icon), "%s", json_str(json_get(a->man, "icon"), "store"));
    snprintf(a->developer, sizeof(a->developer), "%s",
             json_str(json_get(a->man, "developer"), "ByUsi Studio · 北啊呢"));
    snprintf(a->summary, sizeof(a->summary), "%s", json_str(json_get(a->man, "summary"), ""));
    const char *c = json_str(json_get(a->man, "color"), "#3E7BFF");
    a->color = (uint32_t)strtoul(c[0] == '#' ? c + 1 : c, NULL, 16);
    if (!a->color) a->color = 0x3E7BFF;

    jval_t *perms = json_get(a->man, "permissions");
    for (int i = 0; i < json_len(perms); i++) {
        const char *p = json_str(json_at(perms, i), "");
        if (!strcmp(p, "run")) a->allow_run = 1;
        if (!strcmp(p, "url")) a->allow_url = 1;
    }

    /* 初始状态：仅在状态文件里没有该键时生效 */
    jval_t *initv = json_get(a->man, "init");
    if (initv && initv->t == JOBJ) {
        for (jval_t *c = initv->child; c; c = c->next) {
            if (!c->key) continue;
            char v[96];
            if (c->t == JNUM) fmt_num(c->num, v, sizeof(v));
            else if (c->t == JBOOL) snprintf(v, sizeof(v), "%d", c->b);
            else snprintf(v, sizeof(v), "%s", json_str(c, ""));
            var_set(a, c->key, v);
        }
    }

    a->pages = json_get(a->doc, "pages");
    if (!a->pages) return -1;
    const char *start = json_str(json_get(a->doc, "start"), "root");
    a->depth = 0;
    push_page(a, start);
    return 0;
}

int main(int argc, char **argv)
{
    g_debug = getenv("PROPEROS_JSPK_DEBUG") != NULL;
    const char *pkg = NULL;
    const char *render = NULL;      /* WxH */
    const char *render_out = NULL;  /* out.bmp */
    int render_page = 1;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--render") && i + 2 < argc) {
            render = argv[i + 1];
            render_out = argv[i + 2];
            if (i + 3 < argc && argv[i + 3][0] != '-') render_page = atoi(argv[i + 3]);
            i += (i + 3 < argc && argv[i + 3][0] != '-') ? 3 : 2;
        } else if (!strcmp(argv[i], "run") && i + 1 < argc) {
            pkg = argv[++i];
        } else if (argv[i][0] != '-') {
            pkg = argv[i];
        }
    }
    if (!pkg) {
        fprintf(stderr, "用法：properos-jspk <应用.jspk|目录> [--render WxH file.bmp 页面]\n");
        return 2;
    }

    memset(&A, 0, sizeof(A));
    A.focus_id = -1;
    if (load_package(&A, pkg) != 0) {
        fprintf(stderr, "无法加载应用包：%s\n", pkg);
        return 1;
    }
    state_load(&A);

    char dv[8];
    if (sys_get("dark", dv, sizeof(dv))) A.dark = atoi(dv);

    if (render) {
        int rw = 1280, rh = 800;
        if (sscanf(render, "%dx%d", &rw, &rh) != 2) { rw = 1280; rh = 800; }
        A.tty_w = rw; A.tty_h = rh;
        if (gfx_offscreen(&A.g, rw, rh) < 0) return 1;
    } else {
        if (gfx_open(&A.g, NULL) != 0) return 1;
        gfx_console_grab(7);
        atexit(gfx_console_release);
        input_open(&A.in, A.g.w, A.g.h);
        signal(SIGINT, on_signal);
        signal(SIGTERM, on_signal);
    }
    A.wide = A.g.w > A.g.h;
    /* 720x1280 是 2 倍密度屏：字号按短边 / 360 缩放 */
    int base = A.g.w < A.g.h ? A.g.w : A.g.h;
    float dn = (float)base / 360.0f;
    if (dn < 1.5f) dn = 1.5f;
    if (dn > 3.2f) dn = 3.2f;
    A.dn = dn;
    FS_TINY = (int)(11 * dn);
    FS_SMALL = (int)(13 * dn);
    FS_BODY = (int)(15 * dn);
    FS_TITLE = (int)(19 * dn);
    FS_H1 = (int)(24 * dn);
    FS_HERO = (int)(32 * dn);
    ui_init(&A.u, &A.g);

    if (render) {
        /* 依次压栈到目标页（按声明顺序） */
        int idx = 1;
        for (jval_t *p = A.pages->child; p && idx < render_page; p = p->next) {
            if (p->key) push_page(&A, p->key);
            idx++;
        }
        ui_clear(&A.u);
        draw_page(&A);
        gfx_save_bmp(&A.g, render_out);
        gfx_close(&A.g);
        return 0;
    }

    int64_t last_fling = mono_ms();
    while (!g_quit && !A.quit) {
        in_event_t ev[32];
        int n = input_poll(&A.in, ev, 32, 16);
        for (int i = 0; i < n; i++) ui_feed(&A.u, &ev[i]);
        handle_pointer(&A, ev, n);
        ui_begin(&A.u);

        draw_page(&A);
        draw_keyboard(&A);
        draw_toast(&A);
        gfx_flush(&A.g);

        /* 惯性滚动 */
        int64_t now = mono_ms();
        float dt = (float)(now - last_fling) / 16.0f;
        last_fling = now;
        if (!A.drag_active && (A.fling > 0.4f || A.fling < -0.4f) && A.scroll_max > 0) {
            A.scroll += (int)(A.fling * dt * 1.6f);
            A.fling *= 0.92f;
            if (A.scroll < 0) { A.scroll = 0; A.fling = 0; }
            if (A.scroll > A.scroll_max) { A.scroll = A.scroll_max; A.fling = 0; }
        }
        ui_end(&A.u);
        if (A.dark) {
            char d2[8];
            if (sys_get("dark", d2, sizeof(d2))) A.dark = atoi(d2);
        }
    }

    input_close(&A.in);
    gfx_close(&A.g);
    gfx_console_release();
    zip_close(&A.zip);
    json_free(A.man);
    json_free(A.doc);
    return 0;
}
