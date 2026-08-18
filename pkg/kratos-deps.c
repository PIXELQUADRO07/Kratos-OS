/* kratos-deps.c — Dependency Parser, Version Constraint Checker & Solver Implementation
 *
 * Designed for KratosOS Package Manager (KPM).
 */

#define _GNU_SOURCE

#include "kratos-deps.h"
#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* Version Comparison Logic                                            */
/* ------------------------------------------------------------------ */

static int is_digit_str(const char *s)
{
    return (s && isdigit((unsigned char)*s));
}

int kratos_version_cmp(const char *ver1, const char *ver2)
{
    if (!ver1 && !ver2) return 0;
    if (!ver1) return -1;
    if (!ver2) return 1;

    const char *p1 = ver1;
    const char *p2 = ver2;

    while (*p1 != '\0' || *p2 != '\0') {
        /* Skip leading non-alphanumeric separators (dots, hyphens, underscores) */
        while (*p1 != '\0' && !isalnum((unsigned char)*p1)) p1++;
        while (*p2 != '\0' && !isalnum((unsigned char)*p2)) p2++;

        if (*p1 == '\0' && *p2 == '\0') return 0;
        if (*p1 == '\0') {
            /* p1 exhausted, p2 has a remaining suffix: a leading digit
             * means p2 is a numeric continuation (newer point release),
             * a leading letter means it's a pre-release tag (older than
             * the bare version p1). */
            return isdigit((unsigned char)*p2) ? -1 : 1;
        }
        if (*p2 == '\0') {
            return isdigit((unsigned char)*p1) ? 1 : -1;
        }

        if (is_digit_str(p1) && is_digit_str(p2)) {
            /* Compare numeric segments */
            unsigned long num1 = strtoul(p1, (char **)&p1, 10);
            unsigned long num2 = strtoul(p2, (char **)&p2, 10);

            if (num1 != num2) {
                return (num1 > num2) ? 1 : -1;
            }
        } else {
            /* Compare alphabetic segments */
            char seg1[64] = {0};
            char seg2[64] = {0};
            size_t len1 = 0, len2 = 0;

            while (*p1 && isalpha((unsigned char)*p1) && len1 < sizeof(seg1) - 1) {
                seg1[len1++] = *p1++;
            }
            while (*p2 && isalpha((unsigned char)*p2) && len2 < sizeof(seg2) - 1) {
                seg2[len2++] = *p2++;
            }

            int diff = strcmp(seg1, seg2);
            if (diff != 0) return (diff > 0) ? 1 : -1;
        }
    }

    return 0;
}

int kratos_version_satisfies(const char *installed_ver, kratos_version_op_t op, const char *req_ver)
{
    if (op == OP_ANY || !req_ver || req_ver[0] == '\0') return 1;
    if (!installed_ver || installed_ver[0] == '\0') return 0;

    int cmp = kratos_version_cmp(installed_ver, req_ver);

    switch (op) {
        case OP_EQ: return (cmp == 0);
        case OP_GE: return (cmp >= 0);
        case OP_LE: return (cmp <= 0);
        case OP_GT: return (cmp > 0);
        case OP_LT: return (cmp < 0);
        case OP_NE: return (cmp != 0);
        case OP_ANY:
        default:
            return 1;
    }
}

/* ------------------------------------------------------------------ */
/* Dependency Parsing                                                  */
/* ------------------------------------------------------------------ */

static void trim_whitespace(char *str)
{
    if (!str) return;
    char *start = str;
    while (*start && isspace((unsigned char)*start)) start++;

    char *end = start + strlen(start);
    while (end > start && isspace((unsigned char)*(end - 1))) end--;
    *end = '\0';

    if (start != str) {
        memmove(str, start, end - start + 1);
    }
}

