/* ProperOS 25.0.0 Mobile · properd
 *
 * 角色：移动端会话与服务管理器（Android init/zygote 的对应层）。
 *   - 扫描 /etc/properos/services/*.service，拉起并守护系统服务
 *   - 崩溃自动重启，带退避与重启上限
 *   - 通过 /run/properd.sock 提供 list/status/start/stop/restart/launch
 *   - 输出 /run/properos/services.json 供界面层读取
 *
 * 开发者：ByUsi Studio · 北啊呢
 */
#define _GNU_SOURCE
#include "../common/properos.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <pwd.h>
#include <grp.h>

#define SOCK_PATH   "/run/properd.sock"
#define SERVICE_DIR "/etc/properos/services"
#define STATE_DIR   "/run/properos"
#define MAX_SERVICES 64

typedef struct {
    char  name[64];
    char  exec[512];
    char  user[32];
    int   restart;          /* 0=never 1=on-failure 2=always */
    int   pid;
    int   restarts;
    int   running;
    time_t last_start;
    time_t next_restart;
    int   exit_code;
} service_t;

static service_t g_svc[MAX_SERVICES];
static int g_nsvc;
static volatile sig_atomic_t g_reload;
static volatile sig_atomic_t g_stop;

static void on_signal(int sig)
{
    if (sig == SIGHUP) g_reload = 1;
    else g_stop = 1;
}

static void load_services(void)
{
    g_nsvc = 0;
    DIR *d = opendir(SERVICE_DIR);
    if (!d) { warn("服务目录不存在: %s", SERVICE_DIR); return; }

    struct dirent *de;
    while ((de = readdir(d)) && g_nsvc < MAX_SERVICES) {
        size_t n = strlen(de->d_name);
        if (n < 9 || strcmp(de->d_name + n - 8, ".service")) continue;

        char path[PATH_MAX_LEN];
        snprintf(path, sizeof(path), "%s/%s", SERVICE_DIR, de->d_name);
        kvlist_t kv;
        if (kv_load(&kv, path) < 0) continue;

        service_t *s = &g_svc[g_nsvc];
        memset(s, 0, sizeof(*s));
        snprintf(s->name, sizeof(s->name), "%s", kv_get(&kv, "Name", de->d_name));
        char *dot = strstr(s->name, ".service");
        if (dot) *dot = '\0';
        snprintf(s->exec, sizeof(s->exec), "%s", kv_get(&kv, "Exec", ""));
        snprintf(s->user, sizeof(s->user), "%s", kv_get(&kv, "User", "root"));
        const char *r = kv_get(&kv, "Restart", "on-failure");
        s->restart = !strcmp(r, "always") ? 2 : (!strcmp(r, "never") ? 0 : 1);
        s->pid = -1;
        kv_free(&kv);

        if (s->exec[0]) {
            info("注册服务 %s", s->name);
            g_nsvc++;
        }
    }
    closedir(d);
}

static service_t *find_service(const char *name)
{
    for (int i = 0; i < g_nsvc; i++)
        if (!strcmp(g_svc[i].name, name)) return &g_svc[i];
    return NULL;
}

static int spawn_service(service_t *s)
{
    pid_t pid = fork();
    if (pid < 0) { err("fork 失败: %s", strerror(errno)); return -1; }
    if (pid == 0) {
        setsid();
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            if (devnull > 2) close(devnull);
        }
        /* 服务日志进入 journal */
        if (!strcmp(s->user, "root")) {
            /* 已是 root */
        } else {
            struct passwd *pw = getpwnam(s->user);
            if (pw) {
                setgid(pw->pw_gid);
                initgroups(pw->pw_name, pw->pw_gid);
                setuid(pw->pw_uid);
            }
        }
        setenv("PROPEROS_SERVICE", s->name, 1);
        char *argv[] = { "/bin/sh", "-c", s->exec, NULL };
        execv("/bin/sh", argv);
        _exit(127);
    }
    s->pid = pid;
    s->running = 1;
    s->last_start = time(NULL);
    info("启动服务 %s (pid %d)", s->name, pid);
    return 0;
}

static void stop_service(service_t *s)
{
    if (s->pid > 0) {
        kill(-s->pid, SIGTERM);
        kill(s->pid, SIGTERM);
        for (int i = 0; i < 20; i++) {
            if (kill(s->pid, 0) < 0) break;
            sleep_ms(100);
        }
        if (kill(s->pid, 0) == 0) { kill(-s->pid, SIGKILL); kill(s->pid, SIGKILL); }
        info("停止服务 %s", s->name);
    }
    s->pid = -1;
    s->running = 0;
}

static void reap_children(void)
{
    int st;
    pid_t pid;
    while ((pid = waitpid(-1, &st, WNOHANG)) > 0) {
        for (int i = 0; i < g_nsvc; i++) {
            service_t *s = &g_svc[i];
            if (s->pid != pid) continue;
            s->running = 0;
            s->pid = -1;
            s->exit_code = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
            int failed = !(WIFEXITED(st) && WEXITSTATUS(st) == 0);
            warn("服务 %s 退出（code=%d）", s->name, s->exit_code);

            int should = (s->restart == 2) || (s->restart == 1 && failed);
            if (should && !g_stop) {
                int backoff = 1 << (s->restarts > 4 ? 4 : s->restarts);
                if (backoff > 30) backoff = 30;
                s->next_restart = time(NULL) + backoff;
                s->restarts++;
                warn("%d 秒后重启 %s（第 %d 次）", backoff, s->name, s->restarts);
            }
        }
    }
}

