/* kratos-sha256.c — Standalone SHA-256 Engine implementation
 *
 * Reference: NIST FIPS PUB 180-4 Secure Hash Standard (SHS)
 */

#include "kratos-sha256.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(x, y, z)  (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define EP1(x) (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define SIG0(x) (ROTR(x, 7) ^ ROTR(x, 18) ^ ((x) >> 3))
#define SIG1(x) (ROTR(x, 17) ^ ROTR(x, 19) ^ ((x) >> 10))

static const uint32_t K256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

void kratos_sha256_init(kratos_sha256_ctx_t *ctx)
{
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
    ctx->count = 0;
}

static void sha256_transform(uint32_t state[8], const uint8_t data[64])
{
    uint32_t a, b, c, d, e, f, g, h, t1, t2, m[64];
    int i;

    for (i = 0; i < 16; i++) {
        m[i] = ((uint32_t)data[i * 4] << 24) |
               ((uint32_t)data[i * 4 + 1] << 16) |
               ((uint32_t)data[i * 4 + 2] << 8) |
               ((uint32_t)data[i * 4 + 3]);
    }
    for (; i < 64; i++) {
        m[i] = SIG1(m[i - 2]) + m[i - 7] + SIG0(m[i - 15]) + m[i - 16];
    }

    a = state[0];
    b = state[1];
    c = state[2];
    d = state[3];
    e = state[4];
    f = state[5];
    g = state[6];
    h = state[7];

    for (i = 0; i < 64; i++) {
        t1 = h + EP1(e) + CH(e, f, g) + K256[i] + m[i];
        t2 = EP0(a) + MAJ(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

void kratos_sha256_update(kratos_sha256_ctx_t *ctx, const uint8_t *data, size_t len)
{
    size_t i = 0;
    size_t idx = (size_t)(ctx->count & 0x3F);
    ctx->count += len;

    if (idx > 0 && idx + len >= 64) {
        memcpy(&ctx->buffer[idx], data, 64 - idx);
        sha256_transform(ctx->state, ctx->buffer);
        i = 64 - idx;
        idx = 0;
    }

    while (i + 64 <= len) {
        sha256_transform(ctx->state, &data[i]);
        i += 64;
    }

    if (i < len) {
        memcpy(&ctx->buffer[idx], &data[i], len - i);
    }
}

void kratos_sha256_final(kratos_sha256_ctx_t *ctx, uint8_t digest[KRATOS_SHA256_DIGEST_SIZE])
{
    uint8_t pad[64] = { 0x80 };
    uint8_t len_bytes[8];
    uint64_t total_bits = ctx->count * 8;
    int i;

    for (i = 0; i < 8; i++) {
        len_bytes[i] = (uint8_t)(total_bits >> ((7 - i) * 8));
    }

    size_t idx = (size_t)(ctx->count & 0x3F);
    size_t pad_len = (idx < 56) ? (56 - idx) : (120 - idx);

    kratos_sha256_update(ctx, pad, pad_len);
    kratos_sha256_update(ctx, len_bytes, 8);

    for (i = 0; i < 8; i++) {
        digest[i * 4]     = (uint8_t)(ctx->state[i] >> 24);
        digest[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
        digest[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 8);
        digest[i * 4 + 3] = (uint8_t)(ctx->state[i]);
    }
}

void kratos_sha256_buffer(const void *data, size_t len, char hex_out[KRATOS_SHA256_HEX_SIZE])
{
    kratos_sha256_ctx_t ctx;
    uint8_t digest[KRATOS_SHA256_DIGEST_SIZE];
    kratos_sha256_init(&ctx);
    kratos_sha256_update(&ctx, (const uint8_t *)data, len);
    kratos_sha256_final(&ctx, digest);

    for (int i = 0; i < KRATOS_SHA256_DIGEST_SIZE; i++) {
        snprintf(&hex_out[i * 2], 3, "%02x", digest[i]);
    }
    hex_out[KRATOS_SHA256_HEX_SIZE - 1] = '\0';
}

int kratos_sha256_file(const char *filepath, char hex_out[KRATOS_SHA256_HEX_SIZE])
{
    int fd = open(filepath, O_RDONLY);
    if (fd < 0) return -1;

    kratos_sha256_ctx_t ctx;
    kratos_sha256_init(&ctx);

    uint8_t buf[8192];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        kratos_sha256_update(&ctx, buf, (size_t)n);
    }
    close(fd);

    if (n < 0) return -1;

    uint8_t digest[KRATOS_SHA256_DIGEST_SIZE];
    kratos_sha256_final(&ctx, digest);

    for (int i = 0; i < KRATOS_SHA256_DIGEST_SIZE; i++) {
        snprintf(&hex_out[i * 2], 3, "%02x", digest[i]);
    }
    hex_out[KRATOS_SHA256_HEX_SIZE - 1] = '\0';
    return 0;
}