int kratos_parse_dependencies(const char *dep_str, kratos_dep_list_t *list)
{
    if (!list) return -1;
    memset(list, 0, sizeof(kratos_dep_list_t));

    if (!dep_str || dep_str[0] == '\0') return 0;

    char copy[1024];
    strncpy(copy, dep_str, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = '\0';

    char *saveptr = NULL;
    char *token = strtok_r(copy, ",;", &saveptr);

    while (token != NULL && list->count < MAX_PACKAGE_DEPS) {
        trim_whitespace(token);
        if (token[0] != '\0') {
            kratos_dep_rule_t *rule = &list->rules[list->count];
            memset(rule, 0, sizeof(kratos_dep_rule_t));

            /* Check for operators: >=, <=, !=, ==, =, >, < */
            char *op_pos = NULL;
            kratos_version_op_t op = OP_ANY;
            int op_len = 0;

            if ((op_pos = strstr(token, ">=")) != NULL) {
                op = OP_GE; op_len = 2;
            } else if ((op_pos = strstr(token, "<=")) != NULL) {
                op = OP_LE; op_len = 2;
            } else if ((op_pos = strstr(token, "!=")) != NULL) {
                op = OP_NE; op_len = 2;
            } else if ((op_pos = strstr(token, "==")) != NULL) {
                op = OP_EQ; op_len = 2;
            } else if ((op_pos = strstr(token, "=")) != NULL) {
                op = OP_EQ; op_len = 1;
            } else if ((op_pos = strstr(token, ">")) != NULL) {
                op = OP_GT; op_len = 1;
            } else if ((op_pos = strstr(token, "<")) != NULL) {
                op = OP_LT; op_len = 1;
            }

            if (op_pos) {
                size_t name_len = (size_t)(op_pos - token);
                if (name_len >= sizeof(rule->name)) name_len = sizeof(rule->name) - 1;
                strncpy(rule->name, token, name_len);
                rule->name[name_len] = '\0';
                trim_whitespace(rule->name);

                rule->op = op;
                const char *ver_part = op_pos + op_len;
                while (*ver_part && isspace((unsigned char)*ver_part)) ver_part++;
                strncpy(rule->version, ver_part, sizeof(rule->version) - 1);
                trim_whitespace(rule->version);
            } else {
                rule->op = OP_ANY;
                strncpy(rule->name, token, sizeof(rule->name) - 1);
                trim_whitespace(rule->name);
            }

            if (rule->name[0] != '\0') {
                list->count++;
            }
        }
        token = strtok_r(NULL, ",;", &saveptr);
    }

    return (int)list->count;
}

/* ------------------------------------------------------------------ */
/* Local Package Database Resolution                                   */
/* ------------------------------------------------------------------ */

static int get_installed_pkg_version(const char *pkg_name, const char *db_root, char *ver_out, size_t ver_size)
{
    char meta_path[PATH_MAX];
    snprintf(meta_path, sizeof(meta_path), "%s/var/lib/kratos/db/packages/%s",
             (db_root && db_root[0]) ? db_root : "", pkg_name);

    FILE *f = fopen(meta_path, "r");
    if (!f) return -1;

    char line[256];
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "version=", 8) == 0) {
            char *v = line + 8;
            while (*v && (*v == ' ' || *v == '\t')) v++;
            size_t l = strlen(v);
            while (l > 0 && (v[l-1] == '\n' || v[l-1] == '\r' || v[l-1] == ' ')) v[--l] = '\0';

            strncpy(ver_out, v, ver_size - 1);
            ver_out[ver_size - 1] = '\0';
            found = 1;
            break;
        }
    }
    fclose(f);
    return found ? 0 : -1;
}

int kratos_check_dependencies_satisfied(const kratos_dep_list_t *list, const char *db_root,
                                        char *missing_out, size_t missing_size)
{
    if (!list || list->count == 0) return 1;

    for (size_t i = 0; i < list->count; i++) {
        const kratos_dep_rule_t *rule = &list->rules[i];
        char installed_ver[64] = {0};

        if (get_installed_pkg_version(rule->name, db_root, installed_ver, sizeof(installed_ver)) != 0) {
            /* Special case: glibc and kpm are part of the base system.
             * If they are not in the DB, we treat them as satisfied to allow
             * bootstrapping until the DB is fully populated. */
            if (strcmp(rule->name, "glibc") == 0 || strcmp(rule->name, "kpm") == 0) {
                continue;
            }

            if (missing_out && missing_size > 0) {
                if (rule->op != OP_ANY && rule->version[0] != '\0') {
                    const char *op_sym = "=";
                    if (rule->op == OP_GE) op_sym = ">=";
                    else if (rule->op == OP_LE) op_sym = "<=";
                    else if (rule->op == OP_GT) op_sym = ">";
                    else if (rule->op == OP_LT) op_sym = "<";
                    else if (rule->op == OP_NE) op_sym = "!=";
                    snprintf(missing_out, missing_size, "%s (%s %s) [not installed]", rule->name, op_sym, rule->version);
                } else {
                    snprintf(missing_out, missing_size, "%s [not installed]", rule->name);
                }
            }
            return 0;
        }

        if (rule->op != OP_ANY && !kratos_version_satisfies(installed_ver, rule->op, rule->version)) {
            if (missing_out && missing_size > 0) {
                const char *op_sym = "=";
                if (rule->op == OP_GE) op_sym = ">=";
                else if (rule->op == OP_LE) op_sym = "<=";
                else if (rule->op == OP_GT) op_sym = ">";
                else if (rule->op == OP_LT) op_sym = "<";
                else if (rule->op == OP_NE) op_sym = "!=";
                snprintf(missing_out, missing_size, "%s (%s %s) [installed: %s]", rule->name, op_sym, rule->version, installed_ver);
            }
            return 0;
        }
    }

    return 1;
}

int kratos_check_conflicts(const char *conflicts_str, const char *db_root,
                           char *conflict_out, size_t conflict_size)
{
    if (!conflicts_str || conflicts_str[0] == '\0') return 0;

    kratos_dep_list_t list;
    kratos_parse_dependencies(conflicts_str, &list);

    for (size_t i = 0; i < list.count; i++) {
        const kratos_dep_rule_t *rule = &list.rules[i];
        char installed_ver[64] = {0};

        if (get_installed_pkg_version(rule->name, db_root, installed_ver, sizeof(installed_ver)) == 0) {
            if (rule->op == OP_ANY || kratos_version_satisfies(installed_ver, rule->op, rule->version)) {
                if (conflict_out && conflict_size > 0) {
                    snprintf(conflict_out, conflict_size, "%s (version %s is currently installed)", rule->name, installed_ver);
                }
                return 1; /* Conflict detected */
            }
        }
    }

    return 0; /* No conflicts */
}
