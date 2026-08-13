/* kratos-sha256.h — Standalone SHA-256 Engine for KratosOS Package Manager
 *
 * Implements FIPS 180-4 SHA-256 without external dependencies.
 */

#ifndef KRATOS_SHA256_H
#define KRATOS_SHA256_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t  buffer[64];
} kratos_sha256_ctx_t;

#define KRATOS_SHA256_DIGEST_SIZE 32
#define KRATOS_SHA256_HEX_SIZE    65

void kratos_sha256_init(kratos_sha256_ctx_t *ctx);
void kratos_sha256_update(kratos_sha256_ctx_t *ctx, const uint8_t *data, size_t len);
void kratos_sha256_final(kratos_sha256_ctx_t *ctx, uint8_t digest[KRATOS_SHA256_DIGEST_SIZE]);

/* Computes hex SHA-256 hash of a buffer. hex_out must be at least 65 bytes. */
void kratos_sha256_buffer(const void *data, size_t len, char hex_out[KRATOS_SHA256_HEX_SIZE]);

/* Computes hex SHA-256 hash of a file on disk. Returns 0 on success, -1 on error. */
int kratos_sha256_file(const char *filepath, char hex_out[KRATOS_SHA256_HEX_SIZE]);

#ifdef __cplusplus
}
#endif

#endif /* KRATOS_SHA256_H */
