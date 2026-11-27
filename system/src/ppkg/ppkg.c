/* ProperOS 25.0.0 Mobile · ppkg
 *
 * 角色：系统级包管理器（对标 dpkg）。包格式 .ppkg（ZIP + SHA-256 + 可选签名）。
 * 注意：.jspk 是应用包（properos-app 运行），两者不要混用。
 *   PKGINFO      元数据（name/version/arch/description/depends）
 *   FILES        payload 内全部文件的 SHA-256 清单
 *   payload/     安装文件树
 *   SIGNATURE    可选，对 FILES 的 Ed25519 分离签名（openssl 校验）
 *   scripts/     可选 pre-install / post-install / pre-remove / post-remove
 *
 * 数据库：/var/lib/ppkg/db/<name>
 *
 * 开发者：ByUsi Studio · 北啊呢
 */
#define _GNU_SOURCE
#include "../common/properos.h"
#include "../common/sha256.h"

#define DB_DIR "/var/lib/ppkg/db"

/* -unpack-only：只解包不配置（apt 的 dpkg --unpack 阶段用） */
static int g_unpack_only;

/* 内核与引导链相关包：一律拦截，避免 apt 装出无法启动的系统。
 * 内核镜像由 ProperOS 自己构建、自己管理（properos-bootctl / A/B 槽）。 */
static const char *GUARDED_PREFIX[] = {
    "linux-image", "linux-headers", "linux-modules", "linux-kbuild",
    "linux-source", "linux-support", "linux-perf", "linux-config",
    "grub-efi", "grub-common", "grub2-common", "grub-pc", "grub-coreboot",
    "shim-signed", "systemd-boot", "systemd-boot-efi",
    NULL
};

static int is_guarded(const char *name)
{
    if (!name) return 0;
    for (int i = 0; GUARDED_PREFIX[i]; i++) {
        size_t n = strlen(GUARDED_PREFIX[i]);
        if (!strncmp(name, GUARDED_PREFIX[i], n)) return 1;
    }
    return 0;
}

static void usage(void)
{
    printf("ProperOS ppkg %s — 系统包管理器\n\n", PROPEROS_VERSION);
    printf("用法:\n");
    printf("  ppkg install [--unpack-only] <包.ppkg>   安装/升级\n");
    printf("  ppkg remove <包名>        卸载\n");
    printf("  ppkg list                 已安装包\n");
    printf("  ppkg info <包名>          包信息\n");
    printf("  ppkg verify <包.ppkg>     校验（哈希 + 可选签名）\n");
    printf("  ppkg build <目录> <输出>  构建包\n");
    printf("\n"
           "包分两种格式：\n"
           "  format=payload   原生 ppkg，文件树直接落地\n"
           "  format=deb       由 deb2ppkg 从 Debian .deb 转换，落地走 dpkg\n");
}

static int pkg_extract(const char *pkg, char *outdir, size_t outlen)
{
    char tmp[] = "/tmp/ppkg.XXXXXX";
    char *d = mkdtemp(tmp);
    if (!d) return -1;
    snprintf(outdir, outlen, "%s", d);
    char *argv[] = { "tar", "-xzf", (char *)pkg, "-C", d, NULL };
    if (run_cmd(argv) != 0) { run_shell(xasprintf("rm -rf %s", d)); return -1; }
    return 0;
}

