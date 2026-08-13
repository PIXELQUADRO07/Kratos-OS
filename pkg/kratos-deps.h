/* kratos-deps.h — Dependency Parser, Version Constraint Checker & Solver
 *
 * Designed for KratosOS Package Manager (KPM).
 */

#ifndef KRATOS_DEPS_H
#define KRATOS_DEPS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OP_ANY,   /* no version constraint (e.g. "bash") */
    OP_EQ,    /* =  (e.g. "bash=5.2.0") */
    OP_GE,    /* >= (e.g. "glibc>=2.39") */
    OP_LE,    /* <= (e.g. "ncurses<=6.5") */
    OP_GT,    /* >  (e.g. "gcc>14.0") */
    OP_LT,    /* <  (e.g. "openssl<3.0") */
    OP_NE     /* != (e.g. "bash!=5.1") */
} kratos_version_op_t;

typedef struct {
    char name[64];
    kratos_version_op_t op;
    char version[32];
} kratos_dep_rule_t;

#define MAX_PACKAGE_DEPS 32

typedef struct {
    kratos_dep_rule_t rules[MAX_PACKAGE_DEPS];
    size_t count;
} kratos_dep_list_t;

/* Compare two version strings. Returns:
 * < 0 if ver1 < ver2
 *   0 if ver1 == ver2
 * > 0 if ver1 > ver2
 */
int kratos_version_cmp(const char *ver1, const char *ver2);

/* Check if a given installed_ver satisfies the rule (op and required version) */
int kratos_version_satisfies(const char *installed_ver, kratos_version_op_t op, const char *req_ver);

/* Parse a comma/space separated dependency string into a structured list */
int kratos_parse_dependencies(const char *dep_str, kratos_dep_list_t *list);

/* Check if all dependencies in dep_list are satisfied against local installed packages DB */
int kratos_check_dependencies_satisfied(const kratos_dep_list_t *list, const char *db_root,
                                        char *missing_out, size_t missing_size);

/* Check if package conflicts with any currently installed package */
int kratos_check_conflicts(const char *conflicts_str, const char *db_root,
                           char *conflict_out, size_t conflict_size);

#ifdef __cplusplus
}
#endif

#endif /* KRATOS_DEPS_H */
