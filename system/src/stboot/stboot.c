/* ProperOS 25.0.0 Mobile · StBoot
 *
 * 角色：引导阶段 2，接管 ProLoader 挂载好的只读系统分区，完成：
 *   1. 挂载 /data 数据分区（读写）
 *   2. 首次启动时从系统种子初始化 /data 下的 etc、var、home、root
 *   3. 以 bind mount 叠加为可写层（系统本体保持只读，可 A/B 回滚）
 *   4. 迁移 /proc /sys /dev /run 并 switch_root 到真正的 init
 *
 * 开发者：ByUsi Studio · 北啊呢
 */
#define _GNU_SOURCE
#include "../common/properos.h"
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <linux/reboot.h>

#define DATA_MNT "data"

static const char *g_root = "/sysroot";
static const char *g_slot = "a";

static int resolve_data(char *out, size_t outlen)
{
    const char *env = getenv("PROPEROS_DATA");
    if (env && *env) { snprintf(out, outlen, "%s", env); return 0; }

    /* 通过 sysfs uevent 找 properos-data 分区 */
    DIR *d = opendir("/sys/class/block");
    if (d) {
        struct dirent *de;
        while ((de = readdir(d))) {
            if (de->d_name[0] == '.') continue;
            char p[PATH_MAX_LEN];
            snprintf(p, sizeof(p), "/sys/class/block/%s/uevent", de->d_name);
            char *ue = read_file(p, NULL);
            if (!ue) continue;
            char *line = strstr(ue, "PARTNAME=");
            if (line) {
                line += 9;
                char *e = strchr(line, '\n');
                if (e) *e = '\0';
                if (!strcmp(line, "properos-data")) {
                    snprintf(out, outlen, "/dev/%s", de->d_name);
                    free(ue);
                    closedir(d);
                    return 0;
                }
            }
            free(ue);
        }
        closedir(d);
    }

    const char *cand[] = { "/dev/vda3", "/dev/sda3", "/dev/mmcblk0p3", "/dev/nvme0n1p3" };
    for (size_t i = 0; i < ARRAY_LEN(cand); i++)
        if (file_exists(cand[i])) { snprintf(out, outlen, "%s", cand[i]); return 0; }
    return -1;
}

static int mount_data(const char *dev, const char *target)
{
    static const char *types[] = { "ext4", "f2fs", "btrfs" };
    for (size_t i = 0; i < ARRAY_LEN(types); i++) {
        if (mount(dev, target, types[i], MS_NOSUID | MS_NODEV, "rw") == 0) {
            info("data partition %s mounted (%s)", dev, types[i]);
            return 0;
        }
    }
    return -1;
}

/* 把系统种子复制到数据分区（仅首次或缺失时） */
static void seed_dir(const char *name)
{
    char src[PATH_MAX_LEN], dst[PATH_MAX_LEN];
    snprintf(src, sizeof(src), "%s/%s", g_root, name);
    snprintf(dst, sizeof(dst), "%s/%s/%s", g_root, DATA_MNT, name);

    if (!dir_exists(src)) return;
    if (dir_exists(dst)) {
        dbg("/data/%s exists, skipping seed", name);
        return;
    }

    info("first boot: seeding /data/%s", name);
    if (mkdir_p(dst, 0755) < 0) { err("failed to create %s: %s", dst, strerror(errno)); return; }

    /* 用系统 tar 保留权限与符号链接；initramfs 内为 busybox tar */
    char *argv[] = { "tar", "-C", src, "-cf", "-", ".", NULL };
    int pipefd[2];
    if (pipe(pipefd) < 0) return;
    pid_t pid = fork();
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        execvp("tar", argv);
        _exit(127);
    }
    close(pipefd[1]);

    pid_t pid2 = fork();
    if (pid2 == 0) {
        close(pipefd[1]);
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[0]);
        char *targv[] = { "tar", "-C", dst, "-xpf", "-", NULL };
        execvp("tar", targv);
        _exit(127);
    }
    close(pipefd[0]);
    int st1 = 0, st2 = 0;
    waitpid(pid, &st1, 0);
    waitpid(pid2, &st2, 0);
    if (st1 || st2) warn("/data/%s seed incomplete", name);
}

static int bind_over(const char *name)
{
    char src[PATH_MAX_LEN], dst[PATH_MAX_LEN];
    snprintf(src, sizeof(src), "%s/%s/%s", g_root, DATA_MNT, name);
    snprintf(dst, sizeof(dst), "%s/%s", g_root, name);
    if (!dir_exists(src)) return 0;
    if (mount(src, dst, NULL, MS_BIND, NULL) < 0) {
        err("bind %s -> %s failed: %s", src, dst, strerror(errno));
        return -1;
    }
    /* 递归标记，避免子挂载泄漏 */
    mount(NULL, dst, NULL, MS_BIND | MS_REC, NULL);
    dbg("writable layer over /%s", name);
    return 0;
}

static void move_mnt(const char *name)
{
    /* 源必须写绝对路径：相对路径按当前工作目录解析，若 CWD 已切到 g_root，
     * "dev" 就会解析成 <root>/dev —— 那只是一个普通目录，MS_MOVE 直接 EINVAL。 */
    char src[PATH_MAX_LEN], dst[PATH_MAX_LEN];
    snprintf(src, sizeof(src), "/%s", name);
    snprintf(dst, sizeof(dst), "%s/%s", g_root, name);
    mkdir_p(dst, 0755);
    if (mount(src, dst, NULL, MS_MOVE, NULL) < 0)
        dbg("move %s skipped: %s", src, strerror(errno));
}

