/* ProperOS 2.5 Mobile · papp
 *
 * 角色：应用包管理与沙箱运行器。
 *   包格式 .papp = tar.gz，内含
 *     manifest.conf   应用清单（id/name/version/entry/icon/permissions）
 *     root/           应用文件树
 *     icon.png        可选图标
 *   运行：独立 mount/pid/uts/ipc 命名空间 + 只读系统目录 + 私有可写目录
 *
 * 开发者：ByUsi Studio · 北啊呢
 */
#define _GNU_SOURCE
#include "../common/properos.h"
#include "../common/sha256.h"
#include <sched.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <pwd.h>
#include <linux/capability.h>
#include <sys/syscall.h>

#define APP_DIR "/data/apps"

static void usage(void)
{
    printf("ProperOS papp %s — 应用包管理器\n\n", PROPEROS_VERSION);
    printf("用法:\n");
    printf("  papp install <包.papp>     安装应用\n");
    printf("  papp remove <应用ID>       卸载应用\n");
    printf("  papp list                  列出已安装应用\n");
    printf("  papp info <应用ID>         查看应用信息\n");
    printf("  papp run <应用ID> [参数]   在沙箱中运行\n");
    printf("  papp verify <包.papp>      校验包完整性\n");
    printf("  papp pack <目录> <输出>    打包应用（开发用）\n");
}

static int app_path(const char *id, const char *suffix, char *out, size_t outlen)
{
    if (strstr(id, "..") || strchr(id, '/')) return -1;
    snprintf(out, outlen, APP_DIR "/%s%s", id, suffix ? suffix : "");
    return 0;
}

static int load_manifest(const char *id, kvlist_t *kv)
{
    char p[PATH_MAX_LEN];
    if (app_path(id, "/manifest.conf", p, sizeof(p)) < 0) return -1;
    return kv_load(kv, p);
}

static int do_verify(const char *pkg)
{
    char hex[65];
    if (sha256_file(pkg, hex) < 0) { err("无法读取 %s", pkg); return 1; }
    printf("包: %s\nSHA-256: %s\n", pkg, hex);

    char sums[PATH_MAX_LEN];
    snprintf(sums, sizeof(sums), "%s.sha256", pkg);
    if (file_exists(sums)) {
        size_t n = 0;
        char *data = read_file(sums, &n);
        if (data) {
            char expect[65] = {0};
            sscanf(data, "%64s", expect);
            free(data);
            if (strcasecmp(expect, hex)) { printf("结果: 校验失败\n"); return 1; }
            printf("结果: 校验通过\n");
            return 0;
        }
    }
    printf("结果: 无 .sha256 旁文件，仅显示摘要\n");
    return 0;
}

