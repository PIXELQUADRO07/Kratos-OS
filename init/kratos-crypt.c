/* kratos-crypt.c — KratosOS Standalone SHA-512crypt Password Hashing Engine
 *
 * Implements the SHA-512crypt algorithm (Drepper 2007) without libcrypt dependency.
 * Used by /bin/login and /usr/bin/passwd for password verification and update.
 *
 * Reference: https://www.akkadia.org/drepper/SHA-crypt.txt
 */

#include "kratos-crypt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

/* ── SHA-512 Core ─────────────────────────────────────────────────────────── */

typedef struct {
    uint64_t state[8];
    uint64_t count[2];
    uint8_t  buffer[128];
} sha512_ctx_t;

static const uint64_t K512[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeeb9ULL, 0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
    0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL
};

#define ROR64(x, n) (((x) >> (n)) | ((x) << (64 - (n))))
#define Ch(x, y, z)  (((x) & (y)) ^ (~(x) & (z)))
#define Maj(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define Sigma0(x)    (ROR64(x, 28) ^ ROR64(x, 34) ^ ROR64(x, 39))
#define Sigma1(x)    (ROR64(x, 14) ^ ROR64(x, 18) ^ ROR64(x, 41))
#define sigma0(x)    (ROR64(x,  1) ^ ROR64(x,  8) ^ ((x) >> 7))
#define sigma1(x)    (ROR64(x, 19) ^ ROR64(x, 61) ^ ((x) >> 6))

static void sha512_init(sha512_ctx_t *ctx)
{
    ctx->state[0] = 0x6a09e667f3bcc908ULL;
    ctx->state[1] = 0xbb67ae8584caa73bULL;
    ctx->state[2] = 0x3c6ef372fe94f82bULL;
    ctx->state[3] = 0xa54ff53a5f1d36f1ULL;
    ctx->state[4] = 0x510e527fea6db109ULL;
    ctx->state[5] = 0x9b05688c2b3e6c1fULL;
    ctx->state[6] = 0x1f83d9abfb41bd6bULL;
    ctx->state[7] = 0x5be0cd19137e2179ULL;
    ctx->count[0] = ctx->count[1] = 0;
}

static void sha512_transform(uint64_t state[8], const uint8_t block[128])
{
    uint64_t W[80];
    for (int i = 0; i < 16; i++) {
        W[i] = ((uint64_t)block[i*8]   << 56) | ((uint64_t)block[i*8+1] << 48) |
               ((uint64_t)block[i*8+2] << 40) | ((uint64_t)block[i*8+3] << 32) |
               ((uint64_t)block[i*8+4] << 24) | ((uint64_t)block[i*8+5] << 16) |
               ((uint64_t)block[i*8+6] <<  8) | ((uint64_t)block[i*8+7]);
    }
    for (int i = 16; i < 80; i++)
        W[i] = sigma1(W[i-2]) + W[i-7] + sigma0(W[i-15]) + W[i-16];

    uint64_t a=state[0], b=state[1], c=state[2], d=state[3];
    uint64_t e=state[4], f=state[5], g=state[6], h=state[7];
    for (int i = 0; i < 80; i++) {
        uint64_t T1 = h + Sigma1(e) + Ch(e,f,g) + K512[i] + W[i];
        uint64_t T2 = Sigma0(a) + Maj(a,b,c);
        h=g; g=f; f=e; e=d+T1;
        d=c; c=b; b=a; a=T1+T2;
    }
    state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d;
    state[4]+=e; state[5]+=f; state[6]+=g; state[7]+=h;
}

static void sha512_update(sha512_ctx_t *ctx, const uint8_t *data, size_t len)
{
    size_t index = (size_t)(ctx->count[0] >> 3) & 0x7f;
    ctx->count[0] += (uint64_t)len << 3;
    if (ctx->count[0] < ((uint64_t)len << 3)) ctx->count[1]++;

    size_t part_len = 128 - index;
    size_t i = 0;
    if (len >= part_len) {
        memcpy(&ctx->buffer[index], data, part_len);
        sha512_transform(ctx->state, ctx->buffer);
        for (i = part_len; i + 127 < len; i += 128)
            sha512_transform(ctx->state, &data[i]);
        index = 0;
    }
    memcpy(&ctx->buffer[index], &data[i], len - i);
}

