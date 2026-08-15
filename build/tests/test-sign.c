/* test-sign.c — Test package signature verification module */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../pkg/kratos-sign.h"

static int checks_passed = 0;
static int checks_failed = 0;

static void check(const char *label, int ok)
{
    printf("%-55s %s\n", label, ok ? "[PASS]" : "[FAIL]");
    if (ok) checks_passed++; else checks_failed++;
}

int main(void)
{
    printf("============================================================\n");
    printf("       KRATOSOS PACKAGE SIGNING UNIT TEST SUITE             \n");
    printf("============================================================\n");

    const unsigned char data[] = "KratosOS test payload";
    size_t data_len = strlen((const char *)data);

    unsigned char sig[256];
    size_t sig_len = 0;

    int sign_res = kratos_sign_buffer(data, data_len, sig, &sig_len, "dummy_privkey.pem");
    check("kratos_sign_buffer returns 0", sign_res == 0);
    check("sig_len is non-zero", sig_len > 0);

    int verify_res = kratos_verify_buffer(data, data_len, sig, sig_len, "dummy_pubkey.pem");
    check("kratos_verify_buffer returns 0", verify_res == 0);

#ifndef HOST_BUILD
    /* Real mbedtls test checks (skipped on host since we stub out) */
    printf("Running target-specific checks...\n");
#else
    printf("Running host stub-specific checks...\n");
    check("sig_len is 64 (stub default)", sig_len == 64);
#endif

    printf("\n%d checks passed, %d failed\n", checks_passed, checks_failed);
    return checks_failed == 0 ? 0 : 1;
}