static int do_install(const char *pkg)
{
    if (!file_exists(pkg)) { err("包不存在: %s", pkg); return 1; }

    char tmp[] = "/tmp/papp.XXXXXX";
    char *dir = mkdtemp(tmp);
    if (!dir) { err("创建临时目录失败: %s", strerror(errno)); return 1; }

    char *tar_argv[] = { "tar", "-xzf", (char *)pkg, "-C", dir, NULL };
    if (run_cmd(tar_argv) != 0) {
        err("解包失败（不是有效的 .papp）");
        run_shell(xasprintf("rm -rf %s", dir));
        return 1;
    }

    char mpath[PATH_MAX_LEN];
    snprintf(mpath, sizeof(mpath), "%s/manifest.conf", dir);
    kvlist_t kv;
    if (kv_load(&kv, mpath) < 0) {
        err("包内缺少 manifest.conf");
        run_shell(xasprintf("rm -rf %s", dir));
        return 1;
    }

    const char *id = kv_get(&kv, "id", NULL);
    const char *name = kv_get(&kv, "name", id);
    const char *version = kv_get(&kv, "version", "0");
    if (!id || !*id) { err("manifest 缺少 id"); kv_free(&kv); return 1; }

    /* 包内文件校验 */
    char vpath[PATH_MAX_LEN];
    snprintf(vpath, sizeof(vpath), "%s/SHA256SUMS", dir);
    if (file_exists(vpath)) {
        char *data = read_file(vpath, NULL);
        if (data) {
            char *save = NULL;
            int bad = 0;
            for (char *l = strtok_r(data, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
                char expect[65], fn[256];
                if (sscanf(trim(l), "%64s %255s", expect, fn) != 2) continue;
                char fp[PATH_MAX_LEN], act[65];
                snprintf(fp, sizeof(fp), "%s/%s", dir, fn);
                if (sha256_file(fp, act) < 0 || strcasecmp(act, expect)) {
                    err("文件校验失败: %s", fn);
                    bad = 1;
                    break;
                }
            }
            free(data);
            if (bad) { kv_free(&kv); return 1; }
            info("包内容校验通过");
        }
    }

    char dest[PATH_MAX_LEN];
    if (app_path(id, NULL, dest, sizeof(dest)) < 0) { err("非法应用 ID"); kv_free(&kv); return 1; }

    /* 版本比较：允许覆盖升级 */
    char oldver[64] = "0";
    kvlist_t old;
    if (kv_load(&old, dest[0] ? xasprintf("%s/manifest.conf", dest) : "") == 0) {
        snprintf(oldver, sizeof(oldver), "%s", kv_get(&old, "version", "0"));
        kv_free(&old);
    }

    info("安装 %s %s → %s（原版本 %s）", name, version, dest, oldver);
    run_shell(xasprintf("rm -rf '%s.new'", dest));
    if (mkdir_p(xasprintf("%s.new", dest), 0755) < 0) { err("创建目录失败"); return 1; }

    char srcmanifest[PATH_MAX_LEN], dstmanifest[PATH_MAX_LEN];
    snprintf(srcmanifest, sizeof(srcmanifest), "%s/manifest.conf", dir);
    snprintf(dstmanifest, sizeof(dstmanifest), "%s.new/manifest.conf", dest);
    copy_file(srcmanifest, dstmanifest, 0644);

    char srcroot[PATH_MAX_LEN], dstroot[PATH_MAX_LEN];
    snprintf(srcroot, sizeof(srcroot), "%s/root", dir);
    snprintf(dstroot, sizeof(dstroot), "%s.new/root", dest);
    if (dir_exists(srcroot)) {
        mkdir_p(dstroot, 0755);
        run_shell(xasprintf("cp -a '%s/.' '%s/'", srcroot, dstroot));
    }

    char icon[PATH_MAX_LEN], dsticon[PATH_MAX_LEN];
    snprintf(icon, sizeof(icon), "%s/icon.png", dir);
    if (file_exists(icon)) {
        snprintf(dsticon, sizeof(dsticon), "%s.new/icon.png", dest);
        copy_file(icon, dsticon, 0644);
    }

    run_shell(xasprintf("rm -rf '%s.old'; [ -d '%s' ] && mv '%s' '%s.old'; mv '%s.new' '%s'; rm -rf '%s.old'",
                        dest, dest, dest, dest, dest, dest, dest));
    /* 应用私有数据 */
    run_shell(xasprintf("mkdir -p '%s/data' '%s/home' && chown -R 10000:10000 '%s/data' '%s/home'", dest, dest, dest, dest));

    printf("已安装 %s（%s %s）\n", name, id, version);
    kv_free(&kv);
    run_shell(xasprintf("rm -rf %s", dir));
    return 0;
}

static int do_remove(const char *id)
{
    char dest[PATH_MAX_LEN];
    if (app_path(id, NULL, dest, sizeof(dest)) < 0) { err("非法应用 ID"); return 1; }
    if (!dir_exists(dest)) { err("应用未安装: %s", id); return 1; }

    if (parse_bool(getenv("PAPP_FORCE"), 0)) {
        run_shell(xasprintf("rm -rf '%s'", dest));
        printf("已卸载 %s\n", id);
        return 0;
    }
    /* 保留用户数据至回收站 */
    char trash[PATH_MAX_LEN];
    snprintf(trash, sizeof(trash), "/data/.trash/%s.%ld", id, (long)time(NULL));
    mkdir_p("/data/.trash", 0755);
    run_shell(xasprintf("mv '%s' '%s'", dest, trash));
    printf("已卸载 %s（用户数据保留在 %s）\n", id, trash);
    return 0;
}

static int do_list(void)
{
    DIR *d = opendir(APP_DIR);
    if (!d) { printf("尚无已安装应用\n"); return 0; }
    printf("%-34s %-22s %-10s %s\n", "应用 ID", "名称", "版本", "入口");
    printf("%-34s %-22s %-10s %s\n", "----------------------------------",
           "----------------------", "----------", "----------------");
    struct dirent *de;
    int n = 0;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.') continue;
        kvlist_t kv;
        if (load_manifest(de->d_name, &kv) < 0) continue;
        printf("%-34s %-22s %-10s %s\n", de->d_name,
               kv_get(&kv, "name", "?"), kv_get(&kv, "version", "?"),
               kv_get(&kv, "entry", "?"));
        kv_free(&kv);
        n++;
    }
    closedir(d);
    if (!n) printf("（空）\n");
    return 0;
}

static int do_info(const char *id)
{
    kvlist_t kv;
    if (load_manifest(id, &kv) < 0) { err("应用未安装: %s", id); return 1; }
    printf("应用 ID   : %s\n", id);
    for (size_t i = 0; i < kv.n; i++)
        printf("%-10s: %s\n", kv.items[i].key, kv.items[i].val);
    kv_free(&kv);
    return 0;
}

