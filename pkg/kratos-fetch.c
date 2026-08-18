/* kratos-fetch.c — KratosOS Native HTTPS Download Client
 *
 * Responsabilità:
 *   1. Scarica file via HTTPS (GET) con verifica certificato TLS
 *   2. Segue redirect 301/302 (necessario per GitHub Releases)
 *   3. Scrive il body su file (-o) o stdout
 *   4. Mostra progresso download
 *   5. Gestisce Transfer-Encoding: chunked oltre a Content-Length
 *   6. Applica timeout su connessione e lettura per non restare appesi
 *
 * Uso:
 *   kratos-fetch https://example.com/file.tar.gz -o /tmp/file.tar.gz
 *   kratos-fetch https://example.com/index.json
 *   kratos-fetch https://example.com/file.tar.gz -o /tmp/file.tar.gz -q
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
#include <signal.h>
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

#define CA_BUNDLE_PATH     "/etc/ssl/certs/ca-certificates.crt"
#define MAX_REDIRECTS      5
#define READ_BUF_SIZE      8192
#define MAX_URL_LEN        2048
#define MAX_HEADER_SIZE    16384
#define MAX_CHUNK_LINE     64
#define CONNECT_TIMEOUT_SEC 15
#define READ_TIMEOUT_MS     30000

/* Quiet mode: -q suppresses progress/status chatter on stderr.
 * Error messages are always printed regardless of this flag. */
static int g_quiet = 0;

/* ------------------------------------------------------------------ */
/* URL Parser                                                          */
/* ------------------------------------------------------------------ */

typedef struct {
    char host[256];
    char path[MAX_URL_LEN];
    char port[8];
    int  is_https;
} parsed_url_t;

static int parse_url(const char *url, parsed_url_t *out)
{
    memset(out, 0, sizeof(*out));

    const char *host_start;
    if (strncmp(url, "https://", 8) == 0) {
        out->is_https = 1;
        host_start = url + 8;
        strcpy(out->port, "443");
    } else if (strncmp(url, "http://", 7) == 0) {
        out->is_https = 0;
        host_start = url + 7;
        strcpy(out->port, "80");
    } else {
        fprintf(stderr, "[kratos-fetch] Error: only HTTP and HTTPS URLs are supported.\n");
        return -1;
    }

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
    int                     is_https;
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

/* Bytes consumed by SIGALRM while mbedtls_net_connect() is blocked in
 * connect(2). Kernel connect timeouts can otherwise run to several
 * minutes, which is unacceptable for an unattended `kratos update`. */
static volatile sig_atomic_t g_connect_alarm_fired = 0;

static void connect_alarm_handler(int sig)
{
    (void)sig;
    g_connect_alarm_fired = 1;
}

static int net_connect_with_timeout(mbedtls_net_context *net,
                                     const char *host, const char *port)
{
    struct sigaction sa_new, sa_old;
    memset(&sa_new, 0, sizeof(sa_new));
    sa_new.sa_handler = connect_alarm_handler;
    sigemptyset(&sa_new.sa_mask);
    sa_new.sa_flags = 0; /* deliberately no SA_RESTART: interrupt connect() */

    g_connect_alarm_fired = 0;
    sigaction(SIGALRM, &sa_new, &sa_old);
    alarm(CONNECT_TIMEOUT_SEC);

    int ret = mbedtls_net_connect(net, host, port, MBEDTLS_NET_PROTO_TCP);

    alarm(0);
    sigaction(SIGALRM, &sa_old, NULL);

    if (g_connect_alarm_fired) {
        fprintf(stderr, "[kratos-fetch] Error: connection to %s:%s timed out after %ds\n",
                host, port, CONNECT_TIMEOUT_SEC);
        return -1;
    }
    return ret;
}

static int tls_connect(tls_ctx_t *ctx, const char *host, const char *port)
{
    int ret;

    /* TCP connect (bounded by CONNECT_TIMEOUT_SEC) */
    if (net_connect_with_timeout(&ctx->net, host, port) != 0) {
        fprintf(stderr, "[kratos-fetch] Error: TCP connect to %s:%s failed\n", host, port);
        return -1;
    }

    if (!ctx->is_https) {
        return 0; /* No TLS handshake for HTTP */
    }

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

    /* Read timeout: applies to every mbedtls_ssl_read() call, including
     * during the handshake. mbedtls_net_recv_timeout() wraps recv() in a
     * select() with this timeout, so it works on our plain blocking
     * socket without needing a full nonblocking + timer-callback setup. */
    mbedtls_ssl_conf_read_timeout(&ctx->conf, READ_TIMEOUT_MS);

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
                         mbedtls_net_send, mbedtls_net_recv,
                         mbedtls_net_recv_timeout);

    /* TLS handshake */
    while ((ret = mbedtls_ssl_handshake(&ctx->ssl)) != 0) {
        if (ret == MBEDTLS_ERR_SSL_TIMEOUT) {
            fprintf(stderr, "[kratos-fetch] Error: TLS handshake timed out after %dms\n",
                    READ_TIMEOUT_MS);
            return -1;
        }
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
        int ret;
        if (ctx->is_https) {
            ret = mbedtls_ssl_write(&ctx->ssl,
                                     (const unsigned char *)buf + written,
                                     len - written);
        } else {
            ret = mbedtls_net_send(&ctx->net,
                                    (const unsigned char *)buf + written,
                                    len - written);
        }

        if (ret > 0) {
            written += (size_t)ret;
        } else if (ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            return -1;
        }
    }
    return 0;
}

