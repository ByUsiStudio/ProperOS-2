#define _GNU_SOURCE
#include "properos.h"

int plog_level = LOG_INFO;

static FILE *g_logfp;
static int   g_syslog;

void plog_open(const char *ident)
{
    (void)ident;
    const char *env = getenv("PROPEROS_LOG_LEVEL");
    if (env) plog_level = atoi(env);
    const char *path = getenv("PROPEROS_LOG");
    if (path && *path) {
        g_logfp = fopen(path, "a");
        if (!g_logfp) g_logfp = stderr;
    } else {
        g_logfp = stderr;
    }
    g_syslog = 1;
}

static const char *level_name(int l)
{
    switch (l) {
    case LOG_ERR:  return "error";
    case LOG_WARN: return "warn";
    case LOG_DBG:  return "debug";
    default:       return "info";
    }
}

void plog(int level, const char *fmt, ...)
{
    if (level > plog_level) return;
    char msg[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    if (!g_logfp) g_logfp = stderr;

    /* 手机端：引导阶段输出到串口/控制台，运行期同时写 journal */
    if (g_syslog && access("/dev/kmsg", W_OK) == 0 && level <= LOG_WARN) {
        FILE *k = fopen("/dev/kmsg", "w");
        if (k) {
            fprintf(k, "properos[%s]: %s\n", level_name(level), msg);
            fclose(k);
        }
    }
    fprintf(g_logfp, "properos[%s]: %s\n", level_name(level), msg);
    fflush(g_logfp);
}

void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) { fprintf(stderr, "properos: 内存不足\n"); _exit(127); }
    return p;
}

void *xcalloc(size_t n, size_t sz)
{
    void *p = calloc(n ? n : 1, sz ? sz : 1);
    if (!p) { fprintf(stderr, "properos: 内存不足\n"); _exit(127); }
    return p;
}

void *xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);
    if (!q) { fprintf(stderr, "properos: 内存不足\n"); _exit(127); }
    return q;
}

char *xstrdup(const char *s)
{
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = xmalloc(n);
    memcpy(p, s, n);
    return p;
}

char *xasprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char *out = NULL;
    if (vasprintf(&out, fmt, ap) < 0) out = NULL;
    va_end(ap);
    return out;
}

char *read_file(const char *path, size_t *len_out)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return NULL;

    struct stat st;
    size_t cap = 4096;
    if (fstat(fd, &st) == 0 && st.st_size > 0) cap = (size_t)st.st_size + 1;

    char *buf = xmalloc(cap + 1);
    size_t len = 0;
    for (;;) {
        if (len + 4096 > cap) {
            cap = (len + 4096) * 2;
            buf = xrealloc(buf, cap + 1);
        }
        ssize_t r = read(fd, buf + len, cap - len);
        if (r < 0) {
            if (errno == EINTR) continue;
            free(buf); close(fd); return NULL;
        }
        if (r == 0) break;
        len += (size_t)r;
    }
    close(fd);
    buf[len] = '\0';
    if (len_out) *len_out = len;
    return buf;
}

int write_file(const char *path, const char *data, size_t len)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return -1;
    size_t off = 0;
    while (off < len) {
        ssize_t w = write(fd, data + off, len - off);
        if (w < 0) {
            if (errno == EINTR) continue;
            close(fd); return -1;
        }
        off += (size_t)w;
    }
    close(fd);
    return 0;
}

int write_file_atomic(const char *path, const char *data, size_t len)
{
    char tmp[PATH_MAX_LEN];
    snprintf(tmp, sizeof(tmp), "%s.tmp.%d", path, (int)getpid());
    if (write_file(tmp, data, len) < 0) return -1;
    if (rename(tmp, path) < 0) { unlink(tmp); return -1; }
    return 0;
}

int file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

int dir_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

int mkdir_p(const char *path, mode_t mode)
{
    char tmp[PATH_MAX_LEN];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(tmp)) { errno = ENAMETOOLONG; return -1; }
    memcpy(tmp, path, len + 1);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, mode) < 0 && errno != EEXIST) return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, mode) < 0 && errno != EEXIST) return -1;
    return 0;
}

