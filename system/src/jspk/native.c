/* ProperOS 25.0.0 Mobile · jspk 原生后端实现
 *
 * 这里没有一条预置数据：
 *   文件列表 = opendir/stat 的真实结果
 *   音乐     = 磁盘上真实存在的音频文件，播放入口是真实的 PCM 进程
 *   包       = /var/lib/ppkg/db 与 /usr/share/properos/apps 的真实内容
 *   状态     = /sys、/proc、statvfs 的实时读数
 *   通话短信 = 本机真实发生的记录（没有 modem 就如实报告没有）
 *
 * 开发者：ByUsi Studio · 北啊呢
 */
#define _GNU_SOURCE
#include "native.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/statvfs.h>
#include <linux/videodev2.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define P_SMS_DIR  "/data/phone/sms"
#define P_CALL_LOG "/data/phone/calls.log"
#define P_MEDIA_PID "/run/properos-media.pid"
#define P_MEDIA_TRK "/run/properos-media.track"
#define P_PKG_CACHE "/var/cache/properos/ppkg"

static int64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void mkdir_p(const char *path)
{
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') { *p = '\0'; mkdir(tmp, 0755); *p = '/'; }
    }
    mkdir(tmp, 0755);
}

static int rd_file(const char *path, char *out, size_t n)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    ssize_t g = read(fd, out, n - 1);
    close(fd);
    if (g < 0) return -1;
    out[g] = '\0';
    while (g > 0 && (out[g - 1] == '\n' || out[g - 1] == '\r')) out[--g] = '\0';
    return (int)g;
}

static int rd_line(const char *path, char *out, size_t n)
{
    if (rd_file(path, out, n) < 0) return -1;
    return out[0] ? 0 : -1;
}

static void wr_file(const char *path, const char *s)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return;
    ssize_t ignored = write(fd, s, strlen(s));
    (void)ignored;
    close(fd);
}

static void fmt_size(long long b, char *out, size_t n)
{
    if (b < 1024) snprintf(out, n, "%lld B", b);
    else if (b < 1024LL * 1024) snprintf(out, n, "%lld KB", (b + 512) / 1024);
    else if (b < 1024LL * 1024 * 1024) snprintf(out, n, "%.1f MB", (double)b / (1024.0 * 1024.0));
    else snprintf(out, n, "%.1f GB", (double)b / (1024.0 * 1024.0 * 1024.0));
}

/* 今天 09:12 / 昨天 20:41 / 11-20 */
static void fmt_when(time_t t, char *out, size_t n)
{
    time_t now = time(NULL);
    struct tm a, b;
    localtime_r(&t, &a);
    localtime_r(&now, &b);
    long days = (long)((now - t) / 86400);
    if (a.tm_year == b.tm_year && a.tm_yday == b.tm_yday)
        snprintf(out, n, "今天 %02d:%02d", a.tm_hour, a.tm_min);
    else if (days <= 1 && a.tm_yday == b.tm_yday - 1)
        snprintf(out, n, "昨天 %02d:%02d", a.tm_hour, a.tm_min);
    else if (a.tm_year == b.tm_year)
        snprintf(out, n, "%02d-%02d", a.tm_mon + 1, a.tm_mday);
    else
        snprintf(out, n, "%d-%02d-%02d", a.tm_year + 1900, a.tm_mon + 1, a.tm_mday);
}

static const char *ext_of(const char *name)
{
    const char *d = strrchr(name, '.');
    return d && d[1] ? d + 1 : "";
}

static int ext_in(const char *e, const char *const *list)
{
    for (int i = 0; list[i]; i++)
        if (!strcasecmp(e, list[i])) return 1;
    return 0;
}

static void kind_of(const char *name, int isdir, const char **icon, const char **desc)
{
    static const char *img[] = { "jpg", "jpeg", "png", "webp", "gif", "bmp", "heic", NULL };
    static const char *aud[] = { "wav", "mp3", "flac", "ogg", "opus", "m4a", "aac", NULL };
    static const char *vid[] = { "mp4", "mkv", "webm", "mov", "avi", NULL };
    static const char *txt[] = { "md", "txt", "log", "conf", "json", "c", "h", "sh", "py", "ini", NULL };
    static const char *pkg[] = { "deb", "ppkg", "jspk", "papp", NULL };
    static const char *imgl[] = { "iso", "img", "qcow2", NULL };

    if (isdir) { *icon = "folder"; *desc = "文件夹"; return; }
    const char *e = ext_of(name);
    if (ext_in(e, img)) { *icon = "photos"; *desc = "图片"; return; }
    if (ext_in(e, aud)) { *icon = "music"; *desc = "音频"; return; }
    if (ext_in(e, vid)) { *icon = "play"; *desc = "视频"; return; }
    if (ext_in(e, txt)) { *icon = "notes"; *desc = "文本"; return; }
    if (ext_in(e, pkg)) { *icon = "download"; *desc = "安装包"; return; }
    if (ext_in(e, imgl)) { *icon = "chip"; *desc = "镜像"; return; }
    *icon = "files";
    *desc = e[0] ? "文件" : "文件";
}

static int has_cmd(const char *name)
{
    const char *path = getenv("PATH");
    if (!path) path = "/usr/bin:/bin:/usr/sbin:/sbin";
    char buf[1024];
    snprintf(buf, sizeof(buf), "%s", path);
    char *save = NULL;
    for (char *d = strtok_r(buf, ":", &save); d; d = strtok_r(NULL, ":", &save)) {
        char p[512];
        snprintf(p, sizeof(p), "%s/%s", d, name);
        if (access(p, X_OK) == 0) return 1;
    }
    return 0;
}

/* 带超时的外部命令，避免 nmcli / mmcli 无响应时卡死界面 */
static int run_capture(char *const argv[], char *out, size_t n, int timeout_ms)
{
    int pfd[2];
    if (pipe(pfd) < 0) return -1;
    pid_t pid = fork();
    if (pid == 0) {
        close(pfd[0]);
        dup2(pfd[1], 1);
        dup2(pfd[1], 2);
        close(pfd[1]);
        int dn = open("/dev/null", O_RDONLY);
        if (dn >= 0) { dup2(dn, 0); close(dn); }
        execvp(argv[0], argv);
        _exit(127);
    }
    close(pfd[1]);
    size_t used = 0;
    int64_t end = now_ms() + timeout_ms;
    int status = 0, timedout = 0;
    if (out && n) out[0] = '\0';
    for (;;) {
        int left = (int)(end - now_ms());
        if (left < 0) left = 0;
        struct pollfd p = { pfd[0], POLLIN, 0 };
        int r = poll(&p, 1, left);
        if (r == 0) { timedout = 1; kill(pid, SIGKILL); break; }
        if (r < 0) { if (errno == EINTR) continue; break; }
        char b[512];
        ssize_t g = read(pfd[0], b, sizeof(b));
        if (g <= 0) break;
        if (out && used + 1 < n) {
            size_t c = (size_t)g;
            if (c > n - 1 - used) c = n - 1 - used;
            memcpy(out + used, b, c);
            used += c;
            out[used] = '\0';
        }
    }
    close(pfd[0]);
    waitpid(pid, &status, 0);
    if (timedout) return -2;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static pid_t spawn_bg(char *const argv[])
{
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        int dn = open("/dev/null", O_RDWR);
        if (dn >= 0) { dup2(dn, 0); dup2(dn, 1); dup2(dn, 2); close(dn); }
        execvp(argv[0], argv);
        _exit(127);
    }
    return pid;
}

/* 音频状态（实现在下方） */
static int media_pid(void);
static const char *media_track(void);

/* ================= 状态（/proc /sys，2 秒缓存） ================= */

#define STATUS_SLOTS 12
typedef struct {
    char key[24];
    char val[96];
    int64_t ms;
} status_t;
static status_t g_st[STATUS_SLOTS];
static int g_st_n;

static void status_put(const char *key, const char *val)
{
    if (g_st_n >= STATUS_SLOTS) return;
    snprintf(g_st[g_st_n].key, sizeof(g_st[0].key), "%s", key);
    snprintf(g_st[g_st_n].val, sizeof(g_st[0].val), "%s", val);
    g_st[g_st_n].ms = now_ms();
    g_st_n++;
}

static const char *status_find(const char *key)
{
    for (int i = 0; i < g_st_n; i++)
        if (!strcmp(g_st[i].key, key)) return g_st[i].val;
    return NULL;
}

/* 找第一个满足前缀与属性值的 power_supply 节点 */
static int batt_capacity(void)
{
    DIR *d = opendir("/sys/class/power_supply");
    if (!d) return -1;
    struct dirent *de;
    int cap = -1;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.') continue;
        char p[512], type[32];
        snprintf(p, sizeof(p), "/sys/class/power_supply/%s/type", de->d_name);
        if (rd_line(p, type, sizeof(type)) < 0) continue;
        if (strcmp(type, "Battery")) continue;
        snprintf(p, sizeof(p), "/sys/class/power_supply/%s/capacity", de->d_name);
        char v[16];
        if (rd_line(p, v, sizeof(v)) == 0) { cap = atoi(v); break; }
    }
    closedir(d);
    return cap;
}

