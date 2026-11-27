/* ProperOS 25.0.0 Mobile · DEFLATE 解压实现
 *
 * 开发者：ByUsi Studio · 北啊呢
 */
#include "inflate.h"

#include <stdlib.h>
#include <string.h>

#define MAXBITS   15
#define MAXLCODES 286
#define MAXDCODES 30
#define FIXLCODES 288

typedef struct {
    const uint8_t *in;
    size_t inlen, incnt;
    unsigned bitbuf;
    int bitcnt;
    uint8_t *out;
    size_t outlen, outcap;
    int err;
} state_t;

static int getbit(state_t *s)
{
    if (s->bitcnt == 0) {
        if (s->incnt >= s->inlen) { s->err = 1; return 0; }
        s->bitbuf = s->in[s->incnt++];
        s->bitcnt = 8;
    }
    int b = (int)(s->bitbuf & 1u);
    s->bitbuf >>= 1;
    s->bitcnt--;
    return b;
}

static unsigned getbits(state_t *s, int n)
{
    unsigned v = 0;
    for (int i = 0; i < n; i++) v |= (unsigned)getbit(s) << i;
    return v;
}

/* 由码长构造规范 Huffman 表（count 按长度计数，symbol 按码序排列） */
static int construct(short *count, short *symbol, const short *length, int n)
{
    short offs[MAXBITS + 1];
    for (int len = 0; len <= MAXBITS; len++) count[len] = 0;
    for (int i = 0; i < n; i++) count[length[i]]++;
    if (count[0] == n) return 0;
    int left = 1;
    for (int len = 1; len <= MAXBITS; len++) {
        left <<= 1;
        left -= count[len];
        if (left < 0) return -1;
    }
    offs[1] = 0;
    for (int len = 1; len < MAXBITS; len++) offs[len + 1] = (short)(offs[len] + count[len]);
    for (int i = 0; i < n; i++)
        if (length[i]) symbol[offs[length[i]]++] = (short)i;
    return left;
}

