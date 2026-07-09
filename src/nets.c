/**
 * nets.c - Network sender.
 * Summary: Command line interface for sending stdin to TCP or UDP.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#include "libnets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Prints command usage information.
 * @param name Program executable name.
 * @return None.
 */
static void kc_nets_help(const char *name) {
    printf("Usage: %s <target> [--tcp|--udp] [--ctrl PATH]\n", name);
    printf("\n");
    printf("Target:\n");
    printf("  host[:port] or http://, https://, tcp://, udp:// URL\n");
    printf("\n");
    printf("Options:\n");
    printf("  --tcp          Use TCP (default)\n");
    printf("  --udp          Use UDP\n");
    printf("  --tls          Use TLS over TCP\n");
    printf("  --ctrl PATH    Open a Unix control socket for this one send\n");
    printf("  -h, --help     Show this help\n");
    printf("  -v, --version  Show version\n");
}

/**
 * Prints command version information.
 * @return None.
 */
static void kc_nets_cli_version(void) {
    printf("nets build %llu\n", (unsigned long long)kc_nets_version());
}

/**
 * Reads all standard input into memory.
 * @param out_data Output buffer pointer.
 * @param out_size Output size pointer.
 * @return 0 on success, or 1 on failure.
 */
static int kc_nets_read_stdin(char **out_data, size_t *out_size) {
    char *data = NULL;
    size_t used = 0;
    size_t cap = 0;
    char buf[8192];
    size_t n;

    while ((n = fread(buf, 1, sizeof(buf), stdin)) > 0) {
        if (used + n > cap) {
            size_t next = cap ? cap * 2 : 8192;
            char *tmp;
            while (next < used + n) next *= 2;
            tmp = (char *)realloc(data, next);
            if (!tmp) {
                free(data);
                return 1;
            }
            data = tmp;
            cap = next;
        }
        memcpy(data + used, buf, n);
        used += n;
    }
    if (ferror(stdin)) {
        free(data);
        return 1;
    }
    if (!data) {
        data = (char *)malloc(1);
        if (!data) return 1;
    }
    *out_data = data;
    *out_size = used;
    return 0;
}

/**
 * Parses a host, host:port, or URL target.
 * URL schemes select transport defaults only;
 * they do not add HTTP or TLS framing.
 * Supported schemes are http, https, tcp, and udp.
 * @param text Input target text.
 * @param host Output host buffer.
 * @param host_cap Output host capacity.
 * @param port Output port pointer.
 * @param proto Output protocol pointer.
 * @return 0 on success, or 1 on failure.
 */
static int kc_nets_parse_target(
const char *text,
char *host,
size_t host_cap,
unsigned short *port,
int *proto
) {
    const char *authority;
    const char *authority_end;
    const char *scheme_end;
    const char *host_begin;
    const char *host_end;
    const char *port_begin;
    char *end;
    unsigned long value;
    size_t n;

    if (!text || !text[0] || !host || host_cap == 0 || !port || !proto) return 1;

    authority = text;
    authority_end = text + strlen(text);
    *port = 80;

    scheme_end = strstr(text, "://");
    if (scheme_end) {
        n = (size_t)(scheme_end - text);
        if (n == 4 && strncmp(text, "http", 4) == 0) {
            *port = 80;
            *proto = KC_NETS_TCP;
        } else if (n == 5 && strncmp(text, "https", 5) == 0) {
            *port = 443;
            *proto = KC_NETS_TLS;
        } else if (n == 3 && strncmp(text, "tcp", 3) == 0) {
            *port = 80;
            *proto = KC_NETS_TCP;
        } else if (n == 3 && strncmp(text, "udp", 3) == 0) {
            *port = 80;
            *proto = KC_NETS_UDP;
        } else {
            return 1;
        }
        authority = scheme_end + 3;
        authority_end = authority + strcspn(authority, "/?#");
    }

    if (authority == authority_end) return 1;
    if (memchr(authority, '@', (size_t)(authority_end - authority)) != NULL) return 1;

    port_begin = NULL;
    if (*authority == '[') {
        host_begin = authority + 1;
        host_end = memchr(host_begin, ']', (size_t)(authority_end - host_begin));
        if (!host_end || host_end == host_begin) return 1;
        if (host_end + 1 < authority_end) {
            if (host_end[1] != ':') return 1;
            port_begin = host_end + 2;
            if (port_begin == authority_end) return 1;
        } else if (host_end + 1 != authority_end) {
            return 1;
        }
    } else {
        const char *colon;
        const char *pcur;
        int colon_count;

        colon = NULL;
        colon_count = 0;
        for (pcur = authority; pcur < authority_end; pcur++) {
            if (*pcur == ':') {
                colon = pcur;
                colon_count++;
            }
        }
        if (colon_count > 1) return 1;
        host_begin = authority;
        host_end = colon ? colon : authority_end;
        if (colon) {
            port_begin = colon + 1;
            if (port_begin == authority_end) return 1;
        }
    }

    n = (size_t)(host_end - host_begin);
    if (n == 0 || n >= host_cap) return 1;
    memcpy(host, host_begin, n);
    host[n] = '\0';

    if (port_begin) {
        char port_text[6];
        size_t port_len;

        port_len = (size_t)(authority_end - port_begin);
        if (port_len == 0 || port_len >= sizeof(port_text)) return 1;
        memcpy(port_text, port_begin, port_len);
        port_text[port_len] = '\0';
        value = strtoul(port_text, &end, 10);
        if (*end != '\0' || value == 0 || value > 65535) return 1;
        *port = (unsigned short)value;
    }

    return 0;
}

