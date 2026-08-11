/* kratos-json.h — KratosOS Minimal JSON Parser (zero dependencies)
 *
 * Scritto a mano, coerente con lo stile "zero dipendenze" del progetto
 * (vedi kratos-crypt.h/.c). Pensato per parsare il futuro packages.json
 * scaricato via kratos-fetch per kratos update/search/install.
 *
 * Copre l'intera grammatica JSON (RFC 8259): oggetti, array, stringhe
 * (con escape \" \\ \/ \b \f \n \r \t \uXXXX incluse le coppie
 * surrogate), numeri (interi/decimali/esponente), true/false/null.
 *
 * Limitazioni deliberate, accettabili per un package index:
 *   - Le stringhe non possono contenere un NUL letterale o \u0000
 *     (vengono trattate come C string NUL-terminated).
 *   - I numeri sono rappresentati come double (precisione esatta fino
 *     a 2^53, più che sufficiente per size/version/timestamp).
 *   - La profondità di annidamento è limitata (vedi JSON_MAX_DEPTH in
 *     kratos-json.c) per non far esplodere lo stack su un
 *     packages.json malevolo scaricato da rete.
 *
 * Uso tipico:
 *   char *text = ...;               // contenuto di packages.json
 *   json_value_t *root = json_parse(text, strlen(text));
 *   if (!root) {
 *       fprintf(stderr, "parse error: %s\n", json_last_error());
 *       return -1;
 *   }
 *   const json_value_t *pkgs = json_object_get(root, "packages");
 *   for (size_t i = 0; i < json_array_size(pkgs); i++) {
 *       const json_value_t *p = json_array_get(pkgs, i);
 *       const char *name = json_get_string(json_object_get(p, "name"));
 *       ...
 *   }
 *   json_free(root);
 */

#ifndef KRATOS_JSON_H
#define KRATOS_JSON_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    JSON_NULL = 0,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} json_type_t;

typedef struct json_value json_value_t;

/* Parses `len` bytes of JSON text (does not need to be NUL-terminated).
 * Returns a heap-allocated tree owned by the caller — free it with
 * json_free(). Returns NULL on any parse error; call json_last_error()
 * for a human-readable reason. */
json_value_t *json_parse(const char *text, size_t len);

/* Frees a tree returned by json_parse(), recursively. Safe to call
 * with NULL. */
void json_free(json_value_t *v);

/* Human-readable reason for the most recent json_parse() failure on
 * this thread's call stack. Valid until the next json_parse() call.
 * KratosOS's CLI tools are single-threaded, so a static buffer is
 * sufficient here (same tradeoff as kratos_crypt()). */
const char *json_last_error(void);

/* ------------------------------------------------------------------ */
/* Accessors — all NULL-safe: passing NULL or a mismatched type       */
/* returns a "not found" value rather than crashing, since walking a  */
/* possibly-unexpected server-provided document is the normal case.   */
/* ------------------------------------------------------------------ */

json_type_t json_type(const json_value_t *v);
int json_is_null(const json_value_t *v);

/* Object member lookup. Linear search — fine for the small per-package
 * metadata objects and modest package counts a packages.json holds. */
const json_value_t *json_object_get(const json_value_t *obj, const char *key);

size_t json_array_size(const json_value_t *arr);
const json_value_t *json_array_get(const json_value_t *arr, size_t index);

/* Returns the string value, or NULL if v is NULL or not a JSON string.
 * Owned by the tree — do not free, and don't use after json_free(). */
const char *json_get_string(const json_value_t *v);

/* Numeric/bool accessors return 0 on success, -1 if v is NULL or the
 * wrong type (out is left untouched on failure). */
int json_get_number(const json_value_t *v, double *out);
int json_get_long(const json_value_t *v, long *out);
int json_get_bool(const json_value_t *v, int *out);

#ifdef __cplusplus
}
#endif

#endif /* KRATOS_JSON_H */
