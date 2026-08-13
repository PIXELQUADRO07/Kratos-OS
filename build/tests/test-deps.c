/* test-deps.c — Test Suite for KPM Dependency Solver & Version Constraints */

#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include "../../pkg/kratos-deps.h"

static int checks_passed = 0;
static int checks_failed = 0;

static void check(const char *label, int ok)
{
    printf("%-60s %s\n", label, ok ? "[PASS]" : "[FAIL]");
    if (ok) checks_passed++; else checks_failed++;
}

int main(void)
{
    printf("============================================================\n");
    printf("       KRATOSOS DEPENDENCY SOLVER TEST SUITE\n");
    printf("============================================================\n\n");

    /* ── Test 1: Version Comparison ── */
    check("Test 1a: 5.3.0 > 5.2.32", kratos_version_cmp("5.3.0", "5.2.32") > 0);
    check("Test 1b: 2.39 == 2.39", kratos_version_cmp("2.39", "2.39") == 0);
    check("Test 1c: 1.0 < 1.0.1", kratos_version_cmp("1.0", "1.0.1") < 0);
    check("Test 1d: 14.2.0 > 13.3.0", kratos_version_cmp("14.2.0", "13.3.0") > 0);
    check("Test 1e: 2.40 > 2.39", kratos_version_cmp("2.40", "2.39") > 0);
    check("Test 1f: 1.0_p2 > 1.0_p1", kratos_version_cmp("1.0_p2", "1.0_p1") > 0);

    /* ── Test 2: Dependency String Parsing ── */
    kratos_dep_list_t list;
    int count = kratos_parse_dependencies("glibc>=2.39, ncurses>=6.4, bash=5.2.0, openssl!=3.0", &list);
    check("Test 2a: Parsed 4 dependency rules", count == 4);

    if (count == 4) {
        check("Test 2b: Rule 1 name == 'glibc'", strcmp(list.rules[0].name, "glibc") == 0);
        check("Test 2c: Rule 1 op == OP_GE (>=)", list.rules[0].op == OP_GE);
        check("Test 2d: Rule 1 ver == '2.39'", strcmp(list.rules[0].version, "2.39") == 0);

        check("Test 2e: Rule 2 name == 'ncurses'", strcmp(list.rules[1].name, "ncurses") == 0);
        check("Test 2f: Rule 2 op == OP_GE (>=)", list.rules[1].op == OP_GE);

        check("Test 2g: Rule 3 name == 'bash'", strcmp(list.rules[2].name, "bash") == 0);
        check("Test 2h: Rule 3 op == OP_EQ (=)", list.rules[2].op == OP_EQ);

        check("Test 2i: Rule 4 name == 'openssl'", strcmp(list.rules[3].name, "openssl") == 0);
        check("Test 2j: Rule 4 op == OP_NE (!=)", list.rules[3].op == OP_NE);
    }

    /* ── Test 3: Version Constraint Satisfaction ── */
    check("Test 3a: 2.40 satisfies >= 2.39", kratos_version_satisfies("2.40", OP_GE, "2.39") == 1);
    check("Test 3b: 2.38 fails >= 2.39", kratos_version_satisfies("2.38", OP_GE, "2.39") == 0);
    check("Test 3c: 5.2.0 satisfies == 5.2.0", kratos_version_satisfies("5.2.0", OP_EQ, "5.2.0") == 1);
    check("Test 3d: 5.2.1 fails == 5.2.0", kratos_version_satisfies("5.2.1", OP_EQ, "5.2.0") == 0);
    check("Test 3e: 1.0.0 satisfies <= 1.0.0", kratos_version_satisfies("1.0.0", OP_LE, "1.0.0") == 1);
    check("Test 3f: 3.1.0 satisfies != 3.0.0", kratos_version_satisfies("3.1.0", OP_NE, "3.0.0") == 1);
    check("Test 3g: 3.0.0 fails != 3.0.0", kratos_version_satisfies("3.0.0", OP_NE, "3.0.0") == 0);

    /* ── Test 4: Empty & Complex Dependency Strings ── */
    kratos_dep_list_t empty_list;
    check("Test 4a: Parse NULL / empty string", kratos_parse_dependencies("", &empty_list) == 0 && empty_list.count == 0);

    kratos_dep_list_t unconstrained;
    kratos_parse_dependencies("coreutils, sed, gawk", &unconstrained);
    check("Test 4b: Parse unconstrained package list",
          unconstrained.count == 3 &&
          unconstrained.rules[0].op == OP_ANY &&
          strcmp(unconstrained.rules[0].name, "coreutils") == 0);

    printf("\nTotal: %d passed, %d failed.\n", checks_passed, checks_failed);
    return checks_failed == 0 ? 0 : 1;
}