static int decode(state_t *s, const short *count, const short *symbol)
{
    int len, code, first, cnt, index;
    code = first = index = 0;
    for (len = 1; len <= MAXBITS; len++) {
        code |= getbit(s);
        if (s->err) return -1;
        cnt = count[len];
        if (code - cnt < first) return symbol[index + (code - first)];
        index += cnt;
        first += cnt;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

static const short lenbase[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51,
    59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
static const short lenext[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};
static const short distbase[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513,
    769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
};
static const short distext[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

static int codes(state_t *s, const short *lencnt, const short *lensym,
                 const short *distcnt, const short *distsym)
{
    int symbol;
    do {
        symbol = decode(s, lencnt, lensym);
        if (symbol < 0) { s->err = 1; return -1; }
        if (symbol < 256) {
            if (s->outlen >= s->outcap) { s->err = 1; return -1; }
            s->out[s->outlen++] = (uint8_t)symbol;
        } else if (symbol > 256) {
            symbol -= 257;
            if (symbol >= 29) { s->err = 1; return -1; }
            int len = lenbase[symbol] + (int)getbits(s, lenext[symbol]);
            int dsym = decode(s, distcnt, distsym);
            if (dsym < 0 || dsym >= 30) { s->err = 1; return -1; }
            int dist = distbase[dsym] + (int)getbits(s, distext[dsym]);
            if (dist <= 0 || (size_t)dist > s->outlen) { s->err = 1; return -1; }
            if (s->outlen + (size_t)len > s->outcap) { s->err = 1; return -1; }
            for (int i = 0; i < len; i++, s->outlen++)
                s->out[s->outlen] = s->out[s->outlen - (size_t)dist];
        }
    } while (symbol != 256);
    return 0;
}

static int stored(state_t *s)
{
    s->bitbuf = 0;
    s->bitcnt = 0;
    if (s->incnt + 4 > s->inlen) { s->err = 1; return -1; }
    unsigned len = (unsigned)s->in[s->incnt] | ((unsigned)s->in[s->incnt + 1] << 8);
    unsigned nlen = (unsigned)s->in[s->incnt + 2] | ((unsigned)s->in[s->incnt + 3] << 8);
    s->incnt += 4;
    if ((len ^ 0xFFFFu) != nlen) { s->err = 1; return -1; }
    if (s->incnt + len > s->inlen || s->outlen + len > s->outcap) { s->err = 1; return -1; }
    memcpy(s->out + s->outlen, s->in + s->incnt, len);
    s->outlen += len;
    s->incnt += len;
    return 0;
}

static void build_fixed(short *lencnt, short *lensym, short *distcnt, short *distsym)
{
    short lengths[FIXLCODES];
    int i = 0;
    for (; i < 144; i++) lengths[i] = 8;
    for (; i < 256; i++) lengths[i] = 9;
    for (; i < 280; i++) lengths[i] = 7;
    for (; i < 288; i++) lengths[i] = 8;
    construct(lencnt, lensym, lengths, FIXLCODES);
    for (i = 0; i < 30; i++) lengths[i] = 5;
    construct(distcnt, distsym, lengths, 30);
}

static int fixed_block(state_t *s)
{
    short lencnt[MAXBITS + 1], lensym[FIXLCODES];
    short distcnt[MAXBITS + 1], distsym[MAXDCODES];
    build_fixed(lencnt, lensym, distcnt, distsym);
    return codes(s, lencnt, lensym, distcnt, distsym);
}

static int dynamic_block(state_t *s)
{
    static const short order[19] = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
    };
    short lencnt[MAXBITS + 1], lensym[MAXLCODES];
    short distcnt[MAXBITS + 1], distsym[MAXDCODES];
    short lengths[MAXLCODES + MAXDCODES];

    int nlen = (int)getbits(s, 5) + 257;
    int ndist = (int)getbits(s, 5) + 1;
    int ncode = (int)getbits(s, 4) + 4;
    if (nlen > MAXLCODES || ndist > MAXDCODES) { s->err = 1; return -1; }

    for (int i = 0; i < ncode; i++) lengths[order[i]] = (short)getbits(s, 3);
    for (int i = ncode; i < 19; i++) lengths[order[i]] = 0;
    if (construct(lencnt, lensym, lengths, 19) < 0) { s->err = 1; return -1; }

    int n = 0;
    while (n < nlen + ndist) {
        int sym = decode(s, lencnt, lensym);
        if (sym < 0) { s->err = 1; return -1; }
        if (sym < 16) {
            lengths[n++] = (short)sym;
        } else {
            int len = 0, rep;
            if (sym == 16) {
                if (n == 0) { s->err = 1; return -1; }
                len = lengths[n - 1];
                rep = 3 + (int)getbits(s, 2);
            } else if (sym == 17) {
                rep = 3 + (int)getbits(s, 3);
            } else {
                rep = 11 + (int)getbits(s, 7);
            }
            if (n + rep > nlen + ndist) { s->err = 1; return -1; }
            while (rep--) lengths[n++] = (short)len;
        }
    }

    if (lengths[256] == 0) { s->err = 1; return -1; }
    if (construct(lencnt, lensym, lengths, nlen) < 0) { s->err = 1; return -1; }
    if (construct(distcnt, distsym, lengths + nlen, ndist) < 0) { s->err = 1; return -1; }
    return codes(s, lencnt, lensym, distcnt, distsym);
}

int inflate_raw(const uint8_t *in, size_t inlen, uint8_t *out, size_t outcap, size_t *outlen)
{
    state_t s;
    memset(&s, 0, sizeof(s));
    s.in = in; s.inlen = inlen;
    s.out = out; s.outcap = outcap;

    int last = 0;
    while (!last && !s.err) {
        last = getbit(&s);
        int type = (int)getbits(&s, 2);
        if (type == 0) { if (stored(&s)) break; }
        else if (type == 1) { if (fixed_block(&s)) break; }
        else if (type == 2) { if (dynamic_block(&s)) break; }
        else { s.err = 1; break; }
    }
    if (outlen) *outlen = s.outlen;
    return s.err ? -1 : 0;
}

uint8_t *inflate_alloc(const uint8_t *in, size_t inlen, size_t outcap, size_t *outlen)
{
    uint8_t *buf = malloc(outcap + 1);
    if (!buf) return NULL;
    size_t got = 0;
    if (inflate_raw(in, inlen, buf, outcap, &got) != 0) { free(buf); return NULL; }
    buf[got] = '\0';
    if (outlen) *outlen = got;
    return buf;
}
