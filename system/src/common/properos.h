/* ProperOS 25.0.0 Mobile · 公共运行时工具
 * 开发者：ByUsi Studio · 北啊呢
 */
#ifndef PROPEROS_COMMON_H
#define PROPEROS_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <time.h>
#include <dirent.h>
#include <signal.h>

#define PROPEROS_VERSION "25.0.0"
#define PROPEROS_CODENAME "Mobile"
#define PROPEROS_VENDOR "ByUsi Studio"
#define PROPEROS_AUTHOR "北啊呢"

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define PATH_MAX_LEN 512

/* ---------- 日志 ---------- */
void plog_open(const char *ident);
void plog(int level, const char *fmt, ...);
#define LOG_ERR  0
#define LOG_WARN 1
#define LOG_INFO 2
#define LOG_DBG  3
extern int plog_level;
#define err(...)  plog(LOG_ERR,  __VA_ARGS__)
#define warn(...) plog(LOG_WARN, __VA_ARGS__)
#define info(...) plog(LOG_INFO, __VA_ARGS__)
#define dbg(...)  do { if (plog_level >= LOG_DBG) plog(LOG_DBG, __VA_ARGS__); } while (0)

/* ---------- 内存 ---------- */
void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t sz);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
char *xasprintf(const char *fmt, ...);

/* ---------- 文件 ---------- */
char *read_file(const char *path, size_t *len_out);   /* 失败返回 NULL */
int   write_file(const char *path, const char *data, size_t len);
int   write_file_atomic(const char *path, const char *data, size_t len);
int   file_exists(const char *path);
int   dir_exists(const char *path);
int   mkdir_p(const char *path, mode_t mode);
char *trim(char *s);
int   copy_file(const char *src, const char *dst, mode_t mode);
int   copy_tree(const char *src, const char *dst);

/* ---------- 配置（key=value） ---------- */
typedef struct {
    char *key;
    char *val;
} kv_t;

typedef struct {
    kv_t  *items;
    size_t n;
} kvlist_t;

int         kv_load(kvlist_t *l, const char *path);
const char *kv_get(const kvlist_t *l, const char *key, const char *def);
int         kv_get_int(const kvlist_t *l, const char *key, int def);
void        kv_free(kvlist_t *l);
int         kv_save(const kvlist_t *l, const char *path);
int         kv_set(kvlist_t *l, const char *key, const char *val);

/* ---------- 命令执行 ---------- */
int run_cmd(char *const argv[]);                  /* 返回退出码，-1 表示启动失败 */
int run_cmd_quiet(char *const argv[]);
int run_shell(const char *cmd);

/* ---------- 挂载辅助 ---------- */
int mount_ensure(const char *src, const char *target, const char *fstype,
                 unsigned long flags, const char *data);
int umount_all(const char *target);
int is_mountpoint(const char *path);
int wait_for_path(const char *path, int timeout_ms);

/* ---------- 杂项 ---------- */
const char *human_size(uint64_t bytes, char *buf, size_t buflen);
int   parse_bool(const char *s, int def);
void  sleep_ms(int ms);

#endif /* PROPEROS_COMMON_H */