char *trim(char *s)
{
    if (!s) return s;
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = '\0';
    return s;
}

int copy_file(const char *src, const char *dst, mode_t mode)
{
    size_t len = 0;
    char *data = read_file(src, &len);
    if (!data) return -1;
    int rc = write_file_atomic(dst, data, len);
    free(data);
    if (rc == 0) chmod(dst, mode);
    return rc;
}

int copy_tree(const char *src, const char *dst)
{
    struct stat st;
    if (lstat(src, &st) < 0) return -1;

    if (S_ISLNK(st.st_mode)) {
        char buf[PATH_MAX_LEN];
        ssize_t n = readlink(src, buf, sizeof(buf) - 1);
        if (n < 0) return -1;
        buf[n] = '\0';
        unlink(dst);
        return symlink(buf, dst);
    }
    if (S_ISDIR(st.st_mode)) {
        if (mkdir_p(dst, st.st_mode & 07777) < 0) return -1;
        DIR *d = opendir(src);
        if (!d) return -1;
        struct dirent *de;
        int rc = 0;
        while ((de = readdir(d))) {
            if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
            char s[PATH_MAX_LEN], t[PATH_MAX_LEN];
            snprintf(s, sizeof(s), "%s/%s", src, de->d_name);
            snprintf(t, sizeof(t), "%s/%s", dst, de->d_name);
            if (copy_tree(s, t) < 0) rc = -1;
        }
        closedir(d);
        return rc;
    }
    if (S_ISREG(st.st_mode)) {
        if (copy_file(src, dst, st.st_mode & 07777) < 0) return -1;
        struct timespec times[2] = { st.st_atim, st.st_mtim };
        utimensat(AT_FDCWD, dst, times, 0);
        return 0;
    }
    return 0;
}

/* ---------- kv ---------- */
int kv_load(kvlist_t *l, const char *path)
{
    memset(l, 0, sizeof(*l));
    size_t len = 0;
    char *data = read_file(path, &len);
    if (!data) return -1;

    char *save = NULL;
    for (char *line = strtok_r(data, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char *s = trim(line);
        if (!*s || *s == '#' || *s == ';') continue;
        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq = '\0';
        char *k = trim(s);
        char *v = trim(eq + 1);
        if ((v[0] == '"' && v[strlen(v) - 1] == '"') || (v[0] == '\'' && v[strlen(v) - 1] == '\'')) {
            v[strlen(v) - 1] = '\0';
            v++;
        }
        kv_set(l, k, v);
    }
    free(data);
    return 0;
}

int kv_set(kvlist_t *l, const char *key, const char *val)
{
    for (size_t i = 0; i < l->n; i++) {
        if (!strcmp(l->items[i].key, key)) {
            free(l->items[i].val);
            l->items[i].val = xstrdup(val);
            return 0;
        }
    }
    l->items = xrealloc(l->items, sizeof(kv_t) * (l->n + 1));
    l->items[l->n].key = xstrdup(key);
    l->items[l->n].val = xstrdup(val);
    l->n++;
    return 0;
}

const char *kv_get(const kvlist_t *l, const char *key, const char *def)
{
    for (size_t i = 0; i < l->n; i++)
        if (!strcmp(l->items[i].key, key)) return l->items[i].val;
    return def;
}

int kv_get_int(const kvlist_t *l, const char *key, int def)
{
    const char *v = kv_get(l, key, NULL);
    return v ? atoi(v) : def;
}

int kv_save(const kvlist_t *l, const char *path)
{
    size_t cap = 256, len = 0;
    char *buf = xmalloc(cap);
    buf[0] = '\0';
    for (size_t i = 0; i < l->n; i++) {
        size_t need = strlen(l->items[i].key) + strlen(l->items[i].val) + 4;
        if (len + need > cap) { cap = (len + need) * 2; buf = xrealloc(buf, cap); }
        len += (size_t)snprintf(buf + len, cap - len, "%s=%s\n", l->items[i].key, l->items[i].val);
    }
    int rc = write_file_atomic(path, buf, len);
    free(buf);
    return rc;
}

void kv_free(kvlist_t *l)
{
    for (size_t i = 0; i < l->n; i++) { free(l->items[i].key); free(l->items[i].val); }
    free(l->items);
    l->items = NULL;
    l->n = 0;
}

/* ---------- 进程 ---------- */
int run_cmd(char *const argv[])
{
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        execvp(argv[0], argv);
        _exit(127);
    }
    int st = 0;
    if (waitpid(pid, &st, 0) < 0) return -1;
    if (WIFEXITED(st)) return WEXITSTATUS(st);
    return 128 + (WIFSIGNALED(st) ? WTERMSIG(st) : 0);
}