static void write_boot_status(void)
{
    mkdir_p("/run/properos", 0755);
    kvlist_t st;
    memset(&st, 0, sizeof(st));
    char buf[32];
    snprintf(buf, sizeof(buf), "%ld", (long)time(NULL));
    kv_set(&st, "boot_epoch", buf);
    kv_set(&st, "slot", g_slot);
    kv_set(&st, "stage", "stboot");
    kv_set(&st, "version", PROPEROS_VERSION);
    kv_save(&st, "/run/properos/boot.status");
    kv_free(&st);
}

static int switch_root(void)
{
    if (chdir(g_root) < 0) return -1;

    static const char *mnts[] = { "proc", "sys", "dev", "run" };
    /* 关键：若挂载树是 shared，MS_MOVE 会返回 EINVAL（switch_root 失败）。
     * busybox/util-linux 的 switch_root 同样先做这一次 MS_REC|MS_PRIVATE。 */
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) < 0)
        warn("cannot make mounts private: %s", strerror(errno));

    for (size_t i = 0; i < ARRAY_LEN(mnts); i++) move_mnt(mnts[i]);

    if (mount(".", "/", NULL, MS_MOVE, NULL) < 0) {
        err("switch_root failed: %s", strerror(errno));
        return -1;
    }
    if (chroot(".") < 0) return -1;
    chdir("/");
    return 0;
}

int main(int argc, char **argv)
{
    plog_open("stboot");

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--root") && i + 1 < argc) g_root = argv[++i];
        else if (!strcmp(argv[i], "--slot") && i + 1 < argc) g_slot = argv[++i];
        else if (!strcmp(argv[i], "--help")) {
            printf("usage: stboot [--root /sysroot] [--slot a|b]\n");
            return 0;
        } else if (!strcmp(argv[i], "--version")) {
            printf("StBoot %s (%s / %s)\n", PROPEROS_VERSION, PROPEROS_VENDOR, PROPEROS_AUTHOR);
            return 0;
        }
    }

    info("StBoot start, root %s, slot %s", g_root, g_slot);

    char datadev[PATH_MAX_LEN];
    char datamnt[PATH_MAX_LEN];
    snprintf(datamnt, sizeof(datamnt), "%s/%s", g_root, DATA_MNT);

    int have_data = 0;
    if (resolve_data(datadev, sizeof(datadev)) == 0) {
        if (mkdir_p(datamnt, 0755) == 0 && mount_data(datadev, datamnt) == 0) {
            have_data = 1;
        } else {
            warn("data partition mount failed, falling back to tmpfs layer");
        }
    } else {
        warn("no data partition found");
    }

    if (have_data) {
        /* 首次启动种子 */
        seed_dir("etc");
        seed_dir("var");
        seed_dir("home");
        seed_dir("root");
        /* 设备标识 */
        char mid[PATH_MAX_LEN];
        snprintf(mid, sizeof(mid), "%s/%s/etc/machine-id", g_root, DATA_MNT);
        if (!file_exists(mid)) {
            FILE *f = fopen("/dev/urandom", "rb");
            char id[33] = {0};
            if (f) {
                unsigned char raw[16];
                if (fread(raw, 1, sizeof(raw), f) == sizeof(raw))
                    for (int i = 0; i < 16; i++) sprintf(id + i * 2, "%02x", raw[i]);
                fclose(f);
            }
            if (id[0]) write_file(mid, id, 32);
        }
        bind_over("etc");
        bind_over("var");
        bind_over("home");
        bind_over("root");
    } else {
        /* 无数据分区：把原始 /etc /var 先绑定到临时点，再用 tmpfs 覆盖并回填，
         * 保证系统仍能启动且配置完整（仅在内存中，掉电丢失） */
        char seed[PATH_MAX_LEN], p[PATH_MAX_LEN];
        snprintf(seed, sizeof(seed), "%s/.etc-seed", g_root);
        mkdir_p(seed, 0755);
        if (mount("/sysroot/etc", seed, NULL, MS_BIND, NULL) == 0) {
            snprintf(p, sizeof(p), "%s/etc", g_root);
            if (mount("tmpfs", p, "tmpfs", MS_NOSUID | MS_NODEV, "mode=0755") == 0) {
                run_shell("cp -a /sysroot/.etc-seed/. /sysroot/etc/ 2>/dev/null");
            }
            umount2(seed, MNT_DETACH);
        }
        snprintf(p, sizeof(p), "%s/var", g_root);
        mount("tmpfs", p, "tmpfs", MS_NOSUID | MS_NODEV, "mode=0755");
    }

    write_boot_status();

    /* 首启标记：供 properd 做一次性初始化 */
    char flag[PATH_MAX_LEN];
    snprintf(flag, sizeof(flag), "%s/%s/.first-boot", g_root, DATA_MNT);
    if (have_data && access(flag, F_OK) < 0) write_file(flag, "", 0);

    if (switch_root() < 0) {
        err("cannot switch to system root");
        return 1;
    }

    info("system ready, starting init");
    setenv("PROPEROS_SLOT", g_slot, 1);
    char *init_argv[] = { "/sbin/init", NULL };
    execv("/sbin/init", init_argv);

    /* 兜底：systemd 缺失时用 shell */
    char *sh_argv[] = { "/bin/sh", NULL };
    execv("/bin/sh", sh_argv);
    return 1;
}
