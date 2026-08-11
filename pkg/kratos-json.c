/* kratos-json.c — KratosOS Minimal JSON Parser (implementation)
 *
 * Recursive-descent parser producing a DOM tree. See kratos-json.h for
 * the public API and design notes.
 */

#define _GNU_SOURCE

#include "kratos-json.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Guards against a maliciously (or accidentally) deeply-nested
 * packages.json blowing the stack via unbounded recursion — this
 * document comes from the network via kratos-fetch, so it must be
 * treated as untrusted input. */
#define JSON_MAX_DEPTH 64

/* ------------------------------------------------------------------ */
/* Tree representation                                                 */
/* ------------------------------------------------------------------ */

typedef struct {
    char *key;
    json_value_t *value;
} json_member_t;

struct json_value {
    json_type_t type;
    union {
        int boolean;
        double number;
        char *string;
        struct { json_value_t **items; size_t count; } array;
        struct { json_member_t *members; size_t count; } object;
    } u;
};

static char g_json_error[256] = "";

const char *json_last_error(void)
{
    return g_json_error;
}

static void set_error(size_t pos, const char *fmt, ...)
{
    char msg[192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    snprintf(g_json_error, sizeof(g_json_error), "%s (byte offset %zu)", msg, pos);
}

/* ------------------------------------------------------------------ */
/* Parser state                                                        */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *buf;
    size_t len;
    size_t pos;
    int depth;
    int failed;
} parser_t;

static int pk_eof(parser_t *p) { return p->pos >= p->len; }
static char pk_peek(parser_t *p) { return pk_eof(p) ? '\0' : p->buf[p->pos]; }
static char pk_advance(parser_t *p) { return pk_eof(p) ? '\0' : p->buf[p->pos++]; }

