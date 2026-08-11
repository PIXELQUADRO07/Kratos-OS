/* test-json.c — Test end-to-end del KratosOS JSON parser */
#include <stdio.h>
#include <string.h>
#include "../../pkg/kratos-json.h"

static int checks_passed = 0;
static int checks_failed = 0;

static void check(const char *label, int ok)
{
    printf("%-55s %s\n", label, ok ? "[PASS]" : "[FAIL]");
    if (ok) checks_passed++; else checks_failed++;
}

int main(void)
{
    /* Test 1: documento realistico stile packages.json */
    const char *doc =
        "{\n"
        "  \"format\": 1,\n"
        "  \"packages\": [\n"
        "    {\n"
        "      \"name\": \"bash\",\n"
        "      \"version\": \"5.2.32\",\n"
        "      \"arch\": \"x86_64\",\n"
        "      \"description\": \"GNU Bourne-Again SHell\",\n"
        "      \"url\": \"https://example.com/bash-5.2.32.kpkg\",\n"
        "      \"sha256\": \"deadbeef\",\n"
        "      \"size\": 1048576,\n"
        "      \"stable\": true,\n"
        "      \"replaces\": null\n"
        "    }\n"
        "  ]\n"
        "}\n";

    json_value_t *root = json_parse(doc, strlen(doc));
    check("Test 1: documento valido -> parse riuscito", root != NULL);

    if (root) {
        const json_value_t *pkgs = json_object_get(root, "packages");
        check("  packages e' un array di 1 elemento",
              json_type(pkgs) == JSON_ARRAY && json_array_size(pkgs) == 1);

        const json_value_t *pkg = json_array_get(pkgs, 0);
        const char *name = json_get_string(json_object_get(pkg, "name"));
        check("  name == \"bash\"", name && strcmp(name, "bash") == 0);

        const char *version = json_get_string(json_object_get(pkg, "version"));
        check("  version == \"5.2.32\"", version && strcmp(version, "5.2.32") == 0);

        long size = 0;
        int size_ok = json_get_long(json_object_get(pkg, "size"), &size) == 0 && size == 1048576;
        check("  size == 1048576", size_ok);

        int stable = 0;
        int stable_ok = json_get_bool(json_object_get(pkg, "stable"), &stable) == 0 && stable == 1;
        check("  stable == true", stable_ok);

        check("  replaces == null", json_is_null(json_object_get(pkg, "replaces")));
        check("  campo inesistente -> NULL", json_object_get(pkg, "nonexistent") == NULL);

        json_free(root);
    }

    /* Test 2: escape sequences (incluso \u con coppia surrogata = emoji) */
    const char *esc_doc = "{\"s\": \"tab:\\t quote:\\\" emoji:\\ud83d\\ude00\"}";
    json_value_t *esc_root = json_parse(esc_doc, strlen(esc_doc));
    if (esc_root) {
        const char *s = json_get_string(json_object_get(esc_root, "s"));
        /* atteso: "tab:\t quote:" emoji:😀" in UTF-8 (F0 9F 98 80) */
        int ok = s && strstr(s, "\t") && strstr(s, "\"") && strstr(s, "\xF0\x9F\x98\x80");
        check("Test 2: escape sequences + surrogate pair UTF-8", ok);
        json_free(esc_root);
    } else {
        check("Test 2: escape sequences + surrogate pair UTF-8", 0);
        printf("  errore: %s\n", json_last_error());
    }

    /* Test 3: numeri negativi e con esponente */
    const char *num_doc = "[-42, 3.14, 6.022e23, -1.5e-10]";
    json_value_t *num_root = json_parse(num_doc, strlen(num_doc));
    if (num_root) {
        double a, b, c, d;
        int ok = json_array_size(num_root) == 4
            && json_get_number(json_array_get(num_root, 0), &a) == 0 && a == -42.0
            && json_get_number(json_array_get(num_root, 1), &b) == 0 && b == 3.14
            && json_get_number(json_array_get(num_root, 2), &c) == 0 && c > 6.021e23 && c < 6.023e23
            && json_get_number(json_array_get(num_root, 3), &d) == 0 && d < 0;
        check("Test 3: numeri negativi/decimali/esponente", ok);
        json_free(num_root);
    } else {
        check("Test 3: numeri negativi/decimali/esponente", 0);
    }

    /* Test 4: JSON malformato deve fallire con errore, non con un crash */
    json_value_t *bad1 = json_parse("{\"a\": }", 7);
    check("Test 4a: valore mancante -> parse fallisce", bad1 == NULL);
    if (bad1) json_free(bad1);

    json_value_t *bad2 = json_parse("{\"a\": 1,}", 9);
    check("Test 4b: virgola finale -> parse fallisce", bad2 == NULL);
    if (bad2) json_free(bad2);

    json_value_t *bad3 = json_parse("{\"a\": 1} garbage", 16);
    check("Test 4c: dati dopo il valore top-level -> parse fallisce", bad3 == NULL);
    if (bad3) json_free(bad3);

    /* Test 5: input malevolo — annidamento profondo non deve mandare in
     * stack overflow il parser (protezione JSON_MAX_DEPTH). */
    char deep[4096];
    size_t n = 0;
    for (int i = 0; i < 2000 && n < sizeof(deep) - 2; i++) deep[n++] = '[';
    for (int i = 0; i < 2000 && n < sizeof(deep) - 2; i++) deep[n++] = ']';
    deep[n] = '\0';
    json_value_t *deep_root = json_parse(deep, n);
    check("Test 5: annidamento profondo (attacco) rifiutato senza crash", deep_root == NULL);
    if (deep_root) json_free(deep_root);

    /* Test 6: oggetti e array vuoti */
    json_value_t *empty_obj = json_parse("{}", 2);
    json_value_t *empty_arr = json_parse("[]", 2);
    check("Test 6: oggetto e array vuoti", empty_obj && empty_arr &&
          json_type(empty_obj) == JSON_OBJECT && json_array_size(empty_arr) == 0);
    json_free(empty_obj);
    json_free(empty_arr);

    printf("\n%d passati, %d falliti\n", checks_passed, checks_failed);
    return checks_failed == 0 ? 0 : 1;
}
