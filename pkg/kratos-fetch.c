/* kratos-fetch.c — KratosOS Native HTTPS Download Client
 *
 * Responsabilità:
 *   1. Scarica file via HTTPS (GET) con verifica certificato TLS
 *   2. Segue redirect 301/302 (necessario per GitHub Releases)
 *   3. Scrive il body su file (-o) o stdout
 *   4. Mostra progresso download
 *
 * Uso:
 *   kratos-fetch https://example.com/file.tar.gz -o /tmp/file.tar.gz
 *   kratos-fetch https://example.com/index.json
 *
 * Compilazione:
 *   x86_64-kratos-linux-gnu-gcc --sysroot=$KRATOS_SYSROOT -O2 -Wall -std=gnu11 \
 *     -fstack-protector-strong -D_FORTIFY_SOURCE=2 -fPIE -pie \
 *     -o /usr/bin/kratos-fetch kratos-fetch.c \
 *     -lmbedtls -lmbedx509 -lmbedcrypto -Wl,-z,relro,-z,now
 */

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

#define CA_BUNDLE_PATH  "/etc/ssl/certs/ca-certificates.crt"
#define MAX_REDIRECTS   5
#define READ_BUF_SIZE   8192
#define MAX_URL_LEN     2048
#define MAX_HEADER_SIZE 16384

/* ------------------------------------------------------------------ */
/* URL Parser                                                          */
/* ------------------------------------------------------------------ */

typedef struct {
    char host[256];
    char path[MAX_URL_LEN];
    char port[8];
} parsed_url_t;