/* body_reader_t pulls body bytes first from whatever was already
 * buffered while reading headers (leftover), then transparently falls
 * back to the TLS socket. Used uniformly for identity transfers with a
 * known Content-Length, identity transfers without one, and chunked
 * transfers (chunk-size lines, chunk data, chunk trailers). */
typedef struct {
    tls_ctx_t *ctx;
    const unsigned char *buf;
    size_t len;
    size_t pos;
} body_reader_t;

/* Returns >0 bytes read, 0 on clean EOF, -1 on error (message already
 * printed). */
static long reader_read(body_reader_t *r, unsigned char *out, size_t want)
{
    if (r->pos < r->len) {
        size_t avail = r->len - r->pos;
        size_t n = avail < want ? avail : want;
        memcpy(out, r->buf + r->pos, n);
        r->pos += n;
        return (long)n;
    }

    for (;;) {
        int ret;
        if (r->ctx->is_https) {
            ret = mbedtls_ssl_read(&r->ctx->ssl, out, want);
        } else {
            ret = mbedtls_net_recv(&r->ctx->net, out, want);
        }

        if (ret > 0) {
            return ret;
        } else if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
            return 0;
        } else if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            continue;
        } else if (ret == MBEDTLS_ERR_SSL_TIMEOUT) {
            fprintf(stderr, "\n[kratos-fetch] Error: read timed out after %dms — server unresponsive.\n",
                    READ_TIMEOUT_MS);
            return -1;
        } else {
            fprintf(stderr, "\n[kratos-fetch] Error: connection read failed (-0x%04x)\n", -ret);
            return -1;
        }
    }
}

/* Reads exactly n bytes and writes them to `out`, updating progress.
 * Returns 0 on success, -1 on short read/error (partial data has
 * already been written — caller is responsible for discarding the
 * output file). */
static int read_exact_to_file(body_reader_t *r, FILE *out, size_t n,
                               size_t *total_written, long content_length,
                               int show_progress)
{
    unsigned char buf[READ_BUF_SIZE];
    size_t remaining = n;
    while (remaining > 0) {
        size_t want = remaining < sizeof(buf) ? remaining : sizeof(buf);
        long got = reader_read(r, buf, want);
        if (got <= 0) return -1;
        fwrite(buf, 1, (size_t)got, out);
        *total_written += (size_t)got;
        remaining -= (size_t)got;

        if (show_progress && !g_quiet) {
            if (content_length > 0) {
                int pct = (int)(*total_written * 100 / (size_t)content_length);
                fprintf(stderr, "\r[kratos-fetch] %zu / %ld bytes (%d%%)",
                        *total_written, content_length, pct);
            } else {
                fprintf(stderr, "\r[kratos-fetch] %zu bytes", *total_written);
            }
        }
    }
    return 0;
}