/* 校验 payload 与 FILES 清单 */
static int verify_payload(const char *dir, int verbose)
{
    char fl[PATH_MAX_LEN];
    snprintf(fl, sizeof(fl), "%s/FILES", dir);
    size_t n = 0;
    char *data = read_file(fl, &n);
    if (!data) { err("包内缺少 FILES 清单"); return -1; }

    int bad = 0, count = 0;
    char *save = NULL;
    for (char *l = strtok_r(data, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
        char expect[65], fn[512];
        if (sscanf(trim(l), "%64s %511s", expect, fn) != 2) continue;
        char path[PATH_MAX_LEN], act[65];
        snprintf(path, sizeof(path), "%s/payload/%s", dir, fn);
        if (sha256_file(path, act) < 0) {
            err("缺少文件: %s", fn);
            bad = 1;
            if (!verbose) break;
            continue;
        }
        if (strcasecmp(expect, act)) {
            err("哈希不匹配: %s", fn);
            bad = 1;
            if (!verbose) break;
        }
        count++;
    }
    free(data);
    if (!bad) info("内容校验通过（%d 个文件）", count);
    return bad ? -1 : 0;
}

/* 可选签名校验：openssl dgst -sha256 -verify <pub> -signature SIG FILES */
static int verify_signature(const char *dir)
{
    char sig[PATH_MAX_LEN], fl[PATH_MAX_LEN], pub[] = "/etc/properos/keys/ppkg.pub";
    snprintf(sig, sizeof(sig), "%s/SIGNATURE", dir);
    snprintf(fl, sizeof(fl), "%s/FILES", dir);

    if (!file_exists(sig)) {
        warn("包未签名（发布版应签名）");
        return 0;
    }
    if (!file_exists(pub)) {
        warn("存在签名但系统无公钥 %s，跳过", pub);
        return 0;
    }
    if (!file_exists("/usr/bin/openssl")) {
        warn("未安装 openssl，无法校验签名");
        return 0;
    }
    char *argv[] = { "openssl", "dgst", "-sha256", "-verify", pub, "-signature", sig, fl, NULL };
    if (run_cmd_quiet(argv) != 0) { err("签名校验失败"); return -1; }
    info("签名校验通过");
    return 0;
}

static int db_exists(const char *name)
{
    char p[PATH_MAX_LEN];
    snprintf(p, sizeof(p), DB_DIR "/%s", name);
    return dir_exists(p);
}

static int db_write(const char *name, const char *dir)
{
    char dst[PATH_MAX_LEN];
    snprintf(dst, sizeof(dst), DB_DIR "/%s", name);
    mkdir_p(dst, 0755);

    char src[PATH_MAX_LEN], out[PATH_MAX_LEN];
    snprintf(src, sizeof(src), "%s/PKGINFO", dir);
    snprintf(out, sizeof(out), "%s/PKGINFO", dst);
    copy_file(src, out, 0644);

    snprintf(src, sizeof(src), "%s/FILES", dir);
    snprintf(out, sizeof(out), "%s/FILES", dst);
    copy_file(src, out, 0644);

    char ts[32];
    snprintf(ts, sizeof(ts), "%ld\n", (long)time(NULL));
    snprintf(out, sizeof(out), "%s/INSTALL_DATE", dst);
    write_file(out, ts, strlen(ts));
    return 0;
}

/* deb 兼容包：db 里记录 PKGINFO + dpkg 的文件清单 + 来源 */
static int db_write_deb(const char *name, const kvlist_t *info)
{
    char dst[PATH_MAX_LEN], src[PATH_MAX_LEN], out[PATH_MAX_LEN];
    snprintf(dst, sizeof(dst), DB_DIR "/%s", name);
    mkdir_p(dst, 0755);

    snprintf(out, sizeof(out), "%s/PKGINFO", dst);
    kv_save(info, out);

    /* 文件清单直接复用 dpkg 的 .list，保证两边一致 */
    snprintf(src, sizeof(src), "/var/lib/dpkg/info/%s.list", name);
    snprintf(out, sizeof(out), "%s/FILES", dst);
    size_t n = 0;
    char *data = read_file(src, &n);
    if (data) { write_file(out, data, n); free(data); }

    char buf[PATH_MAX_LEN + 64];
    const char *origin = kv_get(info, "origin", "debian");
    int len = snprintf(buf, sizeof(buf), "%s\n", origin);
    snprintf(out, sizeof(out), "%s/ORIGIN", dst);
    write_file(out, buf, (size_t)len);

    char ts[32];
    len = snprintf(ts, sizeof(ts), "%ld\n", (long)time(NULL));
    snprintf(out, sizeof(out), "%s/INSTALL_DATE", dst);
    write_file(out, ts, (size_t)len);
    return 0;
}

/* 安装由 deb 转换来的包：拦内核、校验 deb、交给 dpkg 落地 */
static int install_deb(const kvlist_t *info, const char *dir)
{
    const char *name = kv_get(info, "name", NULL);
    const char *ver  = kv_get(info, "version", "0");
    if (!name) { err("PKGINFO 缺少 name"); return 1; }

    if (is_guarded(name)) {
        err("拒绝安装 %s：内核与引导链由 ProperOS 管理", name);
        err("内核升级请用 properos-bootctl（A/B 槽），不要经 apt/dpkg");
        return 2;
    }

    char deb[PATH_MAX_LEN];
    snprintf(deb, sizeof(deb), "%s/payload.deb", dir);
    if (!file_exists(deb)) { err("deb 兼容包缺少 payload.deb"); return 1; }

    const char *want = kv_get(info, "deb-sha256", "");
    if (*want) {
        char hex[65];
        if (sha256_file(deb, hex) < 0 || strcasecmp(hex, want)) {
            err("payload.deb 哈希不匹配，包已损坏");
            return 1;
        }
    }

    info("%s %s %s", g_unpack_only ? "解包" : "安装", name, ver);
    if (g_unpack_only) {
        char *argv[] = { "/usr/bin/dpkg", "--unpack", deb, NULL };
        if (run_cmd(argv) != 0) { err("dpkg --unpack 失败"); return 1; }
    } else {
        char *argv[] = { "/usr/bin/dpkg", "-i", deb, NULL };
        int rc = run_cmd(argv);
        if (rc != 0) { err("dpkg 返回 %d（依赖不满足时请先 apt-get -f install）", rc); return 1; }
    }

    db_write_deb(name, info);
    return 0;
}

static int do_install(const char *pkg)
{
    if (geteuid() != 0) { err("安装系统包需要 root"); return 1; }
    if (!file_exists(pkg)) { err("包不存在: %s", pkg); return 1; }

    char dir[PATH_MAX_LEN];
    if (pkg_extract(pkg, dir, sizeof(dir)) < 0) { err("解包失败"); return 1; }

    kvlist_t info;
    char pi[PATH_MAX_LEN];
    snprintf(pi, sizeof(pi), "%s/PKGINFO", dir);
    if (kv_load(&info, pi) < 0) { err("缺少 PKGINFO"); return 1; }

    const char *name = kv_get(&info, "name", NULL);
    const char *ver = kv_get(&info, "version", "0");
    if (!name) { err("PKGINFO 缺少 name"); return 1; }

    /* Debian 转换包：走 dpkg 落地，语义（conffiles/triggers/脚本）完全保留 */
    if (!strcmp(kv_get(&info, "format", "payload"), "deb")) {
        int rc = install_deb(&info, dir);
        kv_free(&info);
        run_shell(xasprintf("rm -rf %s", dir));
        return rc;
    }

    /* 依赖检查（浅层） */
    const char *deps = kv_get(&info, "depends", "");
    if (*deps) {
        char *copy = xstrdup(deps), *save = NULL;
        for (char *d = strtok_r(copy, ",", &save); d; d = strtok_r(NULL, ",", &save)) {
            char dn[128];
            snprintf(dn, sizeof(dn), "%s", trim(d));
            char *sp = strchr(dn, ' ');
            if (sp) *sp = '\0';
            if (!db_exists(dn)) {
                err("缺少依赖: %s", dn);
                free(copy);
                return 1;
            }
        }
        free(copy);
    }

    if (verify_payload(dir, 0) < 0 || verify_signature(dir) < 0) {
        run_shell(xasprintf("rm -rf %s", dir));
        return 1;
    }

    char script[PATH_MAX_LEN];
    snprintf(script, sizeof(script), "%s/scripts/pre-install", dir);
    if (file_exists(script)) { chmod(script, 0755); run_cmd((char *[]){ "/bin/sh", script, NULL }); }

    info("安装 %s %s", name, ver);
    char payload[PATH_MAX_LEN];
    snprintf(payload, sizeof(payload), "%s/payload", dir);
    if (dir_exists(payload)) {
        DIR *d = opendir(payload);
        struct dirent *de;
        while (d && (de = readdir(d))) {
            if (de->d_name[0] == '.') continue;
            char s[PATH_MAX_LEN], t[PATH_MAX_LEN];
            snprintf(s, sizeof(s), "%s/%s", payload, de->d_name);
            snprintf(t, sizeof(t), "/%s", de->d_name);
            if (copy_tree(s, t) < 0) err("复制失败: %s", t);
        }
        if (d) closedir(d);
    }

    db_write(name, dir);

    snprintf(script, sizeof(script), "%s/scripts/post-install", dir);
    if (file_exists(script)) { chmod(script, 0755); run_cmd((char *[]){ "/bin/sh", script, NULL }); }

    printf("已安装 %s %s\n", name, ver);
    kv_free(&info);
    run_shell(xasprintf("rm -rf %s", dir));
    return 0;
}

static int do_remove(const char *name)
{
    if (geteuid() != 0) { err("卸载系统包需要 root"); return 1; }
    char db[PATH_MAX_LEN];
    snprintf(db, sizeof(db), DB_DIR "/%s", name);
    if (!dir_exists(db)) { err("未安装: %s", name); return 1; }

    /* Debian 转换包：卸载同样交回 dpkg，保证依赖与 conffiles 处理正确 */
    char pi[PATH_MAX_LEN];
    snprintf(pi, sizeof(pi), "%s/PKGINFO", db);
    kvlist_t info;
    if (kv_load(&info, pi) == 0) {
        int isdeb = !strcmp(kv_get(&info, "format", "payload"), "deb");
        kv_free(&info);
        if (isdeb) {
            char *argv[] = { "/usr/bin/dpkg", "--purge", (char *)name, NULL };
            if (run_cmd(argv) != 0) { err("dpkg --purge 失败"); return 1; }
            run_shell(xasprintf("rm -rf '%s'", db));
            printf("已卸载 %s（经 dpkg purge）\n", name);
            return 0;
        }
    }

    char fl[PATH_MAX_LEN];
    snprintf(fl, sizeof(fl), "%s/FILES", db);
    size_t n = 0;
    char *data = read_file(fl, &n);
    if (data) {
        char *save = NULL;
        for (char *l = strtok_r(data, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
            char expect[65], fn[512];
            if (sscanf(trim(l), "%64s %511s", expect, fn) != 2) continue;
            char p[PATH_MAX_LEN];
            snprintf(p, sizeof(p), "/%s", fn);
            if (unlink(p) == 0) dbg("删除 %s", p);
        }
        free(data);
    }

    run_shell(xasprintf("rm -rf '%s'", db));
    printf("已卸载 %s\n", name);
    return 0;
}

static int do_list(void)
{
    DIR *d = opendir(DB_DIR);
    if (!d) { printf("尚未安装任何软件包\n"); return 0; }
    printf("%-24s %-12s %-10s %-8s %s\n", "包名", "版本", "架构", "来源", "描述");
    struct dirent *de;
    int n = 0;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.') continue;
        char p[PATH_MAX_LEN];
        snprintf(p, sizeof(p), "%s/%s/PKGINFO", DB_DIR, de->d_name);
        kvlist_t kv;
        if (kv_load(&kv, p) < 0) continue;
        printf("%-24s %-12s %-10s %-8s %s\n", de->d_name,
               kv_get(&kv, "version", "?"), kv_get(&kv, "arch", "?"),
               kv_get(&kv, "format", "payload")[0] == 'd' ? "debian" : "ppkg",
               kv_get(&kv, "description", ""));
        kv_free(&kv);
        n++;
    }
    closedir(d);
    if (!n) printf("（空）\n");
    return 0;
}

static int do_info(const char *name)
{
    char p[PATH_MAX_LEN];
    snprintf(p, sizeof(p), DB_DIR "/%s/PKGINFO", name);
    kvlist_t kv;
    if (kv_load(&kv, p) < 0) { err("未安装: %s", name); return 1; }
    for (size_t i = 0; i < kv.n; i++) printf("%-14s: %s\n", kv.items[i].key, kv.items[i].val);
    kv_free(&kv);

    snprintf(p, sizeof(p), DB_DIR "/%s/FILES", name);
    size_t n = 0;
    char *data = read_file(p, &n);
    if (data) {
        int cnt = 0;
        for (char *c = data; *c; c++) if (*c == '\n') cnt++;
        printf("%-14s: %d\n", "文件数", cnt);
        free(data);
    }
    return 0;
}

static int do_verify(const char *pkg)
{
    char dir[PATH_MAX_LEN];
    if (pkg_extract(pkg, dir, sizeof(dir)) < 0) { err("解包失败"); return 1; }

    int rc = 0, isdeb = 0;
    char pi[PATH_MAX_LEN];
    snprintf(pi, sizeof(pi), "%s/PKGINFO", dir);
    kvlist_t info;
    if (kv_load(&info, pi) == 0) {
        isdeb = !strcmp(kv_get(&info, "format", "payload"), "deb");
        if (isdeb) {
            char deb[PATH_MAX_LEN], hex[65];
            snprintf(deb, sizeof(deb), "%s/payload.deb", dir);
            const char *want = kv_get(&info, "deb-sha256", "");
            rc = -1;
            if (sha256_file(deb, hex) < 0) err("缺少 payload.deb");
            else if (*want && strcasecmp(hex, want)) err("payload.deb 哈希不匹配");
            else { info("deb 内容校验通过（%s）", hex); rc = 0; }
        }
        kv_free(&info);
    }
    if (!isdeb) rc = verify_payload(dir, 1);
    if (rc == 0) rc = verify_signature(dir);
    run_shell(xasprintf("rm -rf %s", dir));
    printf("结果: %s\n", rc == 0 ? "通过" : "失败");
    return rc == 0 ? 0 : 1;
}

static int do_build(const char *srcdir, const char *out)
{
    char pi[PATH_MAX_LEN];
    snprintf(pi, sizeof(pi), "%s/PKGINFO", srcdir);
    if (!file_exists(pi)) { err("缺少 PKGINFO"); return 1; }

    info("生成 FILES 清单");
    run_shell(xasprintf("cd '%s' && find payload -type f -printf '%%P\\n' 2>/dev/null | "
                        "while read f; do sha256sum \"payload/$f\" | sed \"s|payload/||\"; done > FILES",
                        srcdir));
    info("打包 %s", out);
    run_shell(xasprintf("tar -czf '%s' -C '%s' PKGINFO FILES payload scripts 2>/dev/null || "
                        "tar -czf '%s' -C '%s' PKGINFO FILES payload", out, srcdir, out, srcdir));
    char hex[65];
    if (sha256_file(out, hex) == 0) {
        char side[PATH_MAX_LEN];
        snprintf(side, sizeof(side), "%s.sha256", out);
        char line[96];
        snprintf(line, sizeof(line), "%s  %s\n", hex, strrchr(out, '/') ? strrchr(out, '/') + 1 : out);
        write_file(side, line, strlen(line));
        printf("已构建 %s\nSHA-256: %s\n", out, hex);
    }
    return 0;
}

int main(int argc, char **argv)
{
    plog_open("ppkg");
    int ai = 1;
    if (ai < argc && !strcmp(argv[ai], "--unpack-only")) { g_unpack_only = 1; ai++; }
    if (ai >= argc) { usage(); return 1; }
    const char *cmd = argv[ai];
    int nargs = argc - ai - 1;

    if (!strcmp(cmd, "--version")) {
        printf("ppkg %s (%s · %s)\n", PROPEROS_VERSION, PROPEROS_VENDOR, PROPEROS_AUTHOR);
        return 0;
    }
    if (!strcmp(cmd, "install") && nargs >= 1) return do_install(argv[ai + 1]);
    if (!strcmp(cmd, "remove") && nargs >= 1) return do_remove(argv[ai + 1]);
    if (!strcmp(cmd, "list")) return do_list();
    if (!strcmp(cmd, "info") && nargs >= 1) return do_info(argv[ai + 1]);
    if (!strcmp(cmd, "verify") && nargs >= 1) return do_verify(argv[ai + 1]);
    if (!strcmp(cmd, "build") && nargs >= 2) return do_build(argv[ai + 1], argv[ai + 2]);

    usage();
    return 1;
}