static int parse_url(const char *url, parsed_url_t *out)
{
    memset(out, 0, sizeof(*out));

    if (strncmp(url, "https://", 8) != 0) {
        fprintf(stderr, "[kratos-fetch] Error: only HTTPS URLs are supported.\n");
        return -1;
    }

    const char *host_start = url + 8;
    const char *path_start = strchr(host_start, '/');
    const char *port_start = strchr(host_start, ':');

    size_t host_len;
    if (port_start && (!path_start || port_start < path_start)) {
        host_len = (size_t)(port_start - host_start);
        const char *port_end = path_start ? path_start : host_start + strlen(host_start);
        size_t port_len = (size_t)(port_end - port_start - 1);
        if (port_len >= sizeof(out->port)) port_len = sizeof(out->port) - 1;
        memcpy(out->port, port_start + 1, port_len);
    } else {
        host_len = path_start ? (size_t)(path_start - host_start) : strlen(host_start);
        strcpy(out->port, "443");
    }

    if (host_len >= sizeof(out->host)) host_len = sizeof(out->host) - 1;
    memcpy(out->host, host_start, host_len);

    if (path_start) {
        snprintf(out->path, sizeof(out->path), "%s", path_start);
    } else {
        strcpy(out->path, "/");
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* TLS Connection                                                      */
/* ------------------------------------------------------------------ */

typedef struct {
    mbedtls_net_context     net;
    mbedtls_ssl_context     ssl;
    mbedtls_ssl_config      conf;
    mbedtls_x509_crt        cacert;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_entropy_context  entropy;
} tls_ctx_t;

static void tls_init(tls_ctx_t *ctx)
{
    mbedtls_net_init(&ctx->net);
    mbedtls_ssl_init(&ctx->ssl);
    mbedtls_ssl_config_init(&ctx->conf);
    mbedtls_x509_crt_init(&ctx->cacert);
    mbedtls_ctr_drbg_init(&ctx->ctr_drbg);
    mbedtls_entropy_init(&ctx->entropy);
}

static void tls_free(tls_ctx_t *ctx)
{
    mbedtls_net_free(&ctx->net);
    mbedtls_ssl_free(&ctx->ssl);
    mbedtls_ssl_config_free(&ctx->conf);
    mbedtls_x509_crt_free(&ctx->cacert);
    mbedtls_ctr_drbg_free(&ctx->ctr_drbg);
    mbedtls_entropy_free(&ctx->entropy);
}

static int tls_connect(tls_ctx_t *ctx, const char *host, const char *port)
{
    int ret;
    const char *pers = "kratos_fetch";

    /* Seed the RNG */
    ret = mbedtls_ctr_drbg_seed(&ctx->ctr_drbg, mbedtls_entropy_func,
                                 &ctx->entropy,
                                 (const unsigned char *)pers, strlen(pers));
    if (ret != 0) {
        fprintf(stderr, "[kratos-fetch] Error: DRBG seed failed (-0x%04x)\n", -ret);
        return -1;
    }

    /* Load CA certificates */
    ret = mbedtls_x509_crt_parse_file(&ctx->cacert, CA_BUNDLE_PATH);
    if (ret < 0) {
        fprintf(stderr, "[kratos-fetch] Error: Failed to load CA bundle %s (-0x%04x)\n",
                CA_BUNDLE_PATH, -ret);
        return -1;
    }

    /* TCP connect */
    ret = mbedtls_net_connect(&ctx->net, host, port, MBEDTLS_NET_PROTO_TCP);
    if (ret != 0) {
        fprintf(stderr, "[kratos-fetch] Error: TCP connect to %s:%s failed (-0x%04x)\n",
                host, port, -ret);
        return -1;
    }

    /* SSL/TLS setup */
    ret = mbedtls_ssl_config_defaults(&ctx->conf,
                                       MBEDTLS_SSL_IS_CLIENT,
                                       MBEDTLS_SSL_TRANSPORT_STREAM,
                                       MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) {
        fprintf(stderr, "[kratos-fetch] Error: SSL config defaults failed (-0x%04x)\n", -ret);
        return -1;
    }

    mbedtls_ssl_conf_authmode(&ctx->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&ctx->conf, &ctx->cacert, NULL);
    mbedtls_ssl_conf_rng(&ctx->conf, mbedtls_ctr_drbg_random, &ctx->ctr_drbg);

    ret = mbedtls_ssl_setup(&ctx->ssl, &ctx->conf);
    if (ret != 0) {
        fprintf(stderr, "[kratos-fetch] Error: SSL setup failed (-0x%04x)\n", -ret);
        return -1;
    }

    ret = mbedtls_ssl_set_hostname(&ctx->ssl, host);
    if (ret != 0) {
        fprintf(stderr, "[kratos-fetch] Error: SSL hostname failed (-0x%04x)\n", -ret);
        return -1;
    }

    mbedtls_ssl_set_bio(&ctx->ssl, &ctx->net,
                         mbedtls_net_send, mbedtls_net_recv, NULL);

    /* TLS handshake */
    while ((ret = mbedtls_ssl_handshake(&ctx->ssl)) != 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            fprintf(stderr, "[kratos-fetch] Error: TLS handshake failed (-0x%04x)\n", -ret);
            return -1;
        }
    }

    /* Verify server certificate */
    uint32_t flags = mbedtls_ssl_get_verify_result(&ctx->ssl);
    if (flags != 0) {
        char vrfy_buf[512];
        mbedtls_x509_crt_verify_info(vrfy_buf, sizeof(vrfy_buf), "  ! ", flags);
        fprintf(stderr, "[kratos-fetch] Error: Certificate verification failed:\n%s\n", vrfy_buf);
        return -1;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* HTTP Request / Response                                             */
/* ------------------------------------------------------------------ */

static int tls_write_all(tls_ctx_t *ctx, const char *buf, size_t len)
{
    size_t written = 0;
    while (written < len) {
        int ret = mbedtls_ssl_write(&ctx->ssl,
                                     (const unsigned char *)buf + written,
                                     len - written);
        if (ret > 0) {
            written += (size_t)ret;
        } else if (ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            return -1;
        }
    }
    return 0;
}

static int do_https_get(const char *url, const char *output_path, int redirect_count)
{
    if (redirect_count > MAX_REDIRECTS) {
        fprintf(stderr, "[kratos-fetch] Error: too many redirects.\n");
        return -1;
    }

    parsed_url_t pu;
    if (parse_url(url, &pu) != 0) return -1;

    fprintf(stderr, "[kratos-fetch] Connecting to %s:%s...\n", pu.host, pu.port);

    tls_ctx_t ctx;
    tls_init(&ctx);

    if (tls_connect(&ctx, pu.host, pu.port) != 0) {
        tls_free(&ctx);
        return -1;
    }

    /* Build HTTP/1.1 GET request */
    char request[MAX_URL_LEN + 512];
    int req_len = snprintf(request, sizeof(request),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: kratos-fetch/1.0\r\n"
        "Accept: */*\r\n"
        "Connection: close\r\n"
        "\r\n",
        pu.path, pu.host);

    if (tls_write_all(&ctx, request, (size_t)req_len) != 0) {
        fprintf(stderr, "[kratos-fetch] Error: failed to send request.\n");
        tls_free(&ctx);
        return -1;
    }

    /* Read response headers */
    char header_buf[MAX_HEADER_SIZE];
    size_t header_len = 0;
    int header_done = 0;

    while (!header_done && header_len < sizeof(header_buf) - 1) {
        int ret = mbedtls_ssl_read(&ctx.ssl,
                                    (unsigned char *)header_buf + header_len,
                                    sizeof(header_buf) - 1 - header_len);
        if (ret > 0) {
            header_len += (size_t)ret;
            header_buf[header_len] = '\0';
            if (strstr(header_buf, "\r\n\r\n")) {
                header_done = 1;
            }
        } else if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
            break;
        } else if (ret != MBEDTLS_ERR_SSL_WANT_READ) {
            fprintf(stderr, "[kratos-fetch] Error: read failed (-0x%04x)\n", -ret);
            tls_free(&ctx);
            return -1;
        }
    }

    /* Parse status code */
    int status_code = 0;
    if (sscanf(header_buf, "HTTP/%*d.%*d %d", &status_code) != 1) {
        fprintf(stderr, "[kratos-fetch] Error: invalid HTTP response.\n");
        tls_free(&ctx);
        return -1;
    }

    fprintf(stderr, "[kratos-fetch] HTTP %d\n", status_code);

    /* Handle redirects */
    if (status_code == 301 || status_code == 302 || status_code == 307 || status_code == 308) {
        char *loc = strcasestr(header_buf, "\r\nLocation: ");
        if (!loc) {
            fprintf(stderr, "[kratos-fetch] Error: redirect without Location header.\n");
            tls_free(&ctx);
            return -1;
        }
        loc += strlen("\r\nLocation: ");
        char *loc_end = strstr(loc, "\r\n");
        if (!loc_end) {
            tls_free(&ctx);
            return -1;
        }

        char new_url[MAX_URL_LEN];
        size_t loc_len = (size_t)(loc_end - loc);
        if (loc_len >= sizeof(new_url)) loc_len = sizeof(new_url) - 1;
        memcpy(new_url, loc, loc_len);
        new_url[loc_len] = '\0';

        fprintf(stderr, "[kratos-fetch] Redirecting to: %s\n", new_url);
        tls_free(&ctx);
        return do_https_get(new_url, output_path, redirect_count + 1);
    }

    if (status_code != 200) {
        fprintf(stderr, "[kratos-fetch] Error: HTTP %d\n", status_code);
        tls_free(&ctx);
        return -1;
    }

    /* Parse Content-Length if available */
    long content_length = -1;
    char *cl_header = strcasestr(header_buf, "\r\nContent-Length: ");
    if (cl_header) {
        content_length = atol(cl_header + strlen("\r\nContent-Length: "));
    }

    /* Find start of body (after \r\n\r\n) */
    char *body_start = strstr(header_buf, "\r\n\r\n");
    size_t body_offset = 0;
    if (body_start) {
        body_start += 4;
        body_offset = header_len - (size_t)(body_start - header_buf);
    }

    /* Open output file */
    FILE *out;
    if (output_path) {
        out = fopen(output_path, "wb");
        if (!out) {
            fprintf(stderr, "[kratos-fetch] Error: cannot open %s: %s\n",
                    output_path, strerror(errno));
            tls_free(&ctx);
            return -1;
        }
    } else {
        out = stdout;
    }

    /* Write body data already read with headers */
    size_t total_written = 0;
    if (body_offset > 0) {
        fwrite(body_start, 1, body_offset, out);
        total_written += body_offset;
    }

    /* Read remaining body */
    unsigned char read_buf[READ_BUF_SIZE];
    for (;;) {
        int ret = mbedtls_ssl_read(&ctx.ssl, read_buf, sizeof(read_buf));
        if (ret > 0) {
            fwrite(read_buf, 1, (size_t)ret, out);
            total_written += (size_t)ret;

            /* Progress indicator */
            if (output_path && content_length > 0) {
                int pct = (int)(total_written * 100 / (size_t)content_length);
                fprintf(stderr, "\r[kratos-fetch] %zu / %ld bytes (%d%%)",
                        total_written, content_length, pct);
            } else if (output_path) {
                fprintf(stderr, "\r[kratos-fetch] %zu bytes", total_written);
            }
        } else if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
            break;
        } else if (ret != MBEDTLS_ERR_SSL_WANT_READ) {
            break;
        }
    }

    if (output_path) {
        fprintf(stderr, "\n");
        fclose(out);
        fprintf(stderr, "[kratos-fetch] Saved %zu bytes to %s\n", total_written, output_path);
    }

    tls_free(&ctx);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

static void show_help(void)
{
    printf("Usage: kratos-fetch <url> [-o output_file]\n");
    printf("\n");
    printf("Download a file via HTTPS with certificate verification.\n");
    printf("\n");
    printf("Options:\n");
    printf("  -o <file>   Write output to file instead of stdout\n");
    printf("  -q          Quiet mode (suppress progress)\n");
    printf("  -h, --help  Show this help\n");
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        show_help();
        return 1;
    }

    const char *url = NULL;
    const char *output = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            show_help();
            return 0;
        } else if (argv[i][0] != '-') {
            url = argv[i];
        } else {
            fprintf(stderr, "[kratos-fetch] Unknown option: %s\n", argv[i]);
            return 1;
        }
    }

    if (!url) {
        fprintf(stderr, "[kratos-fetch] Error: no URL specified.\n");
        return 1;
    }

    return do_https_get(url, output, 0);
}
