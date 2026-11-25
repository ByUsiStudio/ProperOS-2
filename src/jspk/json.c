/* ProperOS 2.5 Mobile · 极简 JSON 实现
 *
 * 开发者：ByUsi Studio · 北啊呢
 */
#include "json.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *p;
    int err;
} jp_t;

static jval_t *parse_value(jp_t *s, int depth);

static void skip_ws(jp_t *s)
{
    while (*s->p == ' ' || *s->p == '\t' || *s->p == '\r' || *s->p == '\n') s->p++;
}

static jval_t *jnew(jtype_t t)
{
    jval_t *v = calloc(1, sizeof(jval_t));
    if (v) v->t = t;
    return v;
}

static int hex4(const char *p)
{
    int v = 0;
    for (int i = 0; i < 4; i++) {
        char c = p[i];
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return -1;
        v = v * 16 + d;
    }
    return v;
}

static void put_utf8(char **out, unsigned cp)
{
    if (cp < 0x80) {
        *(*out)++ = (char)cp;
    } else if (cp < 0x800) {
        *(*out)++ = (char)(0xC0 | (cp >> 6));
        *(*out)++ = (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        *(*out)++ = (char)(0xE0 | (cp >> 12));
        *(*out)++ = (char)(0x80 | ((cp >> 6) & 0x3F));
        *(*out)++ = (char)(0x80 | (cp & 0x3F));
    } else {
        *(*out)++ = (char)(0xF0 | (cp >> 18));
        *(*out)++ = (char)(0x80 | ((cp >> 12) & 0x3F));
        *(*out)++ = (char)(0x80 | ((cp >> 6) & 0x3F));
        *(*out)++ = (char)(0x80 | (cp & 0x3F));
    }
}

static char *parse_string_raw(jp_t *s)
{
    if (*s->p != '"') { s->err = 1; return NULL; }
    s->p++;
    size_t cap = 32, len = 0;
    char *buf = malloc(cap);
    if (!buf) { s->err = 1; return NULL; }

    while (*s->p && *s->p != '"') {
        unsigned cp;
        if (*s->p == '\\') {
            s->p++;
            char c = *s->p++;
            switch (c) {
            case 'n': cp = '\n'; break;
            case 't': cp = '\t'; break;
            case 'r': cp = '\r'; break;
            case 'b': cp = '\b'; break;
            case 'f': cp = '\f'; break;
            case 'u': {
                int u = hex4(s->p);
                if (u < 0) { free(buf); s->err = 1; return NULL; }
                s->p += 4;
                cp = (unsigned)u;
                if (cp >= 0xD800 && cp <= 0xDBFF && s->p[0] == '\\' && s->p[1] == 'u') {
                    int lo = hex4(s->p + 2);
                    if (lo >= 0xDC00 && lo <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + ((unsigned)lo - 0xDC00);
                        s->p += 6;
                    }
                }
                break;
            }
            default: cp = (unsigned char)c; break;
            }
        } else {
            /* 源文本已是 UTF-8，直接透传字节，不能再按码点编码一次 */
            if (len + 1 >= cap) {
                cap *= 2;
                char *nb = realloc(buf, cap);
                if (!nb) { free(buf); s->err = 1; return NULL; }
                buf = nb;
            }
            buf[len++] = *s->p++;
            continue;
        }
        if (len + 5 >= cap) {
            cap *= 2;
            char *nb = realloc(buf, cap);
            if (!nb) { free(buf); s->err = 1; return NULL; }
            buf = nb;
        }
        char *q = buf + len;
        put_utf8(&q, cp);
        len = (size_t)(q - buf);
    }
    if (*s->p != '"') { free(buf); s->err = 1; return NULL; }
    s->p++;
    buf[len] = '\0';
    return buf;
}

static jval_t *parse_object(jp_t *s, int depth)
{
    jval_t *obj = jnew(JOBJ);
    if (!obj) { s->err = 1; return NULL; }
    s->p++;   /* { */
    skip_ws(s);
    if (*s->p == '}') { s->p++; return obj; }

    jval_t *tail = NULL;
    for (;;) {
        skip_ws(s);
        char *key = parse_string_raw(s);
        if (!key) { json_free(obj); s->err = 1; return NULL; }
        skip_ws(s);
        if (*s->p != ':') { free(key); json_free(obj); s->err = 1; return NULL; }
        s->p++;
        jval_t *v = parse_value(s, depth + 1);
        if (!v) { free(key); json_free(obj); s->err = 1; return NULL; }
        v->key = key;
        if (!tail) obj->child = v; else tail->next = v;
        tail = v;
        skip_ws(s);
        if (*s->p == ',') { s->p++; continue; }
        if (*s->p == '}') { s->p++; break; }
        json_free(obj); s->err = 1; return NULL;
    }
    return obj;
}

static jval_t *parse_array(jp_t *s, int depth)
{
    jval_t *arr = jnew(JARR);
    if (!arr) { s->err = 1; return NULL; }
    s->p++;   /* [ */
    skip_ws(s);
    if (*s->p == ']') { s->p++; return arr; }
    jval_t *tail = NULL;
    for (;;) {
        jval_t *v = parse_value(s, depth + 1);
        if (!v) { json_free(arr); s->err = 1; return NULL; }
        if (!tail) arr->child = v; else tail->next = v;
        tail = v;
        skip_ws(s);
        if (*s->p == ',') { s->p++; continue; }
        if (*s->p == ']') { s->p++; break; }
        json_free(arr); s->err = 1; return NULL;
    }
    return arr;
}

static jval_t *parse_value(jp_t *s, int depth)
{
    if (depth > 64) { s->err = 1; return NULL; }
    skip_ws(s);
    char c = *s->p;
    if (c == '{') return parse_object(s, depth);
    if (c == '[') return parse_array(s, depth);
    if (c == '"') {
        jval_t *v = jnew(JSTR);
        if (!v) { s->err = 1; return NULL; }
        v->str = parse_string_raw(s);
        if (!v->str) { free(v); s->err = 1; return NULL; }
        return v;
    }
    if (c == 't' && !strncmp(s->p, "true", 4)) {
        s->p += 4;
        jval_t *v = jnew(JBOOL); if (v) v->b = 1;
        return v;
    }
    if (c == 'f' && !strncmp(s->p, "false", 5)) {
        s->p += 5;
        jval_t *v = jnew(JBOOL); if (v) v->b = 0;
        return v;
    }
    if (c == 'n' && !strncmp(s->p, "null", 4)) {
        s->p += 4;
        return jnew(JNULL);
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
        char *end = NULL;
        double d = strtod(s->p, &end);
        if (end == s->p) { s->err = 1; return NULL; }
        s->p = end;
        jval_t *v = jnew(JNUM);
        if (v) v->num = d;
        return v;
    }
    s->err = 1;
    return NULL;
}

jval_t *json_parse(const char *text)
{
    if (!text) return NULL;
    jp_t s = { text, 0 };
    jval_t *v = parse_value(&s, 0);
    if (!v || s.err) { json_free(v); return NULL; }
    return v;
}

void json_free(jval_t *v)
{
    while (v) {
        jval_t *next = v->next;
        json_free(v->child);
        free(v->str);
        free(v->key);
        free(v);
        v = next;
    }
}

jval_t *json_get(const jval_t *obj, const char *key)
{
    if (!obj || obj->t != JOBJ || !key) return NULL;
    for (jval_t *c = obj->child; c; c = c->next)
        if (c->key && !strcmp(c->key, key)) return c;
    return NULL;
}

jval_t *json_at(const jval_t *arr, int i)
{
    if (!arr || (arr->t != JARR && arr->t != JOBJ) || i < 0) return NULL;
    for (jval_t *c = arr->child; c; c = c->next)
        if (i-- == 0) return c;
    return NULL;
}

int json_len(const jval_t *v)
{
    int n = 0;
    if (!v) return 0;
    for (jval_t *c = v->child; c; c = c->next) n++;
    return n;
}

const char *json_str(const jval_t *v, const char *def)
{
    if (!v) return def;
    if (v->t == JSTR) return v->str ? v->str : def;
    return def;
}

int json_int(const jval_t *v, int def)
{
    if (!v) return def;
    if (v->t == JNUM) return (int)v->num;
    if (v->t == JBOOL) return v->b;
    if (v->t == JSTR && v->str) return atoi(v->str);
    return def;
}

int json_bool(const jval_t *v, int def)
{
    if (!v) return def;
    if (v->t == JBOOL) return v->b;
    if (v->t == JNUM) return v->num != 0;
    if (v->t == JSTR && v->str)
        return !strcmp(v->str, "1") || !strcmp(v->str, "true") || !strcmp(v->str, "on");
    return def;
}
