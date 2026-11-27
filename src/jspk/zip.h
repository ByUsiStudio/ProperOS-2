/* ProperOS 25.0.0 Mobile · ZIP 容器读取（.jspk 包格式）
 *
 * 只读、只支持单个中央目录、不支持 ZIP64（应用包远小于 4 GB）。
 * 压缩方法支持 0（store）与 8（deflate）。
 *
 * 开发者：ByUsi Studio · 北啊呢
 */
#ifndef PROPEROS_ZIP_H
#define PROPEROS_ZIP_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    char name[192];
    uint16_t method;
    uint32_t csize, usize, off;
} zip_ent_t;

typedef struct {
    uint8_t *data;
    size_t len;
    zip_ent_t *ents;
    int n;
} zip_t;

int  zip_open_mem(zip_t *z, uint8_t *data, size_t len);
int  zip_open_file(zip_t *z, const char *path);
void zip_close(zip_t *z);
const zip_ent_t *zip_find(const zip_t *z, const char *name);
/* 读取并解压条目，成功返回 0 并写入 *out（malloc，含结尾 0）。 */
int  zip_read(const zip_t *z, const zip_ent_t *e, uint8_t **out, size_t *outlen);

#endif