static void batt_status(char *out, size_t n)
{
    int cap = batt_capacity();
    if (cap < 0) {
        snprintf(out, n, "交流电");
        return;
    }
    char st[32] = "", p[512];
    snprintf(p, sizeof(p), "/sys/class/power_supply/BAT0/status");
    if (rd_line(p, st, sizeof(st)) < 0) {
        /* 名字不一定是 BAT0，重新找一个 Battery 节点的 status */
        DIR *d = opendir("/sys/class/power_supply");
        struct dirent *de;
        while (d && (de = readdir(d))) {
            if (de->d_name[0] == '.') continue;
            char t[32];
            snprintf(p, sizeof(p), "/sys/class/power_supply/%s/type", de->d_name);
            if (rd_line(p, t, sizeof(t)) < 0 || strcmp(t, "Battery")) continue;
            snprintf(p, sizeof(p), "/sys/class/power_supply/%s/status", de->d_name);
            rd_line(p, st, sizeof(st));
            break;
        }
        if (d) closedir(d);
    }
    if (!strcmp(st, "Charging")) snprintf(out, n, "%d%% · 充电中", cap);
    else if (!strcmp(st, "Full")) snprintf(out, n, "%d%% · 已充满", cap);
    else if (!strcmp(st, "Discharging")) snprintf(out, n, "%d%% · 放电中", cap);
    else snprintf(out, n, "%d%%", cap);
}

static void wifi_status(char *out, size_t n)
{
    DIR *d = opendir("/sys/class/net");
    if (!d) { snprintf(out, n, "无网络设备"); return; }
    struct dirent *de;
    int found = 0, up = 0;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.') continue;
        if (!strcmp(de->d_name, "lo")) continue;
        char p[512], op[32];
        snprintf(p, sizeof(p), "/sys/class/net/%s/wireless", de->d_name);
        int wireless = (access(p, F_OK) == 0);
        if (!wireless) {
            /* 有些驱动不建 wireless 目录，用 phy80211 判断 */
            snprintf(p, sizeof(p), "/sys/class/net/%s/phy80211", de->d_name);
            wireless = (access(p, F_OK) == 0);
        }
        if (!wireless) continue;
        found = 1;
        snprintf(p, sizeof(p), "/sys/class/net/%s/operstate", de->d_name);
        if (rd_line(p, op, sizeof(op)) == 0 && !strcmp(op, "up")) up = 1;
    }
    closedir(d);
    if (!found) snprintf(out, n, "无 Wi-Fi 设备");
    else snprintf(out, n, "%s", up ? "已连接" : "未连接");
}