static void write_state(void)
{
    mkdir_p(STATE_DIR, 0755);
    size_t cap = 8192, len = 0;
    char *buf = xmalloc(cap);
    len += (size_t)snprintf(buf + len, cap - len, "{\"generated\":%ld,\"services\":[", (long)time(NULL));
    for (int i = 0; i < g_nsvc; i++) {
        service_t *s = &g_svc[i];
        len += (size_t)snprintf(buf + len, cap - len,
            "%s{\"name\":\"%s\",\"state\":\"%s\",\"pid\":%d,\"restarts\":%d,\"exit\":%d}",
            i ? "," : "", s->name, s->running ? "running" : "stopped",
            s->pid, s->restarts, s->exit_code);
    }
    snprintf(buf + len, cap - len, "]}");
    write_file_atomic(STATE_DIR "/services.json", buf, strlen(buf));
    free(buf);
}

static void reply(int fd, const char *fmt, ...)
{
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) { ssize_t w = write(fd, buf, (size_t)n); (void)w; }
    const char *end = "\n.\n";
    ssize_t w = write(fd, end, 3); (void)w;
}

static void handle_command(int fd)
{
    char line[512] = {0};
    ssize_t n = read(fd, line, sizeof(line) - 1);
    if (n <= 0) return;
    char *cmd = trim(line);

    if (!strcmp(cmd, "list")) {
        char out[3072] = {0};
        size_t off = 0;
        for (int i = 0; i < g_nsvc; i++) {
            service_t *s = &g_svc[i];
            off += (size_t)snprintf(out + off, sizeof(out) - off,
                                    "%s\t%s\t%d\t%d\n", s->name,
                                    s->running ? "running" : "stopped", s->pid, s->restarts);
        }
        reply(fd, "%s", out);
        return;
    }

    char name[64] = {0}, action[32] = {0};
    if (sscanf(cmd, "%31s %63s", action, name) == 2) {
        service_t *s = find_service(name);
        if (!s) { reply(fd, "ERR 未找到服务 %s", name); return; }

        if (!strcmp(action, "status")) {
            reply(fd, "OK %s state=%s pid=%d restarts=%d exit=%d",
                  s->name, s->running ? "running" : "stopped", s->pid, s->restarts, s->exit_code);
        } else if (!strcmp(action, "start")) {
            if (s->running) { reply(fd, "OK 已在运行"); return; }
            s->restarts = 0;
            reply(fd, spawn_service(s) == 0 ? "OK 已启动" : "ERR 启动失败");
        } else if (!strcmp(action, "stop")) {
            s->restart = 0;
            stop_service(s);
            reply(fd, "OK 已停止");
        } else if (!strcmp(action, "restart")) {
            stop_service(s);
            s->restart = 2;
            reply(fd, spawn_service(s) == 0 ? "OK 已重启" : "ERR 重启失败");
        } else if (!strcmp(action, "launch")) {
            /* 启动一个 .papp 应用：交给 papp 运行，服务化跟踪 */
            char cmdline[600];
            snprintf(cmdline, sizeof(cmdline), "/usr/bin/papp run %s", name);
            char *argv[] = { "/bin/sh", "-c", cmdline, NULL };
            int rc = run_cmd(argv);
            reply(fd, rc == 0 ? "OK 已退出" : "ERR 退出码 %d", rc);
        } else {
            reply(fd, "ERR 未知命令");
        }
        return;
    }
    reply(fd, "ERR 用法: list | status|start|stop|restart|launch <name>");
}

int main(int argc, char **argv)
{
    plog_open("properd");
    if (argc > 1 && !strcmp(argv[1], "--version")) {
        printf("properd %s (%s · %s)\n", PROPEROS_VERSION, PROPEROS_VENDOR, PROPEROS_AUTHOR);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "--list")) {
        load_services();
        for (int i = 0; i < g_nsvc; i++) printf("%s\n", g_svc[i].name);
        return 0;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigaction(SIGHUP, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    load_services();
    for (int i = 0; i < g_nsvc; i++) spawn_service(&g_svc[i]);

    mkdir_p("/run", 0755);
    unlink(SOCK_PATH);
    int lfd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (lfd < 0) { err("创建 socket 失败: %s", strerror(errno)); return 1; }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", SOCK_PATH);

    if (bind(lfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        err("bind 失败: %s", strerror(errno));
        return 1;
    }
    chmod(SOCK_PATH, 0660);
    listen(lfd, 8);
    info("properd 就绪，%d 个服务", g_nsvc);

    time_t last_state = 0;
    while (!g_stop) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(lfd, &fds);
        struct timeval tv = { 1, 0 };
        int rc = select(lfd + 1, &fds, NULL, NULL, &tv);
        if (rc > 0 && FD_ISSET(lfd, &fds)) {
            int cfd = accept4(lfd, NULL, NULL, SOCK_CLOEXEC);
            if (cfd >= 0) { handle_command(cfd); close(cfd); }
        }
        reap_children();

        time_t now = time(NULL);
        for (int i = 0; i < g_nsvc; i++) {
            service_t *s = &g_svc[i];
            if (!s->running && s->next_restart && now >= s->next_restart) {
                s->next_restart = 0;
                spawn_service(s);
            }
        }
        if (now - last_state >= 5) { write_state(); last_state = now; }
        if (g_reload) {
            info("重新加载服务定义");
            for (int i = 0; i < g_nsvc; i++) stop_service(&g_svc[i]);
            load_services();
            for (int i = 0; i < g_nsvc; i++) spawn_service(&g_svc[i]);
            g_reload = 0;
        }
    }

    info("properd 退出，停止全部服务");
    for (int i = 0; i < g_nsvc; i++) stop_service(&g_svc[i]);
    close(lfd);
    unlink(SOCK_PATH);
    write_state();
    return 0;
}
