/* ProperOS 25.0.0 Mobile · 极简 JSON（jspk 清单与界面描述）
 *
 * 只做解析与取值，不做序列化。支持对象/数组/字符串/数字/布尔/null，
 * 字符串支持 \\uXXXX 与代理对。
 *
 * 开发者：ByUsi Studio · 北啊呢
 */
#ifndef PROPEROS_JSON_H
#define PROPEROS_JSON_H

typedef enum { JNULL = 0, JBOOL, JNUM, JSTR, JARR, JOBJ } jtype_t;

typedef struct jval {
    jtype_t t;
    double num;
    int b;
    char *str;              /* JSTR 的值 */
    char *key;              /* 作为 JOBJ 成员时的键名 */
    struct jval *child;     /* 首个子节点 */
    struct jval *next;      /* 下一个兄弟 */
} jval_t;

jval_t *json_parse(const char *text);
void    json_free(jval_t *v);

jval_t *json_get(const jval_t *obj, const char *key);
jval_t *json_at(const jval_t *arr, int i);
int     json_len(const jval_t *v);

const char *json_str(const jval_t *v, const char *def);
int         json_int(const jval_t *v, int def);
int         json_bool(const jval_t *v, int def);

#endif
