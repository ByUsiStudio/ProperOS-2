/* ProperOS 25.0.0 Mobile · ProLoader
 *
 * 角色：initramfs 中的第一阶段引导器（PID 1）。
 *   1. 建立 /proc /sys /dev /run
 *   2. 解析内核参数与 ESP 上的 proloader.conf
 *   3. A/B 槽选择：失败计数、校验失败自动回滚到另一槽
 *   4. 校验 boot 产物 SHA-256
 *   5. 只读挂载系统分区，移交 StBoot
 *
 * 开发者：ByUsi Studio · 北啊呢
 */
#define _GNU_SOURCE
#include "../common/properos.h"
#include "../common/sha256.h"
#include <sys/sysmacros.h>
#include <sys/time.h>

#define ESP_MNT      "/esp"
#define SYSROOT      "/sysroot"
#define STBOOT_PATH  "/usr/lib/properos/stboot"
#define CONF_PATH    ESP_MNT "/proloader/proloader.conf"
#define STATE_PATH   ESP_MNT "/proloader/slotstate"

static kvlist_t g_cfg, g_state;
static char g_cmdline[4096];

static void banner(void)
{
    printf("\n");
    printf("  ProperOS %s %s  -  %s\n", PROPEROS_VERSION, PROPEROS_CODENAME, PROPEROS_VENDOR);
    printf("  ProLoader stage 1/2\n");
    printf("  ------------------------------------------------------------\n");
    fflush(stdout);
}

static void read_cmdline(void)
{
    size_t n = 0;
    char *s = read_file("/proc/cmdline", &n);
    if (s) {
        snprintf(g_cmdline, sizeof(g_cmdline), "%s", s);
        free(s);
    } else {
        g_cmdline[0] = '\0';
    }
}

/* 从 /proc/cmdline 取 key 的值；支持 key=value 与裸 key */
static const char *cmdline_get(const char *key, char *out, size_t outlen)
{
    size_t klen = strlen(key);
    const char *p = g_cmdline;
    while (p && *p) {
        while (*p == ' ') p++;
        if (!strncmp(p, key, klen) && (p[klen] == '=' || p[klen] == ' ' || p[klen] == '\0')) {
            if (p[klen] == '=') {
                const char *v = p + klen + 1;
                size_t i = 0;
                while (v[i] && v[i] != ' ' && i + 1 < outlen) { out[i] = v[i]; i++; }
                out[i] = '\0';
            } else {
                snprintf(out, outlen, "1");
            }
            return out;
        }
        while (*p && *p != ' ') p++;
    }
    return NULL;
}

/* 无 udev 环境下通过 sysfs uevent 解析分区设备节点 */
static int dev_by_partname(const char *name, char *out, size_t outlen)
{
    DIR *d = opendir("/sys/class/block");
    if (!d) return -1;
    struct dirent *de;
    int found = -1;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.') continue;
        char path[PATH_MAX_LEN];
        snprintf(path, sizeof(path), "/sys/class/block/%s/uevent", de->d_name);
        char *ue = read_file(path, NULL);
        if (!ue) continue;
        char *line = strstr(ue, "PARTNAME=");
        if (line) {
            line += 9;
            char *e = strchr(line, '\n');
            if (e) *e = '\0';
            if (!strcmp(line, name)) {
                snprintf(out, outlen, "/dev/%s", de->d_name);
                found = 0;
                free(ue);
                break;
            }
        }
        free(ue);
    }
    closedir(d);
    return found;
}

static int dev_by_partuuid(const char *uuid, char *out, size_t outlen)
{
    DIR *d = opendir("/sys/class/block");
    if (!d) return -1;
    struct dirent *de;
    int found = -1;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.') continue;
        char path[PATH_MAX_LEN];
        snprintf(path, sizeof(path), "/sys/class/block/%s/uevent", de->d_name);
        char *ue = read_file(path, NULL);
        if (!ue) continue;
        char *line = strstr(ue, "PARTUUID=");
        if (line) {
            line += 9;
            char *e = strchr(line, '\n');
            if (e) *e = '\0';
            if (!strcasecmp(line, uuid)) {
                snprintf(out, outlen, "/dev/%s", de->d_name);
                found = 0;
                free(ue);
                break;
            }
        }
        free(ue);
    }
    closedir(d);
    return found;
}

static int wait_block_devices(void)
{
    for (int i = 0; i < 100; i++) {
        if (dir_exists("/sys/class/block/sda") || dir_exists("/sys/class/block/vda") ||
            dir_exists("/sys/class/block/mmcblk0") || dir_exists("/sys/class/block/nvme0n1"))
            return 0;
        sleep_ms(100);
    }
    return -1;
}

