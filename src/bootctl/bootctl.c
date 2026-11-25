/* ProperOS 2.5 Mobile · properos-bootctl
 * A/B 槽管理：查看状态、切换活动槽、标记启动成功、回滚
 * 开发者：ByUsi Studio · 北啊呢
 */
#define _GNU_SOURCE
#include "../common/properos.h"

#define ESP "/boot/efi"
#define STATE ESP "/proloader/slotstate"
#define CONF  ESP "/proloader/proloader.conf"

static void usage(void)
{
    printf("usage: properos-bootctl <command>\n\n");
    printf("  status              show A/B slot state\n");
    printf("  set-active <a|b>    select the slot for the next boot\n");
    printf("  mark-good           mark current slot as good (clear failure count)\n");
    printf("  rollback            roll back to the other slot\n");
}

static int ensure_esp(void)
{
    if (dir_exists(ESP "/proloader")) return 0;

    /* udev 已建好链接时优先用 PARTLABEL；没有就静默跳过，
     * 否则 mount 会把 "Can't lookup blockdev" 打到内核日志里，很吓人。 */
    const char *cand[] = {
        "/dev/disk/by-partlabel/properos-esp",
        "/dev/vda1", "/dev/sda1", "/dev/mmcblk0p1",
        "/dev/nvme0n1p1", "/dev/vdb1",
    };
    for (size_t i = 0; i < ARRAY_LEN(cand); i++) {
        if (access(cand[i], F_OK) != 0) continue;
        if (mount(cand[i], ESP, "vfat", 0, "utf8") == 0) return 0;
    }
    return -1;
}

static int load(kvlist_t *st, kvlist_t *cfg)
{
    if (ensure_esp() < 0) { err("cannot mount ESP at %s", ESP); return -1; }
    if (kv_load(st, STATE) < 0) {
        memset(st, 0, sizeof(*st));
        kv_set(st, "active", "a");
        kv_set(st, "a_tries", "0");
        kv_set(st, "b_tries", "0");
    }
    if (kv_load(cfg, CONF) < 0) memset(cfg, 0, sizeof(*cfg));
    return 0;
}

static int cmd_status(void)
{
    kvlist_t st, cfg;
    if (load(&st, &cfg) < 0) return 1;

    const char *active = kv_get(&st, "active", "a");
    printf("ProperOS %s - ProLoader slot state\n\n", PROPEROS_VERSION);
    printf("  active slot : %s\n", active);
    printf("  last boot   : %s\n", kv_get(&st, "last_slot", "-"));
    printf("  max tries   : %s\n", kv_get(&cfg, "max_tries", "3"));
    printf("\n  %-4s %-8s %-8s %-8s\n", "slot", "tries", "good", "state");
    for (const char *s = "ab"; *s; s++) {
        char tk[8], gk[8];
        snprintf(tk, sizeof(tk), "%c_tries", *s);
        snprintf(gk, sizeof(gk), "%c_good", *s);
        printf("  %-4c %-8d %-8s %s\n", *s, kv_get_int(&st, tk, 0),
               kv_get(&st, gk, "0"), strcmp(active, (char[]){ *s, 0 }) ? "" : "<- active");
    }
    kv_free(&st); kv_free(&cfg);
    return 0;
}

static int cmd_set(const char *slot)
{
    if (!slot || (strcmp(slot, "a") && strcmp(slot, "b"))) { err("slot must be a or b"); return 1; }
    kvlist_t st, cfg;
    if (load(&st, &cfg) < 0) return 1;
    kv_set(&st, "active", slot);
    char key[8];
    snprintf(key, sizeof(key), "%s_tries", slot);
    kv_set(&st, key, "0");
    int rc = kv_save(&st, STATE);
    printf(rc == 0 ? "active slot switched to %s\n" : "write failed\n", slot);
    kv_free(&st); kv_free(&cfg);
    return rc == 0 ? 0 : 1;
}

static int cmd_mark_good(void)
{
    kvlist_t st, cfg;
    if (load(&st, &cfg) < 0) return 1;
    const char *active = kv_get(&st, "active", "a");
    char key[8];
    snprintf(key, sizeof(key), "%s_tries", active);
    kv_set(&st, key, "0");
    snprintf(key, sizeof(key), "%s_good", active);
    kv_set(&st, key, "1");
    kv_set(&st, "last_good_epoch", xasprintf("%ld", (long)time(NULL)));
    int rc = kv_save(&st, STATE);
    printf(rc == 0 ? "slot %s marked good\n" : "write failed\n", active);
    kv_free(&st); kv_free(&cfg);
    return rc == 0 ? 0 : 1;
}

static int cmd_rollback(void)
{
    kvlist_t st, cfg;
    if (load(&st, &cfg) < 0) return 1;
    const char *active = kv_get(&st, "active", "a");
    const char *other = strcmp(active, "a") ? "a" : "b";
    kv_set(&st, "active", other);
    int rc = kv_save(&st, STATE);
    printf(rc == 0 ? "rolled back to slot %s\n" : "write failed\n", other);
    kv_free(&st); kv_free(&cfg);
    return rc == 0 ? 0 : 1;
}

int main(int argc, char **argv)
{
    plog_open("bootctl");
    if (argc < 2 || !strcmp(argv[1], "--help")) { usage(); return argc < 2; }
    if (!strcmp(argv[1], "status")) return cmd_status();
    if (!strcmp(argv[1], "set-active") && argc >= 3) return cmd_set(argv[2]);
    if (!strcmp(argv[1], "mark-good")) return cmd_mark_good();
    if (!strcmp(argv[1], "rollback")) return cmd_rollback();
    if (!strcmp(argv[1], "--version")) {
        printf("properos-bootctl %s (%s)\n", PROPEROS_VERSION, PROPEROS_VENDOR);
        return 0;
    }
    usage();
    return 1;
}