static void sha512_final(sha512_ctx_t *ctx, uint8_t digest[64])
{
    uint8_t bits[16];
    for (int i = 0; i < 8; i++) {
        bits[i]   = (uint8_t)(ctx->count[1] >> ((7-i)*8));
        bits[i+8] = (uint8_t)(ctx->count[0] >> ((7-i)*8));
    }
    size_t index   = (size_t)(ctx->count[0] >> 3) & 0x7f;
    size_t pad_len = (index < 112) ? (112 - index) : (240 - index);
    static const uint8_t padding[128] = { 0x80 };
    sha512_update(ctx, padding, pad_len);
    sha512_update(ctx, bits, 16);
    for (int i = 0; i < 8; i++) {
        digest[i*8]   = (uint8_t)(ctx->state[i] >> 56);
        digest[i*8+1] = (uint8_t)(ctx->state[i] >> 48);
        digest[i*8+2] = (uint8_t)(ctx->state[i] >> 40);
        digest[i*8+3] = (uint8_t)(ctx->state[i] >> 32);
        digest[i*8+4] = (uint8_t)(ctx->state[i] >> 24);
        digest[i*8+5] = (uint8_t)(ctx->state[i] >> 16);
        digest[i*8+6] = (uint8_t)(ctx->state[i] >>  8);
        digest[i*8+7] = (uint8_t)(ctx->state[i]);
    }
}

/* ── Base64 Encoding ──────────────────────────────────────────────────────── */

static const char b64t[] = "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

static void b64_from_24bit(uint8_t b2, uint8_t b1, uint8_t b0, int n, char **out)
{
    uint32_t w = ((uint32_t)b2 << 16) | ((uint32_t)b1 << 8) | b0;
    while (n-- > 0) { **out = b64t[w & 0x3f]; (*out)++; w >>= 6; }
}

/* ── Public API ───────────────────────────────────────────────────────────── */