/**
 * Main application entry point.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status code.
 */
int main(int argc, char **argv) {
    kc_nets_options_t opts = kc_nets_options_default();
    kc_nets_t *ctx = NULL;
    char host[512];
    char *data = NULL;
    size_t size = 0;
    unsigned short port = 80;
    int proto = KC_NETS_TCP;
    int rc;
    int i;

    kc_nets_options_load_env(&opts);

    if (argc >= 2) {
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            kc_nets_help(argv[0]);
            kc_nets_options_free(&opts);
            return 0;
        }
        if (strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "--version") == 0) {
            kc_nets_cli_version();
            kc_nets_options_free(&opts);
            return 0;
        }
    }
    if (argc < 2) {
        kc_nets_help(argv[0]);
        kc_nets_options_free(&opts);
        return 1;
    }
    if (argv[1][0] == '-') {
        fprintf(stderr, "nets: unknown option '%s'\n", argv[1]);
        kc_nets_options_free(&opts);
        return 1;
    }
    if (kc_nets_parse_target(argv[1], host, sizeof(host), &port, &proto) != 0) {
        fprintf(stderr, "nets: invalid target\n");
        kc_nets_options_free(&opts);
        return 1;
    }
    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--tcp") == 0) {
            proto = KC_NETS_TCP;
        } else if (strcmp(argv[i], "--udp") == 0) {
            proto = KC_NETS_UDP;
        } else if (strcmp(argv[i], "--tls") == 0) {
            if (!kc_nets_tls_available()) {
                fprintf(stderr, "nets: TLS not available "
                    "(compiled without OpenSSL)\n");
                kc_nets_options_free(&opts);
                return 1;
            }
            proto = KC_NETS_TLS;
        } else if (strcmp(argv[i], "--ctrl") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "nets: missing value for --ctrl\n");
                kc_nets_options_free(&opts);
                return 1;
            }
            free(opts.ctrl_path);
            opts.ctrl_path = strdup(argv[++i]);
            if (!opts.ctrl_path) {
                fprintf(stderr, "nets: out of memory\n");
                kc_nets_options_free(&opts);
                return 1;
            }
        } else {
            fprintf(stderr, "nets: unknown option '%s'\n", argv[i]);
            kc_nets_options_free(&opts);
            return 1;
        }
    }
    if (kc_nets_open(&ctx, &opts) != KC_NETS_OK) {
        fprintf(stderr, "nets: out of memory\n");
        kc_nets_options_free(&opts);
        return 1;
    }
    kc_nets_listen_signals(ctx);
    if (opts.ctrl_path) {
        if (kc_nets_ctrl_open(ctx, opts.ctrl_path) != KC_NETS_OK) {
            fprintf(stderr, "nets: failed to open control socket at %s\n", opts.ctrl_path);
            kc_nets_close(ctx);
            kc_nets_options_free(&opts);
            return 1;
        }
    }
    kc_nets_ctrl_poll(ctx);
    if (kc_nets_stop_requested(ctx)) {
        kc_nets_close(ctx);
        kc_nets_options_free(&opts);
        fprintf(stderr, "nets: %s\n", kc_nets_strerror(KC_NETS_ESTOP));
        return 1;
    }
    if (kc_nets_read_stdin(&data, &size) != 0) {
        fprintf(stderr, "nets: failed to read stdin\n");
        kc_nets_close(ctx);
        kc_nets_options_free(&opts);
        return 1;
    }
    kc_nets_ctrl_poll(ctx);
    if (kc_nets_stop_requested(ctx)) {
        free(data);
        kc_nets_close(ctx);
        kc_nets_options_free(&opts);
        fprintf(stderr, "nets: %s\n", kc_nets_strerror(KC_NETS_ESTOP));
        return 1;
    }
    kc_nets_ctrl_poll(ctx);
    if (kc_nets_stop_requested(ctx)) {
        rc = KC_NETS_ESTOP;
    } else {
        rc = kc_nets_send(ctx, host, port, proto, data, size);
    }
    kc_nets_ctrl_poll(ctx);
    free(data);
    kc_nets_close(ctx);
    kc_nets_options_free(&opts);
    if (rc != KC_NETS_OK) {
        fprintf(stderr, "nets: %s\n", kc_nets_strerror(rc));
        return 1;
    }
    return 0;
}
