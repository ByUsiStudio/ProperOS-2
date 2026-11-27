/* SHA-256（FIPS 180-4），用于镜像与软件包完整性校验 */
#ifndef PROPEROS_SHA256_H
#define PROPEROS_SHA256_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t  buf[64];
    size_t   buflen;
} sha256_ctx;

void sha256_init(sha256_ctx *c);
void sha256_update(sha256_ctx *c, const void *data, size_t len);
void sha256_final(sha256_ctx *c, uint8_t out[32]);
void sha256_hex(const uint8_t d[32], char out[65]);
int  sha256_file(const char *path, char hex[65]);

#endif