static void mem_status(char *out, size_t n)
{
    char buf[4096];
    if (rd_file("/proc/meminfo", buf, sizeof(buf)) < 0) { snprintf(out, n, "未知"); return; }
    long total = 0, avail = 0;
    char *save = NULL;
    for (char *l = strtok_r(buf, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
        long v = 0;
        if (sscanf(l, "MemTotal: %ld", &v) == 1) total = v;
        else if (sscanf(l, "MemAvailable: %ld", &v) == 1) avail = v;
    }
    if (total <= 0) { snprintf(out, n, "未知"); return; }
    snprintf(out, n, "%.1f / %.1f GB", (double)(total - avail) / (1024.0 * 1024.0),
             (double)total / (1024.0 * 1024.0));
}

static void disk_status(const char *path, char *out, size_t n)
{
    struct statvfs v;
    if (statvfs(path, &v) != 0) { snprintf(out, n, "不可用"); return; }
    unsigned long long bs = v.f_frsize ? v.f_frsize : v.f_bsize;
    double tot = (double)v.f_blocks * bs, freeb = (double)v.f_bavail * bs;
    snprintf(out, n, "已用 %.1f / %.1f GB", (tot - freeb) / (1024.0 * 1024.0 * 1024.0),
             tot / (1024.0 * 1024.0 * 1024.0));
}

static void uptime_status(char *out, size_t n)
{
    char buf[64];
    if (rd_file("/proc/uptime", buf, sizeof(buf)) < 0) { snprintf(out, n, "未知"); return; }
    long s = (long)strtod(buf, NULL);
    long d = s / 86400, h = (s % 86400) / 3600, m = (s % 3600) / 60;
    if (d > 0) snprintf(out, n, "%ld 天 %ld 小时", d, h);
    else if (h > 0) snprintf(out, n, "%ld 小时 %ld 分", h, m);
    else snprintf(out, n, "%ld 分", m);
}

static void cpu_status(char *out, size_t n)
{
    char buf[8192];
    if (rd_file("/proc/cpuinfo", buf, sizeof(buf)) < 0) { snprintf(out, n, "未知"); return; }
    char *save = NULL;
    for (char *l = strtok_r(buf, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
        char *colon = strchr(l, ':');
        if (!colon) continue;
        *colon = '\0';
        char *k = l, *v = colon + 1;
        while (*v == ' ' || *v == '\t') v++;
        if (!strncmp(k, "Model", 5) || !strncmp(k, "model name", 10) || !strcmp(k, "Hardware")) {
            snprintf(out, n, "%s", v);
            return;
        }
    }
    snprintf(out, n, "未知");
}

static void model_status(char *out, size_t n)
{
    static const char *paths[] = {
        "/proc/device-tree/model",
        "/sys/firmware/devicetree/base/model",
        "/sys/class/dmi/id/product_name",
        NULL
    };
    for (int i = 0; paths[i]; i++) {
        char buf[128];
        if (rd_file(paths[i], buf, sizeof(buf)) >= 0 && buf[0]) {
            snprintf(out, n, "%s", buf);
            return;
        }
    }
    snprintf(out, n, "未知设备");
}

static void temp_status(char *out, size_t n)
{
    for (int i = 0; i < 8; i++) {
        char p[128], v[32];
        snprintf(p, sizeof(p), "/sys/class/thermal/thermal_zone%d/temp", i);
        if (rd_line(p, v, sizeof(v)) < 0) continue;
        long t = atol(v);
        if (t > 1000) t /= 1000;
        if (t > 0 && t < 150) { snprintf(out, n, "%ld °C", t); return; }
    }
    snprintf(out, n, "无传感器");
}

const char *nv_status(const char *key)
{
    for (int i = 0; i < g_st_n; i++) {
        if (strcmp(g_st[i].key, key)) continue;
        if (now_ms() - g_st[i].ms < 2000) return g_st[i].val;
        g_st[i].ms = now_ms();
        char v[96] = "";
        if (!strcmp(key, "battery")) batt_status(v, sizeof(v));
        else if (!strcmp(key, "wifi")) wifi_status(v, sizeof(v));
        else if (!strcmp(key, "mem")) mem_status(v, sizeof(v));
        else if (!strcmp(key, "disk")) disk_status("/data", v, sizeof(v));
        else if (!strcmp(key, "rootdisk")) disk_status("/", v, sizeof(v));
        else if (!strcmp(key, "uptime")) uptime_status(v, sizeof(v));
        else if (!strcmp(key, "cpu")) cpu_status(v, sizeof(v));
        else if (!strcmp(key, "model")) model_status(v, sizeof(v));
        else if (!strcmp(key, "temp")) temp_status(v, sizeof(v));
    else if (!strcmp(key, "media")) {
        const char *t = media_track();
        const char *base = t[0] ? (strrchr(t, '/') ? strrchr(t, '/') + 1 : t) : "";
        if (media_pid() > 0) snprintf(v, sizeof(v), "%s", base[0] ? base : "播放中");
        else snprintf(v, sizeof(v), "%s", "未播放");
    }
        else if (!strcmp(key, "media")) {
            const char *t = media_track();
            const char *base = t[0] ? (strrchr(t, '/') ? strrchr(t, '/') + 1 : t) : "";
            if (media_pid() > 0) snprintf(v, sizeof(v), "%s", base[0] ? base : "播放中");
            else snprintf(v, sizeof(v), "%s", "未播放");
        }
        else if (!strcmp(key, "kernel")) {
            char b[64] = "";
            rd_line("/proc/sys/kernel/osrelease", b, sizeof(b));
            snprintf(v, sizeof(v), "%s", b);
        }
        snprintf(g_st[i].val, sizeof(g_st[i].val), "%s", v);
        return g_st[i].val;
    }
    if (g_st_n >= STATUS_SLOTS) g_st_n = 0;   /* 满了就重置，避免永久缺失 */
    char v[96] = "";
    if (!strcmp(key, "battery")) batt_status(v, sizeof(v));
    else if (!strcmp(key, "wifi")) wifi_status(v, sizeof(v));
    else if (!strcmp(key, "mem")) mem_status(v, sizeof(v));
    else if (!strcmp(key, "disk")) disk_status("/data", v, sizeof(v));
    else if (!strcmp(key, "rootdisk")) disk_status("/", v, sizeof(v));
    else if (!strcmp(key, "uptime")) uptime_status(v, sizeof(v));
    else if (!strcmp(key, "cpu")) cpu_status(v, sizeof(v));
    else if (!strcmp(key, "model")) model_status(v, sizeof(v));
    else if (!strcmp(key, "temp")) temp_status(v, sizeof(v));
    else if (!strcmp(key, "media")) {
        const char *t = media_track();
        const char *base = t[0] ? (strrchr(t, '/') ? strrchr(t, '/') + 1 : t) : "";
        if (media_pid() > 0) snprintf(v, sizeof(v), "%s", base[0] ? base : "播放中");
        else snprintf(v, sizeof(v), "%s", "未播放");
    }
    else if (!strcmp(key, "kernel")) {
        char b[64] = "";
        rd_line("/proc/sys/kernel/osrelease", b, sizeof(b));
        snprintf(v, sizeof(v), "%s", b);
    } else snprintf(v, sizeof(v), "%s", "—");
    status_put(key, v);
    return status_find(key);
}

/* ================= 音频 ================= */

/* WAV 头解析：返回 data 偏移与格式；0 表示不是可播放的 PCM WAV */
static int wav_info(const char *path, int *rate, int *ch, int *bits, long *data_off,
                    long long *data_len, int *seconds)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    unsigned char h[12];
    if (read(fd, h, 12) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) {
        close(fd);
        return 0;
    }
    int fmt = 0;
    for (;;) {
        unsigned char c[8];
        if (read(fd, c, 8) != 8) break;
        unsigned int sz = c[4] | (c[5] << 8) | (c[6] << 16) | ((unsigned)c[7] << 24);
        if (!memcmp(c, "fmt ", 4)) {
            unsigned char f[16];
            if (read(fd, f, 16) != 16) break;
            fmt = f[0] | (f[1] << 8);
            *ch = f[2] | (f[3] << 8);
            *rate = f[4] | (f[5] << 8) | (f[6] << 16) | ((unsigned)f[7] << 24);
            *bits = f[14] | (f[15] << 8);
            if (sz > 16) lseek(fd, sz - 16, SEEK_CUR);
        } else if (!memcmp(c, "data", 4)) {
            *data_off = lseek(fd, 0, SEEK_CUR);
            *data_len = sz;
            close(fd);
            if (fmt != 1 || *ch <= 0 || *rate <= 0) return 0;
            if (*bits != 16) return 0;
            long long bytes = (*data_len > 0) ? *data_len : 0;
            *seconds = (int)(bytes / (long long)(*rate * *ch * 2));
            return 1;
        } else {
            lseek(fd, sz + (sz & 1), SEEK_CUR);
        }
    }
    close(fd);
    return 0;
}

static int media_pid(void)
{
    char b[32];
    if (rd_line(P_MEDIA_PID, b, sizeof(b)) < 0) return -1;
    int pid = atoi(b);
    if (pid <= 0) return -1;
    if (kill(pid, 0) != 0) return -1;
    return pid;
}

static const char *media_track(void)
{
    static char b[512];
    if (rd_line(P_MEDIA_TRK, b, sizeof(b)) < 0) b[0] = '\0';
    return b;
}

static void media_stop(void)
{
    int pid = media_pid();
    if (pid > 0) kill(pid, SIGTERM);
    unlink(P_MEDIA_PID);
}

/* 真正把 PCM 送进音频服务：解析 WAV 头，把 data 段作为 pacat 的 stdin */
static int media_play(const char *path, char *msg, size_t n)
{
    media_stop();
    int rate = 0, ch = 0, bits = 0, secs = 0;
    long off = 0;
    long long len = 0;
    int pcm = wav_info(path, &rate, &ch, &bits, &off, &len, &secs);

    if (!has_cmd("pacat") && !has_cmd("paplay")) {
        snprintf(msg, n, "没有可用的音频服务客户端（pacat/paplay）");
        return 0;
    }

    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        int err = open("/run/properos-media.err", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (err >= 0) { dup2(err, 2); close(err); }
        else { int dn = open("/dev/null", O_RDWR); if (dn >= 0) dup2(dn, 2); }
        if (pcm) {
            int fd = open(path, O_RDONLY);
            if (fd < 0) _exit(2);
            lseek(fd, off, SEEK_SET);
            dup2(fd, 0);
            close(fd);
            char r[16], c[16];
            snprintf(r, sizeof(r), "--rate=%d", rate);
            snprintf(c, sizeof(c), "--channels=%d", ch);
            execlp("pacat", "pacat", "--raw", "--format=s16le", r, c, "--latency-msec=150",
                   (char *)NULL);
            _exit(3);
        }
        execlp("paplay", "paplay", path, (char *)NULL);
        _exit(4);
    }
    if (pid < 0) { snprintf(msg, n, "无法启动播放进程"); return 0; }

    /* 音频服务没起来时 pacat/paplay 会立刻退出，别谎称"正在播放" */
    usleep(180000);
    int st = 0;
    if (waitpid(pid, &st, WNOHANG) == pid) {
        char err[160] = "";
        rd_file("/run/properos-media.err", err, sizeof(err));
        char *nl = strchr(err, '\n');
        if (nl) *nl = '\0';
        snprintf(msg, n, "播放失败：%s", err[0] ? err : "音频服务不可用");
        return 0;
    }

    char b[32];
    snprintf(b, sizeof(b), "%d\n", (int)pid);
    wr_file(P_MEDIA_PID, b);
    wr_file(P_MEDIA_TRK, path);
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    if (pcm) snprintf(msg, n, "正在播放 %s（%d Hz · %d 声道 · %d 秒）", base, rate, ch, secs);
    else snprintf(msg, n, "正在播放 %s", base);
    return 1;
}

/* ================= 数据源 ================= */

typedef struct {
    nvi_t it;
    int isdir;
    long long size;
    time_t mt;
} ent_t;

static ent_t g_ent[160];
static int g_ent_n;

static int ent_cmp(const void *a, const void *b)
{
    const ent_t *x = a, *y = b;
    if (x->isdir != y->isdir) return y->isdir - x->isdir;
    return strcasecmp(x->it.title, y->it.title);
}

static int src_fs_dir(const char *arg, nvi_t *out, int max)
{
    const char *path = (arg && arg[0]) ? arg : "/data/home";
    DIR *d = opendir(path);
    if (!d) return -1;
    g_ent_n = 0;
    struct dirent *de;
    while ((de = readdir(d)) && g_ent_n < 160) {
        if (de->d_name[0] == '.') continue;
        char full[512];
        snprintf(full, sizeof(full), "%s/%s", path, de->d_name);
        struct stat st;
        if (lstat(full, &st) != 0) continue;
        ent_t *e = &g_ent[g_ent_n];
        memset(e, 0, sizeof(*e));
        e->isdir = S_ISDIR(st.st_mode);
        e->size = (long long)st.st_size;
        e->mt = st.st_mtime;
        const char *icon = "files", *desc = "文件";
        kind_of(de->d_name, e->isdir, &icon, &desc);
        snprintf(e->it.title, sizeof(e->it.title), "%s", de->d_name);
        char sz[32], wh[32];
        fmt_size(e->size, sz, sizeof(sz));
        fmt_when(e->mt, wh, sizeof(wh));
        if (e->isdir) snprintf(e->it.sub, sizeof(e->it.sub), "%s · %s", desc, wh);
        else snprintf(e->it.sub, sizeof(e->it.sub), "%s · %s · %s", desc, sz, wh);
        snprintf(e->it.icon, sizeof(e->it.icon), "%s", icon);
        snprintf(e->it.op, sizeof(e->it.op), "fs.open");
        snprintf(e->it.arg, sizeof(e->it.arg), "%s", full);
        g_ent_n++;
    }
    closedir(d);
    if (g_ent_n == 0) return 0;
    qsort(g_ent, (size_t)g_ent_n, sizeof(g_ent[0]), ent_cmp);
    int n = g_ent_n < max ? g_ent_n : max;
    for (int i = 0; i < n; i++) out[i] = g_ent[i].it;
    return n;
}

static int src_fs_places(const char *arg, nvi_t *out, int max)
{
    static const struct { const char *path; const char *name; const char *icon; } P[] = {
        { "/data/home", "主目录", "home" },
        { "/data/home/Documents", "文档", "notes" },
        { "/data/home/Downloads", "下载", "download" },
        { "/data/home/Pictures", "图片", "photos" },
        { "/data/home/Music", "音乐", "music" },
        { "/opt/properos/apps", "应用（ppkg）", "grid" },
        { "/usr/share/properos/apps", "旧版应用包", "store" },
        { "/data/apps", "已安装应用", "grid" },
    };
    int n = 0;
    for (size_t i = 0; i < sizeof(P) / sizeof(P[0]) && n < max; i++) {
        if (access(P[i].path, F_OK) != 0) continue;
        nvi_t *it = &out[n++];
        memset(it, 0, sizeof(*it));
        snprintf(it->title, sizeof(it->title), "%s", P[i].name);
        char ds[64];
        disk_status(P[i].path, ds, sizeof(ds));
        snprintf(it->sub, sizeof(it->sub), "%s · %s", P[i].path, ds);
        snprintf(it->icon, sizeof(it->icon), "%s", P[i].icon);
        snprintf(it->op, sizeof(it->op), "fs.open");
        snprintf(it->arg, sizeof(it->arg), "%s", P[i].path);
    }
    return n;
}

static int scan_media(const char *dir, nvi_t *out, int max, int *n, int depth)
{
    static const char *aud[] = { "wav", "mp3", "flac", "ogg", "opus", "m4a", "aac", NULL };
    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *de;
    while ((de = readdir(d)) && *n < max) {
        if (de->d_name[0] == '.') continue;
        char full[512];
        snprintf(full, sizeof(full), "%s/%s", dir, de->d_name);
        struct stat st;
        if (stat(full, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            if (depth > 0) scan_media(full, out, max, n, depth - 1);
            continue;
        }
        if (!ext_in(ext_of(de->d_name), aud)) continue;
        nvi_t *it = &out[(*n)++];
        memset(it, 0, sizeof(*it));
        char base[128];
        snprintf(base, sizeof(base), "%s", de->d_name);
        char *dot = strrchr(base, '.');
        if (dot) *dot = '\0';
        snprintf(it->title, sizeof(it->title), "%s", base);
        char sz[32], dur[32] = "";
        fmt_size((long long)st.st_size, sz, sizeof(sz));
        int rate, ch, bits, secs = 0;
        long off;
        long long len;
        if (wav_info(full, &rate, &ch, &bits, &off, &len, &secs) && secs > 0)
            snprintf(dur, sizeof(dur), "%d:%02d · ", secs / 60, secs % 60);
        snprintf(it->sub, sizeof(it->sub), "%s%s · %s", dur, sz, ext_of(de->d_name));
        snprintf(it->icon, sizeof(it->icon), "music");
        snprintf(it->op, sizeof(it->op), "media.play");
        snprintf(it->arg, sizeof(it->arg), "%s", full);
    }
    closedir(d);
    return 0;
}

static int src_media_tracks(const char *arg, nvi_t *out, int max)
{
    int n = 0;
    if (arg && arg[0]) {
        scan_media(arg, out, max, &n, 2);
        return n;
    }
    scan_media("/data/home/Music", out, max, &n, 3);
    if (n < max) scan_media("/data/home/Downloads", out, max, &n, 1);
    if (n < max) scan_media("/usr/share/sounds", out, max, &n, 3);
    return n;
}

/* 从 jspk 的 manifest.json 里取一个字段（只处理本文件的简单格式） */
static void manifest_field(const char *path, const char *key, char *out, size_t n)
{
    out[0] = '\0';
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return;
    char buf[8192];
    ssize_t g = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (g <= 0) return;
    buf[g] = '\0';
    char pat[32];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    char *p = strstr(buf, pat);
    if (!p) return;
    p = strchr(p + strlen(pat), ':');
    if (!p) return;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '"') {
        p++;
        char *e = strchr(p, '"');
        if (!e) return;
        size_t l = (size_t)(e - p);
        if (l >= n) l = n - 1;
        memcpy(out, p, l);
        out[l] = '\0';
    }
}

static int read_small(const char *path, char *buf, size_t n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t got = fread(buf, 1, n - 1, f);
    fclose(f);
    buf[got] = '\0';
    return 0;
}

/* 从 manifest.json 里取一个字符串字段（够用即可，不做完整解析） */
static void man_str(const char *json, const char *key, char *out, size_t n)
{
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    out[0] = '\0';
    if (!p) return;
    p = strchr(p + strlen(pat), ':');
    if (!p) return;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '"') return;
    p++;
    const char *e = strchr(p, '"');
    if (!e) return;
    size_t l = (size_t)(e - p);
    if (l >= n) l = n - 1;
    memcpy(out, p, l);
    out[l] = '\0';
}

