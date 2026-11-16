/* json.c — thin helpers over jsmn (no malloc in the parser itself). */
#include "common.h"
#include "jsmn.h"

#define JSON_MAX_BYTES  (16 * 1024 * 1024)
#define JSON_MAX_TOKENS 65536

int json_parse_file(const char *path, char **js, jsmntok_t **toks, int *ntok) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    struct stat st;
    if (fstat(fd, &st) < 0 || st.st_size <= 0 || st.st_size > JSON_MAX_BYTES) { close(fd); return -1; }
    char *buf = malloc((size_t)st.st_size + 1);
    if (!buf) { close(fd); return -1; }
    ssize_t r = read(fd, buf, (size_t)st.st_size);
    close(fd);
    if (r != st.st_size) { free(buf); return -1; }
    buf[r] = '\0';

    jsmntok_t *t = malloc(sizeof(jsmntok_t) * JSON_MAX_TOKENS);
    if (!t) { free(buf); return -1; }
    jsmn_parser p;
    jsmn_init(&p);
    int n = jsmn_parse(&p, buf, (size_t)r, t, JSON_MAX_TOKENS);
    if (n < 1) { free(buf); free(t); return -1; }
    *js = buf;
    *toks = t;
    *ntok = n;
    return 0;
}

void json_free(char *js, jsmntok_t *toks) {
    free(js);
    free(toks);
}

int json_tok_eq(const char *js, const jsmntok_t *t, const char *s) {
    size_t n = strlen(s);
    return t->type == JSMN_STRING && (size_t)(t->end - t->start) == n &&
           memcmp(js + t->start, s, n) == 0;
}

void json_tok_copy(const char *js, const jsmntok_t *t, char *out, size_t n) {
    size_t len = (size_t)(t->end - t->start);
    if (len >= n) len = n - 1;
    memcpy(out, js + t->start, len);
    out[len] = '\0';
}

int json_tok_skip(const jsmntok_t *t, int i) {
    int end = i + 1;
    if (t[i].type == JSMN_OBJECT) {
        for (int k = 0; k < t[i].size; k++) { end = json_tok_skip(t, end); end = json_tok_skip(t, end); }
    } else if (t[i].type == JSMN_ARRAY) {
        for (int k = 0; k < t[i].size; k++) end = json_tok_skip(t, end);
    }
    return end;
}

int json_obj_get(const char *js, const jsmntok_t *t, int obj, const char *key) {
    if (obj < 0 || t[obj].type != JSMN_OBJECT) return -1;
    int i = obj + 1;
    for (int k = 0; k < t[obj].size; k++) {
        if (json_tok_eq(js, &t[i], key)) return i + 1;
        i = json_tok_skip(t, i + 1);
    }
    return -1;
}

int json_arr_nth(const jsmntok_t *t, int arr, int n) {
    if (arr < 0 || t[arr].type != JSMN_ARRAY || n >= t[arr].size) return -1;
    int i = arr + 1;
    for (int k = 0; k < n; k++) i = json_tok_skip(t, i);
    return i;
}