/* Reads one CRLF-terminated line (used for chunk-size lines and chunk
 * trailers). \r is skipped; \n ends the line. Truncates silently if a
 * line exceeds out_size, which is fine for chunk-size lines. */
static int read_line(body_reader_t *r, char *out, size_t out_size)
{
    size_t i = 0;
    for (;;) {
        unsigned char c;
        long got = reader_read(r, &c, 1);
        if (got <= 0) return -1;
        if (c == '\r') continue;
        if (c == '\n') {
            if (i < out_size) out[i] = '\0';
            else out[out_size - 1] = '\0';
            return 0;
        }
        if (i + 1 < out_size) out[i++] = (char)c;
    }
}

static int do_https_get(const char *url, const char *output_path, int redirect_count)
{
    if (redirect_count > MAX_REDIRECTS) {
        fprintf(stderr, "[kratos-fetch] Error: too many redirects.\n");
        return -1;
    }

    parsed_url_t pu;
    if (parse_url(url, &pu) != 0) return -1;

    if (!g_quiet) fprintf(stderr, "[kratos-fetch] Connecting to %s:%s (%s)...\n",
                          pu.host, pu.port, pu.is_https ? "HTTPS" : "HTTP");

    tls_ctx_t ctx;
    tls_init(&ctx);
    ctx.is_https = pu.is_https;

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
        } else if (ret == MBEDTLS_ERR_SSL_TIMEOUT) {
            fprintf(stderr, "[kratos-fetch] Error: timed out waiting for response headers (%dms).\n",
                    READ_TIMEOUT_MS);
            tls_free(&ctx);
            return -1;
        } else if (ret != MBEDTLS_ERR_SSL_WANT_READ) {
            fprintf(stderr, "[kratos-fetch] Error: read failed (-0x%04x)\n", -ret);
            tls_free(&ctx);
            return -1;
        }
    }

    if (!header_done) {
        /* Either the connection closed early, or headers exceeded
         * MAX_HEADER_SIZE — either way we must not silently fall
         * through and parse a truncated header block as if valid. */
        if (header_len >= sizeof(header_buf) - 1) {
            fprintf(stderr, "[kratos-fetch] Error: response headers exceeded %d bytes without completing.\n",
                    MAX_HEADER_SIZE);
        } else {
            fprintf(stderr, "[kratos-fetch] Error: connection closed before headers completed.\n");
        }
        tls_free(&ctx);
        return -1;
    }

    /* Parse status code */
    int status_code = 0;
    if (sscanf(header_buf, "HTTP/%*d.%*d %d", &status_code) != 1) {
        fprintf(stderr, "[kratos-fetch] Error: invalid HTTP response.\n");
        tls_free(&ctx);
        return -1;
    }

    if (!g_quiet) fprintf(stderr, "[kratos-fetch] HTTP %d\n", status_code);

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

        if (!g_quiet) fprintf(stderr, "[kratos-fetch] Redirecting to: %s\n", new_url);
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

    /* Parse Transfer-Encoding: chunked. Per HTTP/1.1 (RFC 7230 §3.3.3),
     * Transfer-Encoding takes precedence over Content-Length when both
     * are present — a chunked body's real length is only known once
     * fully decoded. */
    int is_chunked = 0;
    char *te_header = strcasestr(header_buf, "\r\nTransfer-Encoding: ");
    if (te_header) {
        char *val = te_header + strlen("\r\nTransfer-Encoding: ");
        if (strncasecmp(val, "chunked", 7) == 0) {
            is_chunked = 1;
            content_length = -1;
        }
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

    body_reader_t reader = {
        .ctx = &ctx,
        .buf = (const unsigned char *)body_start,
        .len = body_offset,
        .pos = 0,
    };

    size_t total_written = 0;
    int transfer_failed = 0;

    if (is_chunked) {
        for (;;) {
            char line[MAX_CHUNK_LINE];
            if (read_line(&reader, line, sizeof(line)) != 0) {
                fprintf(stderr, "[kratos-fetch] Error: failed to read chunk size.\n");
                transfer_failed = 1;
                break;
            }

            char *semi = strchr(line, ';');
            if (semi) *semi = '\0'; /* strip chunk extensions, we don't use them */

            char *endptr = NULL;
            unsigned long chunk_size = strtoul(line, &endptr, 16);
            if (endptr == line) {
                fprintf(stderr, "[kratos-fetch] Error: malformed chunk size line.\n");
                transfer_failed = 1;
                break;
            }

            if (chunk_size == 0) {
                /* Final chunk: consume trailer headers until blank line. */
                for (;;) {
                    char trailer[512];
                    if (read_line(&reader, trailer, sizeof(trailer)) != 0) {
                        fprintf(stderr, "[kratos-fetch] Error: failed to read chunk trailer.\n");
                        transfer_failed = 1;
                        break;
                    }
                    if (trailer[0] == '\0') break;
                }
                break;
            }

            if (read_exact_to_file(&reader, out, (size_t)chunk_size,
                                    &total_written, -1, output_path != NULL) != 0) {
                fprintf(stderr, "[kratos-fetch] Error: chunked transfer interrupted (incomplete download).\n");
                transfer_failed = 1;
                break;
            }

            /* Each chunk's data is followed by a bare CRLF. */
            char crlf_dummy[8];
            if (read_line(&reader, crlf_dummy, sizeof(crlf_dummy)) != 0) {
                fprintf(stderr, "[kratos-fetch] Error: malformed chunk terminator.\n");
                transfer_failed = 1;
                break;
            }
        }
    } else if (content_length >= 0) {
        if (read_exact_to_file(&reader, out, (size_t)content_length,
                                &total_written, content_length, output_path != NULL) != 0) {
            fprintf(stderr, "[kratos-fetch] Error: download interrupted (received %zu of %ld bytes).\n",
                    total_written, content_length);
            transfer_failed = 1;
        }
    } else {
        /* No Content-Length and not chunked: body is delimited by the
         * connection closing (HTTP/1.0-style). A clean TLS close_notify
         * and an abrupt reset are, by design of this fallback, treated
         * the same way here since there is no length to check against —
         * this ambiguity is inherent to close-delimited bodies, not
         * something kratos-fetch can resolve on its own. */
        unsigned char buf[READ_BUF_SIZE];
        for (;;) {
            long got = reader_read(&reader, buf, sizeof(buf));
            if (got > 0) {
                fwrite(buf, 1, (size_t)got, out);
                total_written += (size_t)got;
                if (!g_quiet && output_path) {
                    fprintf(stderr, "\r[kratos-fetch] %zu bytes", total_written);
                }
            } else {
                break;
            }
        }
    }

    if (output_path) {
        if (!g_quiet) fprintf(stderr, "\n");
        fclose(out);
        if (transfer_failed) {
            /* Don't leave a corrupt/truncated file behind masquerading
             * as a successful download. */
            unlink(output_path);
        } else {
            if (!g_quiet) {
                fprintf(stderr, "[kratos-fetch] Saved %zu bytes to %s\n", total_written, output_path);
            }
        }
    }

    tls_free(&ctx);
    return transfer_failed ? -1 : 0;
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

static void show_help(void)
{
    printf("Usage: kratos-fetch <url> [-o output_file] [-q]\n");
    printf("\n");
    printf("Download a file via HTTP or HTTPS with certificate verification.\n");
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
        } else if (strcmp(argv[i], "-q") == 0) {
            g_quiet = 1;
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