static int src_pkg_apps(const char *arg, nvi_t *out, int max)
{
    int n = 0;

    /* 1) ppkg 应用：/opt/properos/apps/<名字>/manifest.json */
    DIR *d = opendir("/opt/properos/apps");
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) && n < max) {
            if (de->d_name[0] == '.') continue;
            char full[512], mf[560], js[2048];
            snprintf(full, sizeof(full), "/opt/properos/apps/%s", de->d_name);
            snprintf(mf, sizeof(mf), "%s/manifest.json", full);
            if (read_small(mf, js, sizeof(js)) != 0) continue;
            nvi_t *it = &out[n];
            memset(it, 0, sizeof(*it));
            man_str(js, "name", it->title, sizeof(it->title));
            man_str(js, "summary", it->sub, sizeof(it->sub));
            if (!it->title[0]) snprintf(it->title, sizeof(it->title), "%s", de->d_name);
            snprintf(it->icon, sizeof(it->icon), "grid");
            snprintf(it->op, sizeof(it->op), "pkg.launch");
            snprintf(it->arg, sizeof(it->arg), "%s", full);
            n++;
        }
        closedir(d);
    }

    /* 2) 旧 .jspk 包（兼容） */
    static const char *dirs[] = { "/usr/share/properos/apps", "/data/apps", NULL };
    for (int di = 0; dirs[di] && n < max; di++) {
        DIR *dp = opendir(dirs[di]);
        if (!dp) continue;
        struct dirent *de;
        while ((de = readdir(dp)) && n < max) {
            size_t l = strlen(de->d_name);
            if (de->d_name[0] == '.' || l < 6 || strcmp(de->d_name + l - 5, ".jspk")) continue;
            char full[512];
            snprintf(full, sizeof(full), "%s/%s", dirs[di], de->d_name);
            struct stat st;
            if (stat(full, &st) != 0) continue;
            nvi_t *it = &out[n];
            memset(it, 0, sizeof(*it));
            snprintf(it->title, sizeof(it->title), "%s", de->d_name);
            if (l >= 6) it->title[l - 5] = '\0';
            /* 包是 ZIP，manifest.json 不在偏移 0，用文件名做标题，副标题给真实大小与路径 */
            char sz[32];
            fmt_size((long long)st.st_size, sz, sizeof(sz));
            snprintf(it->sub, sizeof(it->sub), "%s · %s", dirs[di], sz);
            snprintf(it->icon, sizeof(it->icon), "grid");
            snprintf(it->op, sizeof(it->op), "pkg.launch");
            snprintf(it->arg, sizeof(it->arg), "%s", full);
            n++;
        }
        closedir(dp);
    }
    return n;
}

/* 已安装的目录形态应用（/data/apps/<id>/manifest.json）能取到中文名 */
static int src_pkg_dir_apps(const char *arg, nvi_t *out, int max)
{
    DIR *d = opendir("/data/apps");
    if (!d) return 0;
    int n = 0;
    struct dirent *de;
    while ((de = readdir(d)) && n < max) {
        if (de->d_name[0] == '.') continue;
        char man[512];
        snprintf(man, sizeof(man), "/data/apps/%s/manifest.json", de->d_name);
        if (access(man, F_OK) != 0) continue;
        nvi_t *it = &out[n++];
        memset(it, 0, sizeof(*it));
        manifest_field(man, "name", it->title, sizeof(it->title));
        if (!it->title[0]) snprintf(it->title, sizeof(it->title), "%s", de->d_name);
        char ver[32] = "0";
        manifest_field(man, "version", ver, sizeof(ver));
        snprintf(it->sub, sizeof(it->sub), "已安装 · 版本 %s", ver);
        snprintf(it->icon, sizeof(it->icon), "grid");
        snprintf(it->op, sizeof(it->op), "app.remove");
        snprintf(it->arg, sizeof(it->arg), "%s", de->d_name);
    }
    closedir(d);
    return n;
}

static int src_pkg_db(const char *arg, nvi_t *out, int max)
{
    DIR *d = opendir("/var/lib/ppkg/db");
    if (!d) return 0;
    int n = 0;
    struct dirent *de;
    while ((de = readdir(d)) && n < max) {
        if (de->d_name[0] == '.') continue;
        char pi[512];
        snprintf(pi, sizeof(pi), "/var/lib/ppkg/db/%s/PKGINFO", de->d_name);
        char buf[2048];
        if (rd_file(pi, buf, sizeof(buf)) < 0) continue;
        nvi_t *it = &out[n++];
        memset(it, 0, sizeof(*it));
        snprintf(it->title, sizeof(it->title), "%s", de->d_name);
        char ver[48] = "0", fmt[24] = "payload", desc[96] = "";
        char *save = NULL;
        for (char *l = strtok_r(buf, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
            char *eq = strchr(l, '=');
            if (!eq) continue;
            *eq = '\0';
            char *k = l, *v = eq + 1;
            while (*v == ' ') v++;
            if (!strcmp(k, "version")) snprintf(ver, sizeof(ver), "%s", v);
            else if (!strcmp(k, "format")) snprintf(fmt, sizeof(fmt), "%s", v);
            else if (!strcmp(k, "description")) snprintf(desc, sizeof(desc), "%s", v);
        }
        snprintf(it->sub, sizeof(it->sub), "%s · %s · %s", desc[0] ? desc : "系统包", ver,
                 strcmp(fmt, "deb") ? "ppkg" : "deb→ppkg");
        snprintf(it->icon, sizeof(it->icon), "chip");
        snprintf(it->value, sizeof(it->value), "%s", ver);
        /* 只读展示：卸载走命令行 ppkg remove，避免误触把系统包删掉 */
    }
    closedir(d);
    return n;
}

static int src_pkg_local(const char *arg, nvi_t *out, int max)
{
    static const char *dirs[] = { "/data/home/Downloads", "/data/Download", "/tmp", NULL };
    int n = 0;
    for (int di = 0; dirs[di] && n < max; di++) {
        DIR *d = opendir(dirs[di]);
        if (!d) continue;
        struct dirent *de;
        while ((de = readdir(d)) && n < max) {
            const char *e = ext_of(de->d_name);
            if (strcasecmp(e, "deb") && strcasecmp(e, "ppkg")) continue;
            char full[512];
            snprintf(full, sizeof(full), "%s/%s", dirs[di], de->d_name);
            struct stat st;
            if (stat(full, &st) != 0) continue;
            nvi_t *it = &out[n++];
            memset(it, 0, sizeof(*it));
            snprintf(it->title, sizeof(it->title), "%s", de->d_name);
            char sz[32];
            fmt_size((long long)st.st_size, sz, sizeof(sz));
            snprintf(it->sub, sizeof(it->sub), "%s · %s · 点击安装", sz,
                     strcasecmp(e, "deb") ? "ppkg" : "deb");
            snprintf(it->icon, sizeof(it->icon), "download");
            snprintf(it->op, sizeof(it->op), "pkg.install");
            snprintf(it->arg, sizeof(it->arg), "%s", full);
        }
        closedir(d);
    }
    return n;
}

static int src_net_ifaces(const char *arg, nvi_t *out, int max)
{
    struct ifaddrs *ifa = NULL;
    if (getifaddrs(&ifa) != 0) return -1;
    int n = 0;
    for (struct ifaddrs *p = ifa; p && n < max; p = p->ifa_next) {
        if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET) continue;
        if (!strcmp(p->ifa_name, "lo")) continue;
        nvi_t *it = &out[n++];
        memset(it, 0, sizeof(*it));
        char ip[64];
        struct sockaddr_in *sin = (struct sockaddr_in *)p->ifa_addr;
        inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip));
        char sp[512], op[32] = "";
        snprintf(sp, sizeof(sp), "/sys/class/net/%s/wireless", p->ifa_name);
        int wireless = access(sp, F_OK) == 0;
        snprintf(sp, sizeof(sp), "/sys/class/net/%s/operstate", p->ifa_name);
        rd_line(sp, op, sizeof(op));
        snprintf(it->title, sizeof(it->title), "%s", p->ifa_name);
        snprintf(it->sub, sizeof(it->sub), "%s · %s · %s", ip,
                 wireless ? "Wi-Fi" : "有线", op[0] ? op : "unknown");
        snprintf(it->icon, sizeof(it->icon), wireless ? "wifi" : "signal");
        snprintf(it->op, sizeof(it->op), "net.detail");
        snprintf(it->arg, sizeof(it->arg), "%s", p->ifa_name);
    }
    freeifaddrs(ifa);
    return n;
}