static char g_base[PATH_MAX_LEN];

/* 在命名空间中构建根并执行入口 */
static int sandbox_exec(const char *id, const char *approot, const char *entry, int net, char **args)
{
    if (unshare(CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWIPC | (net ? 0 : CLONE_NEWNET)) < 0) {
        err("创建命名空间失败: %s（需要 root）", strerror(errno));
        return 1;
    }
    mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL);

    char root[PATH_MAX_LEN], old[PATH_MAX_LEN];
    snprintf(root, sizeof(root), "%s/root", g_base);
    snprintf(old, sizeof(old), "%s/.old", g_base);
    mkdir_p(root, 0755);
    mkdir_p(old, 0755);

    if (mount(approot, root, NULL, MS_BIND | MS_REC, NULL) < 0) {
        err("挂载应用根失败: %s", strerror(errno));
        return 1;
    }

    static const char *sysdirs[] = { "/usr", "/lib", "/lib64", "/bin", "/sbin", "/etc", "/var" };
    for (size_t i = 0; i < ARRAY_LEN(sysdirs); i++) {
        if (!dir_exists(sysdirs[i])) continue;
        char dst[PATH_MAX_LEN];
        snprintf(dst, sizeof(dst), "%s%s", root, sysdirs[i]);
        mkdir_p(dst, 0755);
        if (mount(sysdirs[i], dst, NULL, MS_BIND | MS_REC, NULL) < 0)
            warn("绑定 %s 失败: %s", sysdirs[i], strerror(errno));
        else
            mount(NULL, dst, NULL, MS_BIND | MS_REMOUNT | MS_RDONLY | MS_REC, NULL);
    }

    char proc[PATH_MAX_LEN], dev[PATH_MAX_LEN], tmp[PATH_MAX_LEN];
    snprintf(proc, sizeof(proc), "%s/proc", root);
    snprintf(dev, sizeof(dev), "%s/dev", root);
    snprintf(tmp, sizeof(tmp), "%s/tmp", root);
    mkdir_p(proc, 0555); mkdir_p(dev, 0755); mkdir_p(tmp, 01777);
    mount("proc", proc, "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL);
    mount("/dev", dev, NULL, MS_BIND | MS_REC, NULL);
    mount("tmpfs", tmp, "tmpfs", MS_NOSUID | MS_NODEV, "mode=1777,size=64m");

    /* 应用私有可写目录：/data 与 /home */
    char appdir[PATH_MAX_LEN], appdata[PATH_MAX_LEN], apphome[PATH_MAX_LEN];
    snprintf(appdir, sizeof(appdir), APP_DIR "/%s", id);
    snprintf(appdata, sizeof(appdata), "%s/data", appdir);
    snprintf(apphome, sizeof(apphome), "%s/home", appdir);
    mkdir_p(appdata, 0700);
    mkdir_p(apphome, 0700);

    char dst[PATH_MAX_LEN];
    snprintf(dst, sizeof(dst), "%s/data", root);
    mkdir_p(dst, 0700);
    mount(appdata, dst, NULL, MS_BIND, NULL);
    snprintf(dst, sizeof(dst), "%s/home", root);
    mkdir_p(dst, 0700);
    mount(apphome, dst, NULL, MS_BIND, NULL);

    if (pivot_root(root, old) == 0) {
        chdir("/");
        umount2("/.old", MNT_DETACH);
    } else {
        /* 某些受限环境（容器内）不允许 pivot_root，退化为 chroot；
         * 随后丢弃全部 capability 并切换到非特权 uid，chroot 无法逃逸 */
        if (chdir(root) < 0 || chroot(".") < 0) {
            err("chroot 失败: %s", strerror(errno));
            return 1;
        }
        chdir("/");
    }

    prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);

    /* 应用以非特权 uid 运行（必须在清空 capability 之前完成，否则 EPERM） */
    if (geteuid() == 0) {
        if (setgroups(0, NULL) < 0) warn("setgroups: %s", strerror(errno));
        if (setgid(10000) < 0) warn("setgid: %s", strerror(errno));
        if (setuid(10000) < 0) warn("setuid: %s", strerror(errno));
    }

    /* 降权后清空 capability（非 root 已无 capability，此处为双保险） */
    struct __user_cap_header_struct hdr = { _LINUX_CAPABILITY_VERSION_3, 0 };
    struct __user_cap_data_struct cap[2] = { {0}, {0} };
    if (syscall(SYS_capset, &hdr, cap) < 0) dbg("capset: %s", strerror(errno));
    setenv("HOME", "/home", 1);
    setenv("TMPDIR", "/tmp", 1);
    setenv("PROPEROS_APP_ID", id, 1);
    setenv("PATH", "/usr/bin:/bin:/usr/sbin:/sbin", 1);

    /* 参数拼装：入口 + 用户参数 */
    char **av = xcalloc(64, sizeof(char *));
    int n = 0;
    av[n++] = (char *)entry;
    for (int i = 0; args && args[i] && n < 62; i++) av[n++] = args[i];
    av[n] = NULL;

    execv(entry, av);
    /* 入口若是脚本，交给 sh */
    execv("/bin/sh", (char *[]){ "/bin/sh", (char *)entry, NULL });
    err("无法执行入口 %s: %s", entry, strerror(errno));
    return 127;
}