void kratos_gensalt(char *out, size_t size)
{
    static const char chars[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    char r[17];
    srand((unsigned)(time(NULL) ^ getpid()));
    for (int i = 0; i < 16; i++)
        r[i] = chars[rand() % (sizeof(chars) - 1)];
    r[16] = '\0';
    snprintf(out, size, "$6$%s$", r);
}

/*
 * kratos_crypt — SHA-512crypt (Drepper 2007, steps A-P)
 *
 * Input : key  = plaintext password
 *         salt = "$6$<salt>$" or "$6$<salt>" or raw salt
 * Output: static string "$6$<salt>$<86-char base64 hash>"
 */
char *kratos_crypt(const char *key, const char *salt)
{
    static char result[200];

    /* ── Extract salt string ─────────────────────────────────────────── */
    char clean_salt[17] = {0};
    const char *sp = salt;
    if (strncmp(sp, "$6$", 3) == 0) sp += 3;
    const char *dollar = strchr(sp, '$');
    size_t salt_len = dollar ? (size_t)(dollar - sp) : strlen(sp);
    if (salt_len > 16) salt_len = 16;
    memcpy(clean_salt, sp, salt_len);

    size_t key_len = strlen(key);

    /* ── Step A: digest = sha512(key || salt) ────────────────────────── */
    sha512_ctx_t ctx_a;
    sha512_init(&ctx_a);
    sha512_update(&ctx_a, (const uint8_t *)key,        key_len);
    sha512_update(&ctx_a, (const uint8_t *)clean_salt, salt_len);

    /* ── Step B: digest_b = sha512(key || salt || key) ───────────────── */
    sha512_ctx_t ctx_b;
    sha512_init(&ctx_b);
    sha512_update(&ctx_b, (const uint8_t *)key,        key_len);
    sha512_update(&ctx_b, (const uint8_t *)clean_salt, salt_len);
    sha512_update(&ctx_b, (const uint8_t *)key,        key_len);
    uint8_t digest_b[64];
    sha512_final(&ctx_b, digest_b);

    /* ── Step C: add digest_b to A, repeating to cover key_len bytes ─── */
    {
        size_t rem = key_len;
        while (rem > 64) { sha512_update(&ctx_a, digest_b, 64); rem -= 64; }
        if (rem) sha512_update(&ctx_a, digest_b, rem);
    }

    /* ── Step D: bit-alternation on key_len ─────────────────────────── */
    for (size_t v = key_len; v; v >>= 1) {
        if (v & 1)
            sha512_update(&ctx_a, digest_b, 64);
        else
            sha512_update(&ctx_a, (const uint8_t *)key, key_len);
    }

    uint8_t digest_a[64];
    sha512_final(&ctx_a, digest_a);

    /* ── Steps E-F: P-string ─────────────────────────────────────────── */
    /* E: sha512 of (key repeated key_len times, one copy per character) */
    sha512_ctx_t ctx_p;
    sha512_init(&ctx_p);
    for (size_t i = 0; i < key_len; i++)
        sha512_update(&ctx_p, (const uint8_t *)key, key_len);
    uint8_t dp[64];
    sha512_final(&ctx_p, dp);

    /* F: P = first key_len bytes of dp (cycling over 64-byte digest) */
    uint8_t P[256] = {0};
    for (size_t i = 0; i < key_len && i < 256; i++)
        P[i] = dp[i % 64];

    /* ── Steps G-H: S-string ─────────────────────────────────────────── */
    /* G: sha512 of (salt repeated (16 + digest_a[0]) times) */
    sha512_ctx_t ctx_s;
    sha512_init(&ctx_s);
    for (size_t i = 0; i < (size_t)(16 + digest_a[0]); i++)
        sha512_update(&ctx_s, (const uint8_t *)clean_salt, salt_len);
    uint8_t ds[64];
    sha512_final(&ctx_s, ds);

    /* H: S = first salt_len bytes of ds */
    uint8_t S[64] = {0};
    for (size_t i = 0; i < salt_len; i++)
        S[i] = ds[i % 64];

    /* ── Step I: 5000 stretch rounds ─────────────────────────────────── */
    uint8_t C[64];
    memcpy(C, digest_a, 64);

    for (int r = 0; r < 5000; r++) {
        sha512_ctx_t ctx_c;
        sha512_init(&ctx_c);

        if (r & 1)  sha512_update(&ctx_c, P, key_len);  else sha512_update(&ctx_c, C, 64);
        if (r % 3)  sha512_update(&ctx_c, S, salt_len);
        if (r % 7)  sha512_update(&ctx_c, P, key_len);
        if (r & 1)  sha512_update(&ctx_c, C, 64);       else sha512_update(&ctx_c, P, key_len);

        sha512_final(&ctx_c, C);
    }

    /* ── Step J: produce $6$salt$base64 output ───────────────────────── */
    char *ptr = result;
    ptr += sprintf(ptr, "$6$%s$", clean_salt);

    b64_from_24bit(C[ 0], C[21], C[42], 4, &ptr);
    b64_from_24bit(C[22], C[43], C[ 1], 4, &ptr);
    b64_from_24bit(C[44], C[ 2], C[23], 4, &ptr);
    b64_from_24bit(C[ 3], C[24], C[45], 4, &ptr);
    b64_from_24bit(C[25], C[46], C[ 4], 4, &ptr);
    b64_from_24bit(C[47], C[ 5], C[26], 4, &ptr);
    b64_from_24bit(C[ 6], C[27], C[48], 4, &ptr);
    b64_from_24bit(C[28], C[49], C[ 7], 4, &ptr);
    b64_from_24bit(C[50], C[ 8], C[29], 4, &ptr);
    b64_from_24bit(C[ 9], C[30], C[51], 4, &ptr);
    b64_from_24bit(C[31], C[52], C[10], 4, &ptr);
    b64_from_24bit(C[53], C[11], C[32], 4, &ptr);
    b64_from_24bit(C[12], C[33], C[54], 4, &ptr);
    b64_from_24bit(C[34], C[55], C[13], 4, &ptr);
    b64_from_24bit(C[56], C[14], C[35], 4, &ptr);
    b64_from_24bit(C[15], C[36], C[57], 4, &ptr);
    b64_from_24bit(C[37], C[58], C[16], 4, &ptr);
    b64_from_24bit(C[59], C[17], C[38], 4, &ptr);
    b64_from_24bit(C[18], C[39], C[60], 4, &ptr);
    b64_from_24bit(C[40], C[61], C[19], 4, &ptr);
    b64_from_24bit(C[62], C[20], C[41], 4, &ptr);
    b64_from_24bit(  0,     0,   C[63], 2, &ptr);

    *ptr = '\0';
    return result;
}