static int src_modems(const char *arg, nvi_t *out, int max)
{
    /* 真实检测：ModemManager 的 modem 列表优先，其次 wwan 串口节点 */
    int n = 0;
    if (has_cmd("mmcli")) {
        char buf[1024];
        if (run_capture((char *[]){ "mmcli", "-L", NULL }, buf, sizeof(buf), 2500) == 0 &&
            strstr(buf, "Modem/")) {
            char *save = NULL;
            for (char *l = strtok_r(buf, "\n", &save); l && n < max; l = strtok_r(NULL, "\n", &save)) {
                char *m = strstr(l, "/Modem/");
                if (!m) continue;
                nvi_t *it = &out[n++];
                memset(it, 0, sizeof(*it));
                snprintf(it->title, sizeof(it->title), "调制解调器 %s", m + 7);
                snprintf(it->sub, sizeof(it->sub), "ModemManager 已识别");
                snprintf(it->icon, sizeof(it->icon), "signal");
                snprintf(it->op, sizeof(it->op), "phone.modem");
                snprintf(it->arg, sizeof(it->arg), "%s", m + 7);
            }
        }
    }
    if (n == 0) {
        for (int i = 0; i < 8 && n < max; i++) {
            char p[64];
            snprintf(p, sizeof(p), "/dev/wwan0mbim%d", i);
            if (access(p, F_OK) != 0) {
                snprintf(p, sizeof(p), "/dev/wwan%d", i);
                if (access(p, F_OK) != 0) continue;
            }
            nvi_t *it = &out[n++];
            memset(it, 0, sizeof(*it));
            snprintf(it->title, sizeof(it->title), "WWAN 设备");
            snprintf(it->sub, sizeof(it->sub), "%s（未被 ModemManager 接管）", p);
            snprintf(it->icon, sizeof(it->icon), "signal");
        }
    }
    return n;
}

static void sms_path(const char *number, char *out, size_t n)
{
    char safe[64];
    size_t k = 0;
    for (const char *p = number; *p && k < sizeof(safe) - 1; p++) {
        if (*p == '/' || (*p == '.' && k == 0)) continue;
        safe[k++] = *p;
    }
    safe[k] = '\0';
    snprintf(out, n, P_SMS_DIR "/%s.log", safe[0] ? safe : "unknown");
}

static int src_sms_threads(const char *arg, nvi_t *out, int max)
{
    DIR *d = opendir(P_SMS_DIR);
    if (!d) return 0;
    int n = 0;
    struct dirent *de;
    while ((de = readdir(d)) && n < max) {
        size_t l = strlen(de->d_name);
        if (de->d_name[0] == '.' || l < 5) continue;
        char full[512];
        snprintf(full, sizeof(full), P_SMS_DIR "/%s", de->d_name);
        char buf[4096];
        if (rd_file(full, buf, sizeof(buf)) < 0) continue;
        char *last = strrchr(buf, '\n');
        if (!last) last = buf - 1;
        last++;
        long long ts = 0;
        char dir[8] = "", text[128] = "";
        if (sscanf(last, "%lld|%7[^|]|%127[^\n]", &ts, dir, text) < 3) continue;
        nvi_t *it = &out[n++];
        memset(it, 0, sizeof(*it));
        snprintf(it->title, sizeof(it->title), "%s", de->d_name);
        it->title[l - 4] = '\0';
        snprintf(it->sub, sizeof(it->sub), "%s%s", strcmp(dir, "out") ? "" : "我: ", text);
        char wh[32];
        fmt_when((time_t)ts, wh, sizeof(wh));
        snprintf(it->value, sizeof(it->value), "%s", wh);
        snprintf(it->icon, sizeof(it->icon), "message");
        snprintf(it->op, sizeof(it->op), "sms.thread");
        snprintf(it->arg, sizeof(it->arg), "%s", it->title);
    }
    closedir(d);
    return n;
}

static int src_sms_messages(const char *arg, nvi_t *out, int max)
{
    char p[512];
    sms_path(arg ? arg : "", p, sizeof(p));
    char buf[8192];
    if (rd_file(p, buf, sizeof(buf)) < 0) return 0;
    int n = 0;
    char *save = NULL;
    for (char *l = strtok_r(buf, "\n", &save); l && n < max; l = strtok_r(NULL, "\n", &save)) {
        long long ts = 0;
        char dir[8] = "", text[256] = "";
        if (sscanf(l, "%lld|%7[^|]|%255[^\n]", &ts, dir, text) < 3) continue;
        nvi_t *it = &out[n++];
        memset(it, 0, sizeof(*it));
        snprintf(it->title, sizeof(it->title), "%s", text);
        char wh[32];
        fmt_when((time_t)ts, wh, sizeof(wh));
        snprintf(it->sub, sizeof(it->sub), "%s · %s", strcmp(dir, "out") ? "收到" : "发出", wh);
        snprintf(it->icon, sizeof(it->icon), "message");
    }
    return n;
}

static int src_call_log(const char *arg, nvi_t *out, int max)
{
    char buf[8192];
    if (rd_file(P_CALL_LOG, buf, sizeof(buf)) < 0) return 0;
    int n = 0;
    char *save = NULL;
    for (char *l = strtok_r(buf, "\n", &save); l && n < max; l = strtok_r(NULL, "\n", &save)) {
        long long ts = 0;
        char num[64] = "", dir[8] = "", res[48] = "";
        if (sscanf(l, "%lld|%63[^|]|%7[^|]|%47[^\n]", &ts, num, dir, res) < 4) continue;
        nvi_t *it = &out[n++];
        memset(it, 0, sizeof(*it));
        snprintf(it->title, sizeof(it->title), "%s", num);
        char wh[32];
        fmt_when((time_t)ts, wh, sizeof(wh));
        snprintf(it->sub, sizeof(it->sub), "%s · %s · %s", strcmp(dir, "out") ? "呼入" : "呼出", res, wh);
        snprintf(it->icon, sizeof(it->icon), "phone");
    }
    /* 最近的在最前 */
    for (int i = 0; i < n / 2; i++) {
        nvi_t t = out[i];
        out[i] = out[n - 1 - i];
        out[n - 1 - i] = t;
    }
    return n;
}

static int src_sys_info(const char *arg, nvi_t *out, int max)
{
    struct { const char *k; const char *v; const char *icon; } rows[10];
    char up[64], mem[64], dk[64], rd[64], tm[32];
    uptime_status(up, sizeof(up));
    mem_status(mem, sizeof(mem));
    disk_status("/data", dk, sizeof(dk));
    disk_status("/", rd, sizeof(rd));
    temp_status(tm, sizeof(tm));
    int r = 0;
    rows[r].k = "设备型号"; rows[r].v = nv_status("model"); rows[r++].icon = "chip";
    rows[r].k = "内核版本"; rows[r].v = nv_status("kernel"); rows[r++].icon = "chip";
    rows[r].k = "处理器";   rows[r].v = nv_status("cpu"); rows[r++].icon = "chip";
    rows[r].k = "内存";     rows[r].v = mem; rows[r++].icon = "grid";
    rows[r].k = "系统分区"; rows[r].v = rd; rows[r++].icon = "files";
    rows[r].k = "数据分区"; rows[r].v = dk; rows[r++].icon = "files";
    rows[r].k = "运行时间"; rows[r].v = up; rows[r++].icon = "clock";
    rows[r].k = "温度";     rows[r].v = tm; rows[r++].icon = "chip";
    rows[r].k = "电池";     rows[r].v = nv_status("battery"); rows[r++].icon = "battery";
    rows[r].k = "网络";     rows[r].v = nv_status("wifi"); rows[r++].icon = "wifi";
    int n = 0;
    for (int i = 0; i < r && n < max; i++) {
        nvi_t *it = &out[n++];
        memset(it, 0, sizeof(*it));
        snprintf(it->title, sizeof(it->title), "%s", rows[i].k);
        snprintf(it->value, sizeof(it->value), "%s", rows[i].v);
        snprintf(it->icon, sizeof(it->icon), "%s", rows[i].icon);
    }
    return n;
}