int run_cmd_quiet(char *const argv[])
{
    int devnull = open("/dev/null", O_WRONLY);
    int saved_out = -1, saved_err = -1;
    if (devnull >= 0) {
        saved_out = dup(STDOUT_FILENO);
        saved_err = dup(STDERR_FILENO);
        dup2(devnull, STDOUT_FILENO);
        dup2(devnull, STDERR_FILENO);
        close(devnull);
    }
    int rc = run_cmd(argv);
    if (saved_out >= 0) { dup2(saved_out, STDOUT_FILENO); close(saved_out); }
    if (saved_err >= 0) { dup2(saved_err, STDERR_FILENO); close(saved_err); }
    return rc;
}

int run_shell(const char *cmd)
{
    char *argv[] = { "/bin/sh", "-c", (char *)cmd, NULL };
    return run_cmd(argv);
}

/* ---------- 挂载 ---------- */
int is_mountpoint(const char *path)
{
    char a[PATH_MAX_LEN], b[PATH_MAX_LEN];
    struct stat sa, sb;
    snprintf(a, sizeof(a), "%s", path);
    snprintf(b, sizeof(b), "%s/..", path);
    if (stat(a, &sa) < 0 || stat(b, &sb) < 0) return 0;
    return sa.st_dev != sb.st_dev || sa.st_ino == sb.st_ino;
}

int mount_ensure(const char *src, const char *target, const char *fstype,
                 unsigned long flags, const char *data)
{
    if (mkdir_p(target, 0755) < 0 && errno != EEXIST) return -1;
    if (is_mountpoint(target)) return 0;
    if (mount(src, target, fstype, flags, data) < 0) {
        err("挂载失败 %s -> %s (%s): %s", src, target, fstype, strerror(errno));
        return -1;
    }
    return 0;
}

int umount_all(const char *target)
{
    return umount2(target, MNT_DETACH);
}

int wait_for_path(const char *path, int timeout_ms)
{
    for (int i = 0; i < timeout_ms / 50; i++) {
        if (access(path, F_OK) == 0) return 0;
        sleep_ms(50);
    }
    return -1;
}

/* ---------- 杂项 ---------- */
void sleep_ms(int ms)
{
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

const char *human_size(uint64_t bytes, char *buf, size_t buflen)
{
    static const char *u[] = { "B", "KiB", "MiB", "GiB", "TiB" };
    double v = (double)bytes;
    int i = 0;
    while (v >= 1024.0 && i < 4) { v /= 1024.0; i++; }
    snprintf(buf, buflen, i == 0 ? "%.0f %s" : "%.1f %s", v, u[i]);
    return buf;
}

int parse_bool(const char *s, int def)
{
    if (!s) return def;
    if (!strcasecmp(s, "1") || !strcasecmp(s, "yes") || !strcasecmp(s, "true") ||
        !strcasecmp(s, "on")) return 1;
    if (!strcasecmp(s, "0") || !strcasecmp(s, "no") || !strcasecmp(s, "false") ||
        !strcasecmp(s, "off")) return 0;
    return def;
}