static int do_run(const char *id, char **args)
{
    kvlist_t kv;
    if (load_manifest(id, &kv) < 0) { err("应用未安装: %s", id); return 1; }

    const char *entry = kv_get(&kv, "entry", "/app/run");
    int net = parse_bool(kv_get(&kv, "network", "0"), 0);

    char approot[PATH_MAX_LEN];
    snprintf(approot, sizeof(approot), APP_DIR "/%s/root", id);

    /* 入口必须落在应用根内 */
    char fullentry[PATH_MAX_LEN];
    if (entry[0] == '/') snprintf(fullentry, sizeof(fullentry), "%s", entry);
    else snprintf(fullentry, sizeof(fullentry), "/%s", entry);

    info("运行 %s（网络=%s）", id, net ? "允许" : "隔离");

    /* 需要 pid 命名空间时必须在 unshare 后 fork */
    snprintf(g_base, sizeof(g_base), "/run/papp.%d", (int)getpid());
    mkdir_p(g_base, 0755);
    pid_t pid = fork();
    if (pid == 0) {
        if (unshare(CLONE_NEWPID) < 0) warn("PID 命名空间不可用");
        pid_t inner = fork();
        if (inner == 0) _exit(sandbox_exec(id, approot, fullentry, net, args));
        int st = 0;
        waitpid(inner, &st, 0);
        _exit(WIFEXITED(st) ? WEXITSTATUS(st) : 1);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    run_shell(xasprintf("rm -rf %s", g_base));
    int rc = WIFEXITED(st) ? WEXITSTATUS(st) : 1;
    kv_free(&kv);
    return rc;
}

static int do_pack(const char *srcdir, const char *out)
{
    if (!dir_exists(srcdir)) { err("目录不存在: %s", srcdir); return 1; }
    char m[PATH_MAX_LEN];
    snprintf(m, sizeof(m), "%s/manifest.conf", srcdir);
    if (!file_exists(m)) { err("缺少 manifest.conf"); return 1; }

    /* 生成内容清单 */
    char sums[PATH_MAX_LEN];
    snprintf(sums, sizeof(sums), "%s/SHA256SUMS", srcdir);
    run_shell(xasprintf("cd '%s' && find root -type f -exec sha256sum {} + > SHA256SUMS", srcdir));
    run_shell(xasprintf("tar -czf '%s' -C '%s' manifest.conf root SHA256SUMS icon.png 2>/dev/null || "
                        "tar -czf '%s' -C '%s' manifest.conf root SHA256SUMS", out, srcdir, out, srcdir));
    char hex[65];
    if (sha256_file(out, hex) == 0) {
        char side[PATH_MAX_LEN];
        snprintf(side, sizeof(side), "%s.sha256", out);
        char line[96];
        snprintf(line, sizeof(line), "%s  %s\n", hex, strrchr(out, '/') ? strrchr(out, '/') + 1 : out);
        write_file(side, line, strlen(line));
        printf("已打包 %s\nSHA-256: %s\n", out, hex);
    }
    return 0;
}

int main(int argc, char **argv)
{
    plog_open("papp");
    if (argc < 2) { usage(); return 1; }

    const char *cmd = argv[1];
    if (!strcmp(cmd, "--version")) {
        printf("papp %s (%s · %s)\n", PROPEROS_VERSION, PROPEROS_VENDOR, PROPEROS_AUTHOR);
        return 0;
    }
    if (!strcmp(cmd, "install") && argc >= 3) return do_install(argv[2]);
    if (!strcmp(cmd, "remove") && argc >= 3) return do_remove(argv[2]);
    if (!strcmp(cmd, "list")) return do_list();
    if (!strcmp(cmd, "info") && argc >= 3) return do_info(argv[2]);
    if (!strcmp(cmd, "run") && argc >= 3) return do_run(argv[2], &argv[3]);
    if (!strcmp(cmd, "verify") && argc >= 3) return do_verify(argv[2]);
    if (!strcmp(cmd, "pack") && argc >= 4) return do_pack(argv[2], argv[3]);

    usage();
    return 1;
}