static int src_world_time(const char *arg, nvi_t *out, int max)
{
    static const char *zones[][2] = {
        { "北京", "Asia/Shanghai" },
        { "东京", "Asia/Tokyo" },
        { "伦敦", "Europe/London" },
        { "纽约", "America/New_York" },
    };
    int n = 0;
    for (int i = 0; i < 4 && n < max; i++) {
        char save[128] = "";
        const char *tz = getenv("TZ");
        if (tz) snprintf(save, sizeof(save), "%s", tz);
        setenv("TZ", zones[i][1], 1);
        tzset();
        time_t t = time(NULL);
        struct tm tm;
        localtime_r(&t, &tm);
        char clock[16];
        snprintf(clock, sizeof(clock), "%02d:%02d", tm.tm_hour, tm.tm_min);
        if (save[0]) setenv("TZ", save, 1);
        else unsetenv("TZ");
        tzset();
        nvi_t *it = &out[n++];
        memset(it, 0, sizeof(*it));
        snprintf(it->title, sizeof(it->title), "%s", zones[i][0]);
        snprintf(it->sub, sizeof(it->sub), "%s", zones[i][1]);
        snprintf(it->value, sizeof(it->value), "%s", clock);
        snprintf(it->icon, sizeof(it->icon), "clock");
    }
    return n;
}

static int src_alarms(const char *arg, nvi_t *out, int max)
{
    /* 真实闹钟表：/data/home/.config/properos/alarms.conf，每行 HH:MM|on|标签 */
    char buf[1024];
    if (rd_file("/data/home/.config/properos/alarms.conf", buf, sizeof(buf)) < 0) return 0;
    int n = 0;
    char *save = NULL;
    for (char *l = strtok_r(buf, "\n", &save); l && n < max; l = strtok_r(NULL, "\n", &save)) {
        char hm[8] = "", on[4] = "", label[32] = "";
        if (sscanf(l, "%7[^|]|%3[^|]|%31[^\n]", hm, on, label) < 2) continue;
        nvi_t *it = &out[n++];
        memset(it, 0, sizeof(*it));
        snprintf(it->title, sizeof(it->title), "%s", hm);
        snprintf(it->sub, sizeof(it->sub), "%s", label[0] ? label : "闹钟");
        snprintf(it->value, sizeof(it->value), "%s", strcmp(on, "1") ? "关" : "开");
        snprintf(it->icon, sizeof(it->icon), "clock");
        snprintf(it->op, sizeof(it->op), "clock.toggle");
        snprintf(it->arg, sizeof(it->arg), "%s", hm);
    }
    return n;
}

/* ---------------- 闹钟（真实文件读写） ---------------- */

#define P_ALARMS "/data/home/.config/properos/alarms.conf"

