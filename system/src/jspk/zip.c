/* ProperOS 25.0.0 Mobile · ZIP 容器读取实现
 *
 * 开发者：ByUsi Studio · 北啊呢
 */
#include "zip.h"
#include "inflate.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

int zip_open_mem(zip_t *z, uint8_t *data, size_t len)
{
    memset(z, 0, sizeof(*z));
    if (len < 22) return -1;

    /* 从尾部找 EOCD（注释最长 64 KiB） */
    size_t lo = len > 65557 ? len - 65557 : 0;
    ssize_t eocd = -1;
    for (size_t i = len - 22 + 1; i-- > lo;) {
        if (rd32(data + i) == 0x06054b50u) { eocd = (ssize_t)i; break; }
    }
    if (eocd < 0) return -1;

    int n = (int)rd16(data + eocd + 10);
    uint32_t cdoff = rd32(data + (size_t)eocd + 16);
    if (n <= 0 || cdoff >= len) return -1;

    zip_ent_t *ents = calloc((size_t)n, sizeof(zip_ent_t));
    if (!ents) return -1;

    size_t p = cdoff;
    int got = 0;
    for (; got < n; got++) {
        if (p + 46 > len || rd32(data + p) != 0x02014b50u) break;
        zip_ent_t *e = &ents[got];
        e->method = rd16(data + p + 10);
        e->csize = rd32(data + p + 20);
        e->usize = rd32(data + p + 24);
        uint16_t nlen = rd16(data + p + 28);
        uint16_t elen = rd16(data + p + 30);
        uint16_t clen = rd16(data + p + 32);
        e->off = rd32(data + p + 42);
        if (p + 46 + nlen > len) break;
        size_t cp = nlen < sizeof(e->name) - 1 ? nlen : sizeof(e->name) - 1;
        memcpy(e->name, data + p + 46, cp);
        e->name[cp] = '\0';
        p += 46 + (size_t)nlen + elen + clen;
    }
    if (got == 0) { free(ents); return -1; }

    z->data = data;
    z->len = len;
    z->ents = ents;
    z->n = got;
    return 0;
}

int zip_open_file(zip_t *z, const char *path)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size <= 0 || st.st_size > (16 << 20)) {
        close(fd);
        return -1;
    }
    uint8_t *buf = malloc((size_t)st.st_size);
    if (!buf) { close(fd); return -1; }
    ssize_t total = 0;
    while (total < st.st_size) {
        ssize_t r = read(fd, buf + total, (size_t)st.st_size - (size_t)total);
        if (r <= 0) break;
        total += r;
    }
    close(fd);
    if (total != st.st_size) { free(buf); return -1; }
    if (zip_open_mem(z, buf, (size_t)total) != 0) { free(buf); return -1; }
    return 0;
}

void zip_close(zip_t *z)
{
    free(z->ents);
    free(z->data);
    z->ents = NULL;
    z->data = NULL;
    z->n = 0;
}

const zip_ent_t *zip_find(const zip_t *z, const char *name)
{
    for (int i = 0; i < z->n; i++)
        if (!strcmp(z->ents[i].name, name)) return &z->ents[i];
    return NULL;
}

int zip_read(const zip_t *z, const zip_ent_t *e, uint8_t **out, size_t *outlen)
{
    if (!e || e->off + 30 > z->len || rd32(z->data + e->off) != 0x04034b50u) return -1;
    uint16_t nlen = rd16(z->data + e->off + 26);
    uint16_t elen = rd16(z->data + e->off + 28);
    size_t start = (size_t)e->off + 30 + nlen + elen;
    if (start + e->csize > z->len) return -1;

    if (e->method == 0) {
        uint8_t *buf = malloc((size_t)e->usize + 1);
        if (!buf) return -1;
        memcpy(buf, z->data + start, e->usize);
        buf[e->usize] = '\0';
        *out = buf;
        if (outlen) *outlen = e->usize;
        return 0;
    }
    if (e->method == 8) {
        uint8_t *buf = inflate_alloc(z->data + start, e->csize, e->usize, outlen);
        if (!buf) return -1;
        *out = buf;
        return 0;
    }
    return -1;
}
