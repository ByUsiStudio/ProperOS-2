/* ProperOS 25.0.0 Mobile · jspk 原生后端
 *
 * .jspk 的界面仍然是声明式 JSON，但界面里出现的每一条数据都来自这里，
 * 而不是预先写死的样例：目录项来自 opendir/stat，音乐来自真实音频文件与
 * 真实的 PCM 播放进程，包列表来自 ppkg 数据库与已安装 .jspk，电量/网络/
 * 存储来自 /sys 与 statvfs。
 *
 * 两类入口：
 *   nv_source()  数据源：把某个来源解析成条目数组（供 rows/grid 控件渲染）
 *   nv_action()  动作：执行真实操作（进目录、播放、安装、拨号…）并返回提示
 *
 * 开发者：ByUsi Studio · 北啊呢
 */
#ifndef PROPEROS_NATIVE_H
#define PROPEROS_NATIVE_H

#include <stddef.h>

typedef struct {
    char title[128];
    char sub[208];
    char value[48];
    char icon[24];
    char op[24];        /* 点击时执行的动作；空表示不可点 */
    char arg[400];      /* 动作参数（通常是绝对路径或包名） */
} nvi_t;

/* 进程级初始化（幂等）：建好必需目录、清理上次遗留的播放进程 */
void nv_init(void);

/* 解析数据源。src 为 "fs.dir"/"media.tracks"/…，arg 为参数（可为空）。
 * 返回条目数；0 表示空目录/无设备，负数为错误。 */
int nv_source(const char *src, const char *arg, nvi_t *out, int max);

/* 执行动作，成功返回 1。msg 收到给用户看的真实结果。 */
int nv_action(const char *op, const char *arg, char *msg, size_t n);

/* 真实状态变量（battery/wifi/model/kernel/mem/disk/uptime/cpu/temp/audio…）。
 * 返回值指向静态缓冲，2 秒内复用，供界面模板插值。 */
const char *nv_status(const char *key);

/* 是否存在可用的 V4L2 采集节点（相机应用据此显示真实取景或真实缺失） */
int nv_has_camera(void);

#endif /* PROPEROS_NATIVE_H */