static int act_clock_toggle(const char *hm, char *msg, size_t n)
{
    if (!hm || !hm[0]) { snprintf(msg, n, "闹钟时间为空"); return 0; }
    char buf[2048];
    if (rd_file(P_ALARMS, buf, sizeof(buf)) < 0) { snprintf(msg, n, "没有闹钟"); return 0; }
    char out[2048];
    size_t used = 0;
    int found = 0, now_on = 0;
    char *save = NULL;
    for (char *l = strtok_r(buf, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
        char h[8] = "", on[4] = "", lb[32] = "";
        if (sscanf(l, "%7[^|]|%3[^|]|%31[^\n]", h, on, lb) >= 2 && !strcmp(h, hm)) {
            int nv = strcmp(on, "1") ? 1 : 0;
            now_on = nv;
            used += (size_t)snprintf(out + used, sizeof(out) - used, "%s|%d|%s\n", h, nv, lb);
            found = 1;
            continue;
        }
        used += (size_t)snprintf(out + used, sizeof(out) - used, "%s\n", l);
    }
    if (!found) { snprintf(msg, n, "没有这个闹钟"); return 0; }
    wr_file(P_ALARMS, out);
    snprintf(msg, n, "%s %s", hm, now_on ? "已开启" : "已关闭");
    return 1;
}

static int act_clock_add(const char *spec, char *msg, size_t n)
{
    char hm[8] = "", label[48] = "闹钟";
    if (!spec || !spec[0]) { snprintf(msg, n, "请先填写时间"); return 0; }
    if (sscanf(spec, "%7[^|]|%47[^\n]", hm, label) < 1) { snprintf(msg, n, "格式应为 07:30"); return 0; }
    int hh = 0, mm = 0;
    if (sscanf(hm, "%d:%d", &hh, &mm) != 2 || hh < 0 || hh > 23 || mm < 0 || mm > 59) {
        snprintf(msg, n, "时间格式应为 HH:MM");
        return 0;
    }
    mkdir_p("/data/home/.config/properos");
    char line[96];
    int l = snprintf(line, sizeof(line), "%02d:%02d|1|%s\n", hh, mm, label[0] ? label : "闹钟");
    int fd = open(P_ALARMS, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (fd < 0) { snprintf(msg, n, "无法写入闹钟表"); return 0; }
    ssize_t ignored = write(fd, line, (size_t)l);
    (void)ignored;
    close(fd);
    snprintf(msg, n, "已添加 %02d:%02d", hh, mm);
    return 1;
}

/* ---------------- 摄像头（真实 V4L2） ---------------- */

static int src_camera_devices(const char *arg, nvi_t *out, int max)
{
    int n = 0;
    for (int i = 0; i < 8 && n < max; i++) {
        char dev[32];
        snprintf(dev, sizeof(dev), "/dev/video%d", i);
        int fd = open(dev, O_RDWR | O_NONBLOCK);
        if (fd < 0) continue;
        struct v4l2_capability cap;
        memset(&cap, 0, sizeof(cap));
        int ok = ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0;
        close(fd);
        if (!ok) continue;
        int capture = (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) ||
                      (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE);
        if (!capture) continue;
        nvi_t *it = &out[n++];
        memset(it, 0, sizeof(*it));
        snprintf(it->title, sizeof(it->title), "%s", (const char *)cap.card);
        snprintf(it->sub, sizeof(it->sub), "%s · %s · 可采集", dev, (const char *)cap.driver);
        snprintf(it->icon, sizeof(it->icon), "camera");
        snprintf(it->op, sizeof(it->op), "camera.snap");
        snprintf(it->arg, sizeof(it->arg), "%s", dev);
    }
    return n;
}

/* 抓一帧 YUYV，转 RGB 存 PPM（无编码器依赖，可被任何查看器打开） */
static int act_camera_snap(const char *dev, char *msg, size_t n)
{
    if (!dev || !dev[0]) dev = "/dev/video0";
    int fd = open(dev, O_RDWR | O_NONBLOCK);
    if (fd < 0) { snprintf(msg, n, "打不开 %s：%s", dev, strerror(errno)); return 0; }

    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = 640;
    fmt.fmt.pix.height = 480;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    if (ioctl(fd, VIDIOC_S_FMT, &fmt) != 0) {
        snprintf(msg, n, "设备不支持 YUYV 640x480");
        close(fd);
        return 0;
    }
    int w = (int)fmt.fmt.pix.width, h = (int)fmt.fmt.pix.height;

    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = 2;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(fd, VIDIOC_REQBUFS, &req) != 0 || req.count < 1) {
        snprintf(msg, n, "无法申请采集缓冲");
        close(fd);
        return 0;
    }
    void *buf[4] = { 0 };
    size_t blen[4] = { 0 };
    for (unsigned i = 0; i < req.count && i < 4; i++) {
        struct v4l2_buffer b;
        memset(&b, 0, sizeof(b));
        b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        b.memory = V4L2_MEMORY_MMAP;
        b.index = i;
        if (ioctl(fd, VIDIOC_QUERYBUF, &b) != 0) continue;
        buf[i] = mmap(NULL, b.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, b.m.offset);
        if (buf[i] == MAP_FAILED) { buf[i] = NULL; continue; }
        blen[i] = b.length;
        ioctl(fd, VIDIOC_QBUF, &b);
    }
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) != 0) {
        snprintf(msg, n, "无法开始采集");
        for (unsigned i = 0; i < req.count && i < 4; i++)
            if (buf[i]) munmap(buf[i], blen[i]);
        close(fd);
        return 0;
    }
    struct v4l2_buffer b;
    memset(&b, 0, sizeof(b));
    b.type = type;
    b.memory = V4L2_MEMORY_MMAP;
    int got = -1;
    for (int tries = 0; tries < 40; tries++) {
        if (ioctl(fd, VIDIOC_DQBUF, &b) == 0) { got = (int)b.index; break; }
        usleep(50000);
    }
    ioctl(fd, VIDIOC_STREAMOFF, &type);
    if (got < 0 || !buf[got]) {
        snprintf(msg, n, "没有取到画面");
        for (unsigned i = 0; i < req.count && i < 4; i++)
            if (buf[i]) munmap(buf[i], blen[i]);
        close(fd);
        return 0;
    }

    mkdir_p("/data/home/Pictures");
    char path[128];
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    snprintf(path, sizeof(path), "/data/home/Pictures/IMG_%04d%02d%02d_%02d%02d%02d.ppm",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    FILE *f = fopen(path, "wb");
    if (!f) {
        snprintf(msg, n, "无法写入照片");
        for (unsigned i = 0; i < req.count && i < 4; i++)
            if (buf[i]) munmap(buf[i], blen[i]);
        close(fd);
        return 0;
    }
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    const unsigned char *p = buf[got];
    for (int i = 0; i < w * h; i++) {
        int y0 = p[i * 2], u = p[i * 2 + 1], y1 = p[i * 2 + 2], v = p[i * 2 + 3];
        (void)y1;
        int c = y0 - 16, d = u - 128, e = v - 128;
        int r = (298 * c + 409 * e + 128) >> 8;
        int g = (298 * c - 100 * d - 208 * e + 128) >> 8;
        int bl = (298 * c + 516 * d + 128) >> 8;
        unsigned char rgb[3] = { (unsigned char)(r < 0 ? 0 : r > 255 ? 255 : r),
                                 (unsigned char)(g < 0 ? 0 : g > 255 ? 255 : g),
                                 (unsigned char)(bl < 0 ? 0 : bl > 255 ? 255 : bl) };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    for (unsigned i = 0; i < req.count && i < 4; i++)
        if (buf[i]) munmap(buf[i], blen[i]);
    close(fd);
    snprintf(msg, n, "已保存 %s（%dx%d）", strrchr(path, '/') + 1, w, h);
    return 1;
}

int nv_source(const char *src, const char *arg, nvi_t *out, int max)
{
    if (!src || !src[0]) return -1;
    if (max <= 0) return 0;
    for (int i = 0; i < max; i++) memset(&out[i], 0, sizeof(out[i]));

    if (!strcmp(src, "fs.dir")) return src_fs_dir(arg, out, max);
    if (!strcmp(src, "fs.places")) return src_fs_places(arg, out, max);
    if (!strcmp(src, "media.tracks")) return src_media_tracks(arg, out, max);
    if (!strcmp(src, "pkg.apps")) return src_pkg_apps(arg, out, max);
    if (!strcmp(src, "pkg.installed")) return src_pkg_dir_apps(arg, out, max);
    if (!strcmp(src, "pkg.db")) return src_pkg_db(arg, out, max);
    if (!strcmp(src, "pkg.local")) return src_pkg_local(arg, out, max);
    if (!strcmp(src, "net.ifaces")) return src_net_ifaces(arg, out, max);
    if (!strcmp(src, "modems")) return src_modems(arg, out, max);
    if (!strcmp(src, "sms.threads")) return src_sms_threads(arg, out, max);
    if (!strcmp(src, "sms.messages")) return src_sms_messages(arg, out, max);
    if (!strcmp(src, "calls")) return src_call_log(arg, out, max);
    if (!strcmp(src, "sys.info")) return src_sys_info(arg, out, max);
    if (!strcmp(src, "clock.world")) return src_world_time(arg, out, max);
    if (!strcmp(src, "clock.alarms")) return src_alarms(arg, out, max);
    if (!strcmp(src, "camera.devices")) return src_camera_devices(arg, out, max);
    return -1;
}

/* ================= 动作 ================= */

static void call_log_append(const char *number, const char *dir, const char *result)
{
    mkdir_p("/data/phone");
    int fd = open(P_CALL_LOG, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (fd < 0) return;
    char line[256];
    int l = snprintf(line, sizeof(line), "%lld|%s|%s|%s\n", (long long)time(NULL), number, dir, result);
    ssize_t ignored = write(fd, line, (size_t)l);
    (void)ignored;
    close(fd);
}

static int act_phone_dial(const char *arg, char *msg, size_t n)
{
    const char *num = (arg && arg[0]) ? arg : "";
    if (!num[0]) { snprintf(msg, n, "号码为空"); return 0; }

    if (has_cmd("mmcli")) {
        char out[2048];
        if (run_capture((char *[]){ "mmcli", "-L", NULL }, out, sizeof(out), 2500) == 0 &&
            strstr(out, "Modem/")) {
            char m[16];
            char *p = strstr(out, "/Modem/");
            snprintf(m, sizeof(m), "%s", p + 7);
            char *sp = strchr(m, '\n');
            if (sp) *sp = '\0';
            char callopt[128];
            snprintf(callopt, sizeof(callopt), "--voice-create-call=%s", num);
            char arg1[32];
            snprintf(arg1, sizeof(arg1), "-m%s", m);
            int rc = run_capture((char *[]){ "mmcli", arg1, callopt, NULL }, out, sizeof(out), 15000);
            call_log_append(num, "out", rc == 0 ? "已拨出" : "拨号失败");
            if (rc == 0) { snprintf(msg, n, "正在呼叫 %s", num); return 1; }
            snprintf(msg, n, "调制解调器拒绝拨号：%s", out[0] ? out : "未知错误");
            return 0;
        }
    }
    call_log_append(num, "out", "未拨出：无调制解调器");
    snprintf(msg, n, "本机没有可用调制解调器，已记录为未接通");
    return 0;
}

static int act_sms_send(const char *arg, char *msg, size_t n)
{
    /* arg 形如 "number\n文本" */
    if (!arg || !arg[0]) { snprintf(msg, n, "收件人或内容为空"); return 0; }
    char num[64] = "";
    const char *nl = strchr(arg, '\n');
    size_t nl_len = nl ? (size_t)(nl - arg) : strlen(arg);
    if (nl_len >= sizeof(num)) nl_len = sizeof(num) - 1;
    memcpy(num, arg, nl_len);
    num[nl_len] = '\0';
    const char *text = nl ? nl + 1 : "";
    if (!num[0] || !text[0]) { snprintf(msg, n, "收件人或内容为空"); return 0; }

    mkdir_p(P_SMS_DIR);
    char p[512];
    sms_path(num, p, sizeof(p));
    int fd = open(p, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (fd < 0) { snprintf(msg, n, "无法写入短信存储"); return 0; }
    char line[512];
    int l = snprintf(line, sizeof(line), "%lld|out|%s\n", (long long)time(NULL), text);
    ssize_t ignored = write(fd, line, (size_t)l);
    (void)ignored;
    close(fd);

    int sent = 0;
    if (has_cmd("mmcli")) {
        char out[1024];
        if (run_capture((char *[]){ "mmcli", "-L", NULL }, out, sizeof(out), 2000) == 0 &&
            strstr(out, "Modem/")) {
            char m[16];
            char *q = strstr(out, "/Modem/");
            snprintf(m, sizeof(m), "%s", q + 7);
            char *sp = strchr(m, '\n');
            if (sp) *sp = '\0';
            char a1[32], a2[512];
            snprintf(a1, sizeof(a1), "-m%s", m);
            snprintf(a2, sizeof(a2), "--messaging-create-sms=number=%s,text=%s", num, text);
            sent = run_capture((char *[]){ "mmcli", a1, a2, NULL }, out, sizeof(out), 12000) == 0;
            if (sent) run_capture((char *[]){ "mmcli", a1, "--messaging-list-sms", NULL }, out,
                                  sizeof(out), 5000);
        }
    }
    snprintf(msg, n, sent ? "已交给调制解调器发送" : "已存入本机会话（无调制解调器，未发出）");
    return sent;
}

static int act_pkg_install(const char *arg, char *msg, size_t n)
{
    if (!arg || !arg[0]) { snprintf(msg, n, "没有选择安装包"); return 0; }
    if (geteuid() != 0) { snprintf(msg, n, "安装系统包需要 root 权限"); return 0; }
    char out[2048];
    const char *e = ext_of(arg);
    char ppkg[512];
    if (!strcasecmp(e, "deb")) {
        char base[256];
        const char *bn = strrchr(arg, '/');
        snprintf(base, sizeof(base), "%s", bn ? bn + 1 : arg);
        char *dot = strrchr(base, '.');
        if (dot) *dot = '\0';
        mkdir_p(P_PKG_CACHE);
        snprintf(ppkg, sizeof(ppkg), P_PKG_CACHE "/%s.ppkg", base);
        int rc = run_capture((char *[]){ "/usr/lib/properos/deb2ppkg", (char *)arg, ppkg, NULL },
                             out, sizeof(out), 120000);
        if (rc != 0) {
            snprintf(msg, n, "deb 转换失败：%s", out[0] ? out : "未知错误");
            return 0;
        }
    } else {
        snprintf(ppkg, sizeof(ppkg), "%s", arg);
    }
    int rc = run_capture((char *[]){ "/usr/bin/ppkg", "install", ppkg, NULL }, out, sizeof(out), 180000);
    if (rc == 0) {
        snprintf(msg, n, "%s 安装完成", strrchr(ppkg, '/') ? strrchr(ppkg, '/') + 1 : ppkg);
        return 1;
    }
    snprintf(msg, n, "安装失败：%s", out[0] ? out : "ppkg 返回错误");
    return 0;
}

static int act_pkg_remove(const char *arg, char *msg, size_t n)
{
    if (!arg || !arg[0]) { snprintf(msg, n, "包名为空"); return 0; }
    if (geteuid() != 0) { snprintf(msg, n, "卸载系统包需要 root 权限"); return 0; }
    char out[1024];
    int rc = run_capture((char *[]){ "/usr/bin/ppkg", "remove", (char *)arg, NULL }, out, sizeof(out), 60000);
    if (rc == 0) { snprintf(msg, n, "%s", "已卸载"); return 1; }
    snprintf(msg, n, "卸载失败：%s", out[0] ? out : "未知错误");
    return 0;
}

static int act_volume(const char *arg, char *msg, size_t n)
{
    int pct = atoi(arg ? arg : "0");
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    char v[16];
    snprintf(v, sizeof(v), "%d%%", pct);
    char out[512];
    if (has_cmd("wpctl")) {
        if (run_capture((char *[]){ "wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", v, NULL },
                        out, sizeof(out), 3000) == 0) {
            snprintf(msg, n, "音量 %d%%", pct);
            return 1;
        }
    }
    if (has_cmd("pactl")) {
        if (run_capture((char *[]){ "pactl", "set-sink-volume", "@DEFAULT_SINK@", v, NULL },
                        out, sizeof(out), 3000) == 0) {
            snprintf(msg, n, "音量 %d%%", pct);
            return 1;
        }
    }
    snprintf(msg, n, "没有可用的音频服务（wpctl/pactl）");
    return 0;
}

static int act_brightness(const char *arg, char *msg, size_t n)
{
    int pct = atoi(arg ? arg : "50");
    if (pct < 1) pct = 1;
    if (pct > 100) pct = 100;
    DIR *d = opendir("/sys/class/backlight");
    if (!d) { snprintf(msg, n, "本机没有背光设备"); return 0; }
    struct dirent *de;
    int ok = 0;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.') continue;
        char p[512], v[32];
        snprintf(p, sizeof(p), "/sys/class/backlight/%s/max_brightness", de->d_name);
        if (rd_line(p, v, sizeof(v)) < 0) continue;
        long mx = atol(v);
        if (mx <= 0) continue;
        long val = mx * pct / 100;
        if (val < 1) val = 1;
        snprintf(p, sizeof(p), "/sys/class/backlight/%s/brightness", de->d_name);
        char b[32];
        snprintf(b, sizeof(b), "%ld\n", val);
        wr_file(p, b);
        ok = 1;
    }
    closedir(d);
    if (!ok) { snprintf(msg, n, "背光节点不可写"); return 0; }
    snprintf(msg, n, "亮度 %d%%", pct);
    return 1;
}

static int act_wifi(const char *arg, char *msg, size_t n)
{
    const char *on = arg && !strcmp(arg, "off") ? "off" : "on";
    if (!has_cmd("nmcli")) { snprintf(msg, n, "未安装 NetworkManager（nmcli）"); return 0; }
    char out[512];
    int rc = run_capture((char *[]){ "nmcli", "radio", "wifi", (char *)on, NULL }, out, sizeof(out), 8000);
    if (rc == 0) {
        snprintf(msg, n, "Wi-Fi 已%s", on[1] ? "关闭" : "开启");
        return 1;
    }
    snprintf(msg, n, "操作失败：%s", out[0] ? out : "nmcli 返回错误");
    return 0;
}

static int act_fs_mkdir(const char *arg, char *msg, size_t n)
{
    if (!arg || !arg[0]) { snprintf(msg, n, "名称为空"); return 0; }
    if (mkdir(arg, 0755) != 0) {
        snprintf(msg, n, "创建失败：%s", strerror(errno));
        return 0;
    }
    snprintf(msg, n, "已创建 %s", strrchr(arg, '/') ? strrchr(arg, '/') + 1 : arg);
    return 1;
}

static int act_fs_rm(const char *arg, char *msg, size_t n)
{
    if (!arg || !arg[0]) { snprintf(msg, n, "路径为空"); return 0; }
    struct stat st;
    if (lstat(arg, &st) != 0) { snprintf(msg, n, "不存在"); return 0; }
    if (S_ISDIR(st.st_mode)) {
        if (rmdir(arg) != 0) { snprintf(msg, n, "目录非空或不可删：%s", strerror(errno)); return 0; }
    } else if (unlink(arg) != 0) {
        snprintf(msg, n, "删除失败：%s", strerror(errno));
        return 0;
    }
    snprintf(msg, n, "已删除");
    return 1;
}

int nv_action(const char *op, const char *arg, char *msg, size_t n)
{
    if (msg && n) msg[0] = '\0';
    if (!op || !op[0]) return 0;

    if (!strcmp(op, "media.play")) {
        const char *p = arg;
        if (!p || !p[0]) { snprintf(msg, n, "没有可播放的文件"); return 0; }
        return media_play(p, msg, n);
    }
    if (!strcmp(op, "media.stop")) {
        media_stop();
        snprintf(msg, n, "已停止播放");
        return 1;
    }
    if (!strcmp(op, "pkg.launch")) {
        if (!arg || access(arg, R_OK) != 0) { snprintf(msg, n, "应用包不存在"); return 0; }
        char *argv[] = { "/usr/bin/properos-app", (char *)arg, NULL };
        pid_t pid = spawn_bg(argv);
        if (pid < 0) { snprintf(msg, n, "启动失败"); return 0; }
        snprintf(msg, n, "已启动 %s", strrchr(arg, '/') ? strrchr(arg, '/') + 1 : arg);
        return 1;
    }
    if (!strcmp(op, "pkg.install")) return act_pkg_install(arg, msg, n);
    if (!strcmp(op, "pkg.remove")) return act_pkg_remove(arg, msg, n);
    if (!strcmp(op, "app.remove")) {
        if (geteuid() != 0) { snprintf(msg, n, "卸载应用需要 root"); return 0; }
        char out[512];
        int rc = run_capture((char *[]){ "/usr/bin/papp", "remove", (char *)arg, NULL }, out, sizeof(out), 30000);
        snprintf(msg, n, rc == 0 ? "已卸载" : "卸载失败");
        return rc == 0;
    }
    if (!strcmp(op, "phone.dial")) return act_phone_dial(arg, msg, n);
    if (!strcmp(op, "sms.send")) return act_sms_send(arg, msg, n);
    if (!strcmp(op, "sms.thread")) { snprintf(msg, n, "%s", arg); return 1; }
    if (!strcmp(op, "sys.volume")) return act_volume(arg, msg, n);
    if (!strcmp(op, "sys.brightness")) return act_brightness(arg, msg, n);
    if (!strcmp(op, "wifi")) return act_wifi(arg, msg, n);
    if (!strcmp(op, "fs.mkdir")) return act_fs_mkdir(arg, msg, n);
    if (!strcmp(op, "fs.rm")) return act_fs_rm(arg, msg, n);
    if (!strcmp(op, "power.reboot") || !strcmp(op, "power.off")) {
        if (geteuid() != 0) { snprintf(msg, n, "需要 root 权限"); return 0; }
        char *argv[] = { "/bin/systemctl", (char *)(strcmp(op, "power.off") ? "reboot" : "poweroff"), NULL };
        spawn_bg(argv);
        snprintf(msg, n, "正在%s", strcmp(op, "power.off") ? "重启" : "关机");
        return 1;
    }
    if (!strcmp(op, "net.detail")) {
        char p[256], v[512];
        snprintf(p, sizeof(p), "/sys/class/net/%s/address", arg ? arg : "eth0");
        if (rd_line(p, v, sizeof(v)) < 0) snprintf(v, sizeof(v), "未知");
        snprintf(msg, n, "%s 地址 %s", arg ? arg : "接口", v);
        return 1;
    }
    if (!strcmp(op, "clock.toggle")) return act_clock_toggle(arg, msg, n);
    if (!strcmp(op, "clock.add")) return act_clock_add(arg, msg, n);
    if (!strcmp(op, "camera.snap")) return act_camera_snap(arg, msg, n);
    snprintf(msg, n, "未知动作：%s", op);
    return 0;
}

int nv_has_camera(void)
{
    for (int i = 0; i < 4; i++) {
        char p[64];
        snprintf(p, sizeof(p), "/dev/video%d", i);
        if (access(p, R_OK | W_OK) == 0) return 1;
    }
    return 0;
}

void nv_init(void)
{
    static int done;
    if (done) return;
    done = 1;

    mkdir_p("/data/home");
    mkdir_p("/data/home/Documents");
    mkdir_p("/data/home/Downloads");
    mkdir_p("/data/home/Pictures");
    mkdir_p("/data/home/Music");
    mkdir_p("/data/home/.config/properos");
    mkdir_p("/data/phone/sms");
    mkdir_p(P_PKG_CACHE);

    /* 上次退出遗留的播放进程：pid 文件里的进程已经不属于我们就不动它 */
    int pid = media_pid();
    if (pid > 0) {
        char c[64];
        snprintf(c, sizeof(c), "/proc/%d/cmdline", pid);
        char cmd[256] = "";
        rd_file(c, cmd, sizeof(cmd));
        if (!strstr(cmd, "pacat") && !strstr(cmd, "paplay")) unlink(P_MEDIA_PID);
    } else {
        unlink(P_MEDIA_PID);
    }
}
