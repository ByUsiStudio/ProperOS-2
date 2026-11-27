/* ProperOS 25.0.0 Mobile · DEFLATE 解压（RFC 1951）
 *
 * .jspk 是 ZIP 容器，aarch64 交叉工具链没有 zlib 静态库，这里自带一份
 * 只做解压的实现（store + fixed/dynamic Huffman）。
 *
 * 开发者：ByUsi Studio · 北啊呢
 */
#ifndef PROPEROS_INFLATE_H
#define PROPEROS_INFLATE_H

#include <stddef.h>
#include <stdint.h>

/* 解压到调用方给定的缓冲区；成功返回 0，*outlen 为实际长度。 */
int inflate_raw(const uint8_t *in, size_t inlen, uint8_t *out, size_t outcap, size_t *outlen);

/* 解压到新分配的缓冲区（以 0 结尾，方便当文本用）。失败返回 NULL。 */
uint8_t *inflate_alloc(const uint8_t *in, size_t inlen, size_t outcap, size_t *outlen);

#endif