static int setup_pseudo_fs(void)
{
    mkdir_p("/proc", 0555); mkdir_p("/sys", 0555);
    mkdir_p("/dev", 0755);  mkdir_p("/run", 0755);
    mkdir_p("/tmp", 01777);

    mount_ensure("proc", "/proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL);
    mount_ensure("sysfs", "/sys", "sysfs", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL);
    mount_ensure("devtmpfs", "/dev", "devtmpfs", MS_NOSUID, "mode=0755");
    mount_ensure("tmpfs", "/run", "tmpfs", MS_NOSUID | MS_NODEV, "mode=0755");
    mount_ensure("tmpfs", "/tmp", "tmpfs", MS_NOSUID | MS_NODEV, "mode=1777");

    /* 无 udev 时的基础设备节点 */
    const struct { const char *p; int maj, min; mode_t m; } nodes[] = {
        { "/dev/console", 5, 1, 0600 }, { "/dev/null", 1, 3, 0666 },
        { "/dev/zero", 1, 5, 0666 },    { "/dev/tty", 5, 0, 0666 },
        { "/dev/random", 1, 8, 0666 },  { "/dev/urandom", 1, 9, 0666 },
    };
    for (size_t i = 0; i < ARRAY_LEN(nodes); i++) {
        if (access(nodes[i].p, F_OK) == 0) continue;
        mknod(nodes[i].p, S_IFCHR | nodes[i].m, makedev(nodes[i].maj, nodes[i].min));
    }
    return 0;
}

static const char *slot_other(const char *slot)
{
    return strcmp(slot, "a") ? "a" : "b";
}

/* 校验 /esp/slots/<slot>/SHA256SUMS 中列出的全部文件 */
static int verify_slot(const char *slot, char *reason, size_t reasonlen)
{
    if (!parse_bool(kv_get(&g_cfg, "verify", "1"), 1)) {
        dbg("verify disabled, skipping slot %s", slot);
        return 0;
    }

    char sums[PATH_MAX_LEN];
    snprintf(sums, sizeof(sums), ESP_MNT "/slots/%s/SHA256SUMS", slot);
    size_t len = 0;
    char *data = read_file(sums, &len);
    if (!data) {
        snprintf(reason, reasonlen, "missing SHA256SUMS");
        return -1;
    }

    int rc = 0, checked = 0;
    char *save = NULL;
    for (char *line = strtok_r(data, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char *s = trim(line);
        if (!*s || *s == '#') continue;
        char expect[65], name[256];
        if (sscanf(s, "%64s %255s", expect, name) != 2) continue;
        if (name[0] == '*') memmove(name, name + 1, strlen(name));

        char file[PATH_MAX_LEN];
        snprintf(file, sizeof(file), ESP_MNT "/slots/%s/%s", slot, name);
        char actual[65];
        if (sha256_file(file, actual) < 0) {
            snprintf(reason, reasonlen, "missing file %s", name);
            rc = -1;
            break;
        }
        if (strcasecmp(expect, actual)) {
            snprintf(reason, reasonlen, "%s checksum mismatch", name);
            rc = -1;
            break;
        }
        checked++;
    }
    free(data);
    if (rc == 0 && checked == 0) {
        snprintf(reason, reasonlen, "SHA256SUMS is empty");
        rc = -1;
    }
    if (rc == 0) info("slot %s verified (%d files)", slot, checked);
    return rc;
}

static int slot_usable(const char *slot, char *reason, size_t reasonlen)
{
    char kernel[PATH_MAX_LEN];
    snprintf(kernel, sizeof(kernel), ESP_MNT "/slots/%s/Image", slot);
    if (!file_exists(kernel)) {
        snprintf(reason, reasonlen, "kernel image missing");
        return 0;
    }
    if (verify_slot(slot, reason, reasonlen) < 0) return 0;
    return 1;
}

static void boot_splash(const char *slot)
{
    const char *msg = kv_get(&g_cfg, "splash_message", NULL);
    printf("  boot slot   : %s\n", slot);
    if (msg) printf("  %s\n", msg);
    printf("  mounting system image...\n");
    fflush(stdout);
}

static int load_config(void)
{
    if (kv_load(&g_cfg, CONF_PATH) < 0) {
        warn("%s not found, using built-in defaults", CONF_PATH);
        memset(&g_cfg, 0, sizeof(g_cfg));
        kv_set(&g_cfg, "slots", "a,b");
        kv_set(&g_cfg, "default_slot", "a");
        kv_set(&g_cfg, "max_tries", "3");
        kv_set(&g_cfg, "verify", "1");
    }
    if (kv_load(&g_state, STATE_PATH) < 0) {
        memset(&g_state, 0, sizeof(g_state));
        kv_set(&g_state, "active", kv_get(&g_cfg, "default_slot", "a"));
        kv_set(&g_state, "a_tries", "0");
        kv_set(&g_state, "b_tries", "0");
        kv_set(&g_state, "a_good", "1");
        kv_set(&g_state, "b_good", "0");
    }
    return 0;
}

static int resolve_esp(char *out, size_t outlen)
{
    char buf[256];
    if (cmdline_get("properos.esp", buf, sizeof(buf))) {
        snprintf(out, outlen, "%s", buf);
        return 0;
    }
    if (dev_by_partname("properos-esp", out, outlen) == 0) return 0;
    if (dev_by_partuuid(kv_get(&g_cfg, "esp_partuuid", ""), out, outlen) == 0) return 0;

    /* 兜底：镜像/虚拟机常见布局 */
    const char *cand[] = { "/dev/vda1", "/dev/sda1", "/dev/mmcblk0p1", "/dev/nvme0n1p1" };
    for (size_t i = 0; i < ARRAY_LEN(cand); i++)
        if (file_exists(cand[i])) { snprintf(out, outlen, "%s", cand[i]); return 0; }
    return -1;
}

static int resolve_system(char *out, size_t outlen)
{
    char buf[256];
    if (cmdline_get("properos.system", buf, sizeof(buf))) {
        snprintf(out, outlen, "%s", buf);
        return 0;
    }
    if (dev_by_partname("properos-system", out, outlen) == 0) return 0;
    if (dev_by_partuuid(kv_get(&g_cfg, "system_partuuid", ""), out, outlen) == 0) return 0;

    const char *cand[] = { "/dev/vda2", "/dev/sda2", "/dev/mmcblk0p2", "/dev/nvme0n1p2" };
    for (size_t i = 0; i < ARRAY_LEN(cand); i++)
        if (file_exists(cand[i])) { snprintf(out, outlen, "%s", cand[i]); return 0; }
    return -1;
}

static void emergency(const char *why)
{
    err("entering emergency mode: %s", why);
    printf("\n  ProperOS cannot continue booting: %s\n", why);
    printf("  Rebooting in 10s, press Enter for a recovery shell.\n\n");
    fflush(stdout);

    /* 简单等待：有输入则给 shell */
    struct timeval tv = { 10, 0 };
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0) {
        char *argv[] = { "/bin/sh", NULL };
        execv("/bin/sh", argv);
    }
    sync();
    reboot(RB_AUTOBOOT);
    for (;;) pause();
}

static int cmd_verify(const char *slot)
{
    setup_pseudo_fs();
    read_cmdline();
    load_config();
    char esp[PATH_MAX_LEN];
    if (resolve_esp(esp, sizeof(esp)) < 0) { err("ESP not found"); return 1; }
    if (mount_ensure(esp, ESP_MNT, "vfat", MS_RDONLY, NULL) < 0) return 1;
    char reason[256] = "";
    int ok = slot_usable(slot, reason, sizeof(reason));
    printf("%s: %s\n", slot, ok ? "OK" : reason);
    return ok ? 0 : 1;
}

static int cmd_status(void)
{
    setup_pseudo_fs();
    load_config();
    char esp[PATH_MAX_LEN];
    if (resolve_esp(esp, sizeof(esp)) < 0) { err("ESP not found"); return 1; }
    if (mount_ensure(esp, ESP_MNT, "vfat", MS_RDONLY, NULL) < 0) return 1;

    printf("ProperOS ProLoader %s\n", PROPEROS_VERSION);
    printf("  ESP          : %s\n", esp);
    printf("  active slot  : %s\n", kv_get(&g_state, "active", "?"));
    for (const char *s = "ab"; *s; s++) {
        char key[8], reason[256] = "";
        snprintf(key, sizeof(key), "%c_tries", *s);
        char slotname[2] = { *s, 0 };
        int usable = slot_usable(slotname, reason, sizeof(reason));
        printf("  slot %s       : %s  tries=%d good=%s%s%s\n", slotname,
               usable ? "usable" : "unusable", kv_get_int(&g_state, key, 0),
               kv_get(&g_state, *s == 'a' ? "a_good" : "b_good", "0"),
               usable ? "" : "  reason: ", usable ? "" : reason);
    }
    return 0;
}

static int boot_main(void)
{
    banner();
    setup_pseudo_fs();
    read_cmdline();
    info("kernel cmdline: %s", g_cmdline);

    if (wait_block_devices() < 0) emergency("no block device found");

    char esp[PATH_MAX_LEN];
    if (resolve_esp(esp, sizeof(esp)) < 0) emergency("ESP partition (properos-esp) not found");
    info("ESP: %s", esp);
    if (mount_ensure(esp, ESP_MNT, "vfat", 0, "utf8") < 0) emergency("failed to mount ESP");

    load_config();

    /* 槽选择：命令行 > 活动槽；失败则回滚 */
    char requested[8] = "";
    cmdline_get("properos.slot", requested, sizeof(requested));

    char active[8];
    snprintf(active, sizeof(active), "%s", requested[0] ? requested : kv_get(&g_state, "active", "a"));
    char reason[256] = "";
    int max_tries = kv_get_int(&g_cfg, "max_tries", 3);

    char tries_key[8];
    snprintf(tries_key, sizeof(tries_key), "%s_tries", active);
    int tries = kv_get_int(&g_state, tries_key, 0);

    int use = 0;
    if (tries > max_tries) {
        warn("slot %s failed %d times, rolling back", active, tries);
    } else if (slot_usable(active, reason, sizeof(reason))) {
        use = 1;
    } else {
        warn("slot %s unusable: %s", active, reason);
    }

    if (!use) {
        const char *alt = slot_other(active);
        char alt_reason[256] = "";
        if (slot_usable(alt, alt_reason, sizeof(alt_reason))) {
            info("rolled back to slot %s", alt);
            snprintf(active, sizeof(requested), "%s", alt);
            kv_set(&g_state, "active", active);
            snprintf(tries_key, sizeof(tries_key), "%s_tries", active);
        } else {
            emergency("both slots are unusable");
        }
    }

    boot_splash(active);

    /* 记录本次尝试，供用户态 mark-good 清零 */
    char nbuf[32];
    snprintf(nbuf, sizeof(nbuf), "%d", kv_get_int(&g_state, tries_key, 0) + 1);
    kv_set(&g_state, tries_key, nbuf);
    kv_set(&g_state, "last_slot", active);
    snprintf(nbuf, sizeof(nbuf), "%ld", (long)time(NULL));
    kv_set(&g_state, "last_boot_epoch", nbuf);
    kv_save(&g_state, STATE_PATH);

    char sysdev[PATH_MAX_LEN];
    if (resolve_system(sysdev, sizeof(sysdev)) < 0) emergency("system partition (properos-system) not found");
    info("system partition: %s", sysdev);

    if (mount_ensure(sysdev, SYSROOT, "ext4", MS_RDONLY, NULL) < 0) {
        /* 兼容 btrfs/f2fs 安装 */
        if (mount_ensure(sysdev, SYSROOT, "btrfs", MS_RDONLY, NULL) < 0 &&
            mount_ensure(sysdev, SYSROOT, "f2fs", MS_RDONLY, NULL) < 0)
            emergency("failed to mount system partition");
    }

    char stboot[PATH_MAX_LEN];
    snprintf(stboot, sizeof(stboot), "%s%s", SYSROOT, STBOOT_PATH);
    if (!file_exists(stboot)) emergency("StBoot missing from system");

    /* 移交 StBoot；把槽信息与日志级别通过环境变量传递 */
    setenv("PROPEROS_SLOT", active, 1);
    setenv("PROPEROS_ESP", esp, 1);
    setenv("PROPEROS_SYSDEV", sysdev, 1);
    setenv("PROPEROS_RO", "1", 1);

    char *argv[] = { stboot, "--root", SYSROOT, "--slot", (char *)active, NULL };
    info("handing over to StBoot (slot %s)", active);
    execv(stboot, argv);

    err("failed to start StBoot: %s", strerror(errno));
    emergency("cannot execute StBoot");
    return 1;
}

int main(int argc, char **argv)
{
    plog_open("proloader");

    if (argc > 1 && !strcmp(argv[1], "verify")) return cmd_verify(argc > 2 ? argv[2] : "a");
    if (argc > 1 && !strcmp(argv[1], "status")) return cmd_status();
    if (argc > 1 && !strcmp(argv[1], "--version")) {
        printf("ProLoader %s (%s / %s)\n", PROPEROS_VERSION, PROPEROS_VENDOR, PROPEROS_AUTHOR);
        return 0;
    }
    return boot_main();
}