static void skip_ws(parser_t *p)
{
    while (!pk_eof(p)) {
        char c = p->buf[p->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') p->pos++;
        else break;
    }
}

static int expect(parser_t *p, char c)
{
    if (pk_peek(p) != c) {
        set_error(p->pos, "expected '%c'", c);
        p->failed = 1;
        return -1;
    }
    p->pos++;
    return 0;
}

static json_value_t *alloc_value(json_type_t type)
{
    json_value_t *v = calloc(1, sizeof(json_value_t));
    if (v) v->type = type;
    return v;
}

/* Forward decl: object/array/string free their own members recursively. */
void json_free(json_value_t *v)
{
    if (!v) return;
    switch (v->type) {
        case JSON_STRING:
            free(v->u.string);
            break;
        case JSON_ARRAY:
            for (size_t i = 0; i < v->u.array.count; i++) {
                json_free(v->u.array.items[i]);
            }
            free(v->u.array.items);
            break;
        case JSON_OBJECT:
            for (size_t i = 0; i < v->u.object.count; i++) {
                free(v->u.object.members[i].key);
                json_free(v->u.object.members[i].value);
            }
            free(v->u.object.members);
            break;
        default:
            break;
    }
    free(v);
}

/* ------------------------------------------------------------------ */
/* UTF-8 encoding helper for \uXXXX escapes                            */
/* ------------------------------------------------------------------ */

/* Appends the UTF-8 encoding of a Unicode code point to a growable
 * buffer (see string parser below for the buffer discipline). */
static void append_utf8(char **buf, size_t *len, size_t *cap, unsigned int cp)
{
    unsigned char enc[4];
    int n;

    if (cp <= 0x7F) {
        enc[0] = (unsigned char)cp;
        n = 1;
    } else if (cp <= 0x7FF) {
        enc[0] = (unsigned char)(0xC0 | (cp >> 6));
        enc[1] = (unsigned char)(0x80 | (cp & 0x3F));
        n = 2;
    } else if (cp <= 0xFFFF) {
        enc[0] = (unsigned char)(0xE0 | (cp >> 12));
        enc[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        enc[2] = (unsigned char)(0x80 | (cp & 0x3F));
        n = 3;
    } else {
        enc[0] = (unsigned char)(0xF0 | (cp >> 18));
        enc[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
        enc[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        enc[3] = (unsigned char)(0x80 | (cp & 0x3F));
        n = 4;
    }

    if (*len + (size_t)n + 1 > *cap) {
        size_t new_cap = (*cap == 0) ? 64 : *cap * 2;
        while (new_cap < *len + (size_t)n + 1) new_cap *= 2;
        char *grown = realloc(*buf, new_cap);
        if (!grown) { free(*buf); *buf = NULL; *cap = 0; return; }
        *buf = grown;
        *cap = new_cap;
    }
    memcpy(*buf + *len, enc, (size_t)n);
    *len += (size_t)n;
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int parse_hex4(parser_t *p, unsigned int *out)
{
    unsigned int v = 0;
    for (int i = 0; i < 4; i++) {
        int d = hex_digit(pk_peek(p));
        if (d < 0) {
            set_error(p->pos, "invalid \\u escape (expected 4 hex digits)");
            return -1;
        }
        v = (v << 4) | (unsigned int)d;
        p->pos++;
    }
    *out = v;
    return 0;
}

/* Parses a JSON string starting just after the opening '"'. Returns a
 * newly malloc'd, NUL-terminated UTF-8 C string, or NULL on error
 * (with g_json_error already set). An embedded literal NUL or \u0000
 * is rejected — see the header's "deliberate limitations" note. */
static char *parse_string_raw(parser_t *p)
{
    char *out = NULL;
    size_t out_len = 0, out_cap = 0;

    for (;;) {
        if (pk_eof(p)) {
            set_error(p->pos, "unterminated string");
            free(out);
            return NULL;
        }
        unsigned char c = (unsigned char)pk_advance(p);

        if (c == '"') {
            /* Empty string ("") never goes through append_utf8, so the
             * buffer may still be unallocated here — make sure we
             * always return a valid, NUL-terminated malloc'd string. */
            if (!out) {
                out = malloc(1);
                if (!out) { set_error(p->pos, "out of memory"); return NULL; }
                out_cap = 1;
            }
            out[out_len] = '\0';
            return out;
        }

        if (c == '\0') {
            set_error(p->pos, "embedded NUL byte in string is not supported");
            free(out);
            return NULL;
        }

        if (c < 0x20) {
            set_error(p->pos, "unescaped control character in string");
            free(out);
            return NULL;
        }

        if (c != '\\') {
            append_utf8(&out, &out_len, &out_cap, c);
            if (!out) { set_error(p->pos, "out of memory"); return NULL; }
            continue;
        }

        /* Escape sequence */
        if (pk_eof(p)) {
            set_error(p->pos, "unterminated escape sequence");
            free(out);
            return NULL;
        }
        char esc = pk_advance(p);
        switch (esc) {
            case '"':  append_utf8(&out, &out_len, &out_cap, '"'); break;
            case '\\': append_utf8(&out, &out_len, &out_cap, '\\'); break;
            case '/':  append_utf8(&out, &out_len, &out_cap, '/'); break;
            case 'b':  append_utf8(&out, &out_len, &out_cap, '\b'); break;
            case 'f':  append_utf8(&out, &out_len, &out_cap, '\f'); break;
            case 'n':  append_utf8(&out, &out_len, &out_cap, '\n'); break;
            case 'r':  append_utf8(&out, &out_len, &out_cap, '\r'); break;
            case 't':  append_utf8(&out, &out_len, &out_cap, '\t'); break;
            case 'u': {
                unsigned int cp;
                if (parse_hex4(p, &cp) != 0) { free(out); return NULL; }

                /* High surrogate: must be followed by \uDCxx-\uDFxx low surrogate */
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    if (pk_peek(p) != '\\' ) {
                        set_error(p->pos, "unpaired UTF-16 surrogate");
                        free(out);
                        return NULL;
                    }
                    p->pos++;
                    if (pk_peek(p) != 'u') {
                        set_error(p->pos, "unpaired UTF-16 surrogate");
                        free(out);
                        return NULL;
                    }
                    p->pos++;
                    unsigned int low;
                    if (parse_hex4(p, &low) != 0) { free(out); return NULL; }
                    if (low < 0xDC00 || low > 0xDFFF) {
                        set_error(p->pos, "invalid low surrogate");
                        free(out);
                        return NULL;
                    }
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    set_error(p->pos, "unpaired UTF-16 low surrogate");
                    free(out);
                    return NULL;
                }

                if (cp == 0) {
                    set_error(p->pos, "\\u0000 in string is not supported");
                    free(out);
                    return NULL;
                }
                append_utf8(&out, &out_len, &out_cap, cp);
                break;
            }
            default:
                set_error(p->pos, "invalid escape sequence '\\%c'", esc);
                free(out);
                return NULL;
        }
        if (!out) { set_error(p->pos, "out of memory"); return NULL; }
    }
}

/* ------------------------------------------------------------------ */
/* Number parser                                                       */
/* ------------------------------------------------------------------ */

static json_value_t *parse_number(parser_t *p)
{
    char tok[64];
    size_t n = 0;
    size_t start = p->pos;

    if (pk_peek(p) == '-') tok[n++] = pk_advance(p);

    if (pk_peek(p) == '0') {
        tok[n++] = pk_advance(p);
    } else if (isdigit((unsigned char)pk_peek(p))) {
        while (isdigit((unsigned char)pk_peek(p))) {
            if (n + 1 >= sizeof(tok)) { set_error(start, "number literal too long"); return NULL; }
            tok[n++] = pk_advance(p);
        }
    } else {
        set_error(p->pos, "invalid number");
        return NULL;
    }

    if (pk_peek(p) == '.') {
        if (n + 1 >= sizeof(tok)) { set_error(start, "number literal too long"); return NULL; }
        tok[n++] = pk_advance(p);
        if (!isdigit((unsigned char)pk_peek(p))) {
            set_error(p->pos, "expected digit after decimal point");
            return NULL;
        }
        while (isdigit((unsigned char)pk_peek(p))) {
            if (n + 1 >= sizeof(tok)) { set_error(start, "number literal too long"); return NULL; }
            tok[n++] = pk_advance(p);
        }
    }

    if (pk_peek(p) == 'e' || pk_peek(p) == 'E') {
        if (n + 1 >= sizeof(tok)) { set_error(start, "number literal too long"); return NULL; }
        tok[n++] = pk_advance(p);
        if (pk_peek(p) == '+' || pk_peek(p) == '-') {
            if (n + 1 >= sizeof(tok)) { set_error(start, "number literal too long"); return NULL; }
            tok[n++] = pk_advance(p);
        }
        if (!isdigit((unsigned char)pk_peek(p))) {
            set_error(p->pos, "expected digit in exponent");
            return NULL;
        }
        while (isdigit((unsigned char)pk_peek(p))) {
            if (n + 1 >= sizeof(tok)) { set_error(start, "number literal too long"); return NULL; }
            tok[n++] = pk_advance(p);
        }
    }

    tok[n] = '\0';

    json_value_t *v = alloc_value(JSON_NUMBER);
    if (!v) { set_error(start, "out of memory"); return NULL; }
    v->u.number = strtod(tok, NULL);
    return v;
}

/* ------------------------------------------------------------------ */
/* Value / object / array parsers                                      */
/* ------------------------------------------------------------------ */

static json_value_t *parse_value(parser_t *p);

static json_value_t *parse_string_value(parser_t *p)
{
    char *s = parse_string_raw(p);
    if (!s) return NULL;
    json_value_t *v = alloc_value(JSON_STRING);
    if (!v) { free(s); set_error(p->pos, "out of memory"); return NULL; }
    v->u.string = s;
    return v;
}

static json_value_t *parse_array(parser_t *p)
{
    if (++p->depth > JSON_MAX_DEPTH) {
        set_error(p->pos, "maximum nesting depth (%d) exceeded", JSON_MAX_DEPTH);
        return NULL;
    }

    json_value_t *v = alloc_value(JSON_ARRAY);
    if (!v) { set_error(p->pos, "out of memory"); return NULL; }

    p->pos++; /* consume '[' */
    skip_ws(p);

    if (pk_peek(p) == ']') {
        p->pos++;
        p->depth--;
        return v;
    }

    size_t cap = 0;
    for (;;) {
        skip_ws(p);
        json_value_t *item = parse_value(p);
        if (!item) { json_free(v); p->depth--; return NULL; }

        if (v->u.array.count >= cap) {
            size_t new_cap = cap == 0 ? 8 : cap * 2;
            json_value_t **grown = realloc(v->u.array.items, new_cap * sizeof(*grown));
            if (!grown) {
                set_error(p->pos, "out of memory");
                json_free(item);
                json_free(v);
                p->depth--;
                return NULL;
            }
            v->u.array.items = grown;
            cap = new_cap;
        }
        v->u.array.items[v->u.array.count++] = item;

        skip_ws(p);
        char c = pk_peek(p);
        if (c == ',') { p->pos++; continue; }
        if (c == ']') { p->pos++; break; }
        set_error(p->pos, "expected ',' or ']' in array");
        json_free(v);
        p->depth--;
        return NULL;
    }

    p->depth--;
    return v;
}

static json_value_t *parse_object(parser_t *p)
{
    if (++p->depth > JSON_MAX_DEPTH) {
        set_error(p->pos, "maximum nesting depth (%d) exceeded", JSON_MAX_DEPTH);
        return NULL;
    }

    json_value_t *v = alloc_value(JSON_OBJECT);
    if (!v) { set_error(p->pos, "out of memory"); return NULL; }

    p->pos++; /* consume '{' */
    skip_ws(p);

    if (pk_peek(p) == '}') {
        p->pos++;
        p->depth--;
        return v;
    }

    size_t cap = 0;
    for (;;) {
        skip_ws(p);
        if (pk_peek(p) != '"') {
            set_error(p->pos, "expected string key in object");
            json_free(v);
            p->depth--;
            return NULL;
        }
        p->pos++; /* consume opening quote of key */
        char *key = parse_string_raw(p);
        if (!key) { json_free(v); p->depth--; return NULL; }

        skip_ws(p);
        if (expect(p, ':') != 0) { free(key); json_free(v); p->depth--; return NULL; }
        skip_ws(p);

        json_value_t *val = parse_value(p);
        if (!val) { free(key); json_free(v); p->depth--; return NULL; }

        if (v->u.object.count >= cap) {
            size_t new_cap = cap == 0 ? 8 : cap * 2;
            json_member_t *grown = realloc(v->u.object.members, new_cap * sizeof(*grown));
            if (!grown) {
                set_error(p->pos, "out of memory");
                free(key);
                json_free(val);
                json_free(v);
                p->depth--;
                return NULL;
            }
            v->u.object.members = grown;
            cap = new_cap;
        }
        v->u.object.members[v->u.object.count].key = key;
        v->u.object.members[v->u.object.count].value = val;
        v->u.object.count++;

        skip_ws(p);
        char c = pk_peek(p);
        if (c == ',') { p->pos++; continue; }
        if (c == '}') { p->pos++; break; }
        set_error(p->pos, "expected ',' or '}' in object");
        json_free(v);
        p->depth--;
        return NULL;
    }

    p->depth--;
    return v;
}

static json_value_t *parse_value(parser_t *p)
{
    skip_ws(p);
    if (pk_eof(p)) {
        set_error(p->pos, "unexpected end of input");
        return NULL;
    }

    char c = pk_peek(p);
    if (c == '{') return parse_object(p);
    if (c == '[') return parse_array(p);
    if (c == '"') { p->pos++; return parse_string_value(p); }
    if (c == '-' || isdigit((unsigned char)c)) return parse_number(p);

    if (strncmp(p->buf + p->pos, "true", 4) == 0) {
        p->pos += 4;
        json_value_t *v = alloc_value(JSON_BOOL);
        if (v) v->u.boolean = 1;
        return v;
    }
    if (strncmp(p->buf + p->pos, "false", 5) == 0) {
        p->pos += 5;
        json_value_t *v = alloc_value(JSON_BOOL);
        if (v) v->u.boolean = 0;
        return v;
    }
    if (strncmp(p->buf + p->pos, "null", 4) == 0) {
        p->pos += 4;
        return alloc_value(JSON_NULL);
    }

    set_error(p->pos, "unexpected character '%c'", c);
    return NULL;
}

json_value_t *json_parse(const char *text, size_t len)
{
    g_json_error[0] = '\0';

    parser_t p = { .buf = text, .len = len, .pos = 0, .depth = 0, .failed = 0 };

    json_value_t *root = parse_value(&p);
    if (!root) return NULL;

    skip_ws(&p);
    if (!pk_eof(&p)) {
        set_error(p.pos, "trailing data after top-level value");
        json_free(root);
        return NULL;
    }

    return root;
}

/* ------------------------------------------------------------------ */
/* Accessors                                                            */
/* ------------------------------------------------------------------ */

json_type_t json_type(const json_value_t *v)
{
    return v ? v->type : JSON_NULL;
}

int json_is_null(const json_value_t *v)
{
    return !v || v->type == JSON_NULL;
}

const json_value_t *json_object_get(const json_value_t *obj, const char *key)
{
    if (!obj || obj->type != JSON_OBJECT || !key) return NULL;
    for (size_t i = 0; i < obj->u.object.count; i++) {
        if (strcmp(obj->u.object.members[i].key, key) == 0) {
            return obj->u.object.members[i].value;
        }
    }
    return NULL;
}

size_t json_array_size(const json_value_t *arr)
{
    if (!arr || arr->type != JSON_ARRAY) return 0;
    return arr->u.array.count;
}

const json_value_t *json_array_get(const json_value_t *arr, size_t index)
{
    if (!arr || arr->type != JSON_ARRAY || index >= arr->u.array.count) return NULL;
    return arr->u.array.items[index];
}

const char *json_get_string(const json_value_t *v)
{
    if (!v || v->type != JSON_STRING) return NULL;
    return v->u.string;
}

int json_get_number(const json_value_t *v, double *out)
{
    if (!v || v->type != JSON_NUMBER || !out) return -1;
    *out = v->u.number;
    return 0;
}

int json_get_long(const json_value_t *v, long *out)
{
    if (!v || v->type != JSON_NUMBER || !out) return -1;
    *out = (long)v->u.number;
    return 0;
}

int json_get_bool(const json_value_t *v, int *out)
{
    if (!v || v->type != JSON_BOOL || !out) return -1;
    *out = v->u.boolean;
    return 0;
}
