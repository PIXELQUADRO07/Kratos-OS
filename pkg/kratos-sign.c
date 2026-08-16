#ifdef HOST_BUILD

/* Stub implementation for host testing when mbedTLS is not present */
#include "kratos-sign.h"
#include <stdio.h>
#include <string.h>

int kratos_verify_buffer(const unsigned char *data, size_t data_len,
                         const unsigned char *sig, size_t sig_len,
                         const char *pubkey_path)
{
    (void)data; (void)data_len; (void)sig; (void)sig_len; (void)pubkey_path;
    return 0; /* always succeed in host stub */
}

int kratos_verify_file(const char *file_path, const char *sig_path, const char *pubkey_path)
{
    (void)file_path; (void)sig_path; (void)pubkey_path;
    return 0; /* always succeed in host stub */
}

int kratos_sign_buffer(const unsigned char *data, size_t data_len,
                       unsigned char *sig_out, size_t *sig_len_out,
                       const char *privkey_path)
{
    (void)data; (void)data_len; (void)privkey_path;
    memset(sig_out, 0, 64);
    *sig_len_out = 64;
    return 0; /* always succeed in host stub */
}

#else

/* Real implementation using mbedTLS for the target OS */
#include "kratos-sign.h"

#include <mbedtls/pk.h>
#include <mbedtls/error.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char *read_file(const char *path, size_t *len_out)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);

    if (sz <= 0) {
        fclose(f);
        return NULL;
    }

    unsigned char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    size_t rd = fread(buf, 1, (size_t)sz, f);
    buf[rd] = '\0';
    fclose(f);

    *len_out = rd;
    return buf;
}

static int hex_to_bin(const char *hex, unsigned char *bin, size_t bin_max, size_t *bin_len)
{
    size_t len = strlen(hex);
    while (len > 0 && (hex[len-1] == '\n' || hex[len-1] == '\r' || hex[len-1] == ' ')) len--;

    if (len % 2 != 0 || len / 2 > bin_max) return -1;

    for (size_t i = 0; i < len; i += 2) {
        unsigned int val = 0;
        if (sscanf(hex + i, "%2x", &val) != 1) return -1;
        bin[i/2] = (unsigned char)val;
    }

    *bin_len = len / 2;
    return 0;
}

int kratos_verify_buffer(const unsigned char *data, size_t data_len,
                         const unsigned char *sig, size_t sig_len,
                         const char *pubkey_path)
{
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);

    /* Parse public key (PEM or DER format) */
    int ret = mbedtls_pk_parse_public_keyfile(&pk, pubkey_path);
    if (ret != 0) {
        char err[128];
        mbedtls_strerror(ret, err, sizeof(err));
        fprintf(stderr, "[kratos-sign] Error: Failed to parse public key '%s' (%s)\n", pubkey_path, err);
        mbedtls_pk_free(&pk);
        return -1;
    }

    /* Verify signature.
     * Ed25519 in mbedTLS does not pre-hash the message, so we pass
     * MBEDTLS_MD_NONE and the raw message/length as the hash parameter. */
    ret = mbedtls_pk_verify(&pk, MBEDTLS_MD_NONE, data, data_len, sig, sig_len);
    mbedtls_pk_free(&pk);

    if (ret != 0) {
        char err[128];
        mbedtls_strerror(ret, err, sizeof(err));
        fprintf(stderr, "[kratos-sign] Signature verification failed: %s\n", err);
        return -1;
    }

    return 0;
}

int kratos_verify_file(const char *file_path, const char *sig_path, const char *pubkey_path)
{
    size_t data_len = 0;
    unsigned char *data = read_file(file_path, &data_len);
    if (!data) {
        fprintf(stderr, "[kratos-sign] Error: Could not read data file '%s'\n", file_path);
        return -1;
    }

    size_t sig_file_len = 0;
    unsigned char *sig_file = read_file(sig_path, &sig_file_len);
    if (!sig_file) {
        fprintf(stderr, "[kratos-sign] Error: Could not read signature file '%s'\n", sig_path);
        free(data);
        return -1;
    }

    /* Parse signature (could be hex or raw binary) */
    unsigned char sig[256];
    size_t sig_len = 0;

    /* Decode hex signature if it is at least 128 characters long */
    if (sig_file_len >= 128) {
        if (hex_to_bin((const char *)sig_file, sig, sizeof(sig), &sig_len) != 0) {
            /* Fallback: treat as raw binary */
            sig_len = (sig_file_len > sizeof(sig)) ? sizeof(sig) : sig_file_len;
            memcpy(sig, sig_file, sig_len);
        }
    } else {
        /* Treat as raw binary */
        sig_len = (sig_file_len > sizeof(sig)) ? sizeof(sig) : sig_file_len;
        memcpy(sig, sig_file, sig_len);
    }

    free(sig_file);

    int res = kratos_verify_buffer(data, data_len, sig, sig_len, pubkey_path);
    free(data);
    return res;
}

int kratos_sign_buffer(const unsigned char *data, size_t data_len,
                       unsigned char *sig_out, size_t *sig_len_out,
                       const char *privkey_path)
{
    mbedtls_pk_context pk;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;

    mbedtls_pk_init(&pk);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    /* Seed RNG */
    const char *pers = "kratos_sign";
    int ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                    (const unsigned char *)pers, strlen(pers));
    if (ret != 0) {
        fprintf(stderr, "[kratos-sign] RNG seeding failed\n");
        goto cleanup;
    }

    /* Parse private key */
    ret = mbedtls_pk_parse_keyfile(&pk, privkey_path, NULL,
                                   mbedtls_ctr_drbg_random, &ctr_drbg);
    if (ret != 0) {
        char err[128];
        mbedtls_strerror(ret, err, sizeof(err));
        fprintf(stderr, "[kratos-sign] Failed to parse private key '%s' (%s)\n", privkey_path, err);
        goto cleanup;
    }

    /* Sign message. Ed25519 requires MBEDTLS_MD_NONE.
     * mbedTLS 3.x requires a signature buffer size (sig_size) parameter. We pass 256. */
    ret = mbedtls_pk_sign(&pk, MBEDTLS_MD_NONE, data, data_len,
                          sig_out, 256, sig_len_out,
                          mbedtls_ctr_drbg_random, &ctr_drbg);
    if (ret != 0) {
        char err[128];
        mbedtls_strerror(ret, err, sizeof(err));
        fprintf(stderr, "[kratos-sign] Signing failed: %s\n", err);
        goto cleanup;
    }

cleanup:
    mbedtls_pk_free(&pk);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
    return (ret == 0) ? 0 : -1;
}

#endif
