/**
 * nets.c - Network sender.
 * Summary: Command line interface for sending stdin to TCP or UDP.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#include "nets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Prints command usage information.
 * @param name Program executable name.
 * @return None.
 */
static void kc_nets_help(const char *name) {
    printf("Usage: %s <addr>[:port] [--tcp|--udp]\n", name);
    printf("\n");
    printf("Options:\n");
    printf("  --tcp          Use TCP (default)\n");
    printf("  --udp          Use UDP\n");
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
 * Parses address and optional port.
 * @param text Input address text.
 * @param host Output host buffer.
 * @param host_cap Output host capacity.
 * @param port Output port pointer.
 * @return 0 on success, or 1 on failure.
 */
static int kc_nets_parse_addr(
const char *text,
char *host,
size_t host_cap,
unsigned short *port
) {
    const char *colon;
    char *end;
    unsigned long value;
    size_t n;

    if (!text || !text[0] || !host || host_cap == 0 || !port) return 1;
    colon = strrchr(text, ':');
    *port = 80;
    if (!colon) {
        n = strlen(text);
        if (n == 0 || n >= host_cap) return 1;
        memcpy(host, text, n + 1);
        return 0;
    }
    if (colon == text || colon[1] == '\0') return 1;
    n = (size_t)(colon - text);
    if (n == 0 || n >= host_cap) return 1;
    memcpy(host, text, n);
    host[n] = '\0';
    value = strtoul(colon + 1, &end, 10);
    if (*end != '\0' || value == 0 || value > 65535) return 1;
    *port = (unsigned short)value;
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
    if (kc_nets_parse_addr(argv[1], host, sizeof(host), &port) != 0) {
        fprintf(stderr, "nets: invalid address\n");
        kc_nets_options_free(&opts);
        return 1;
    }
    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--tcp") == 0) {
            proto = KC_NETS_TCP;
        } else if (strcmp(argv[i], "--udp") == 0) {
            proto = KC_NETS_UDP;
        } else {
            fprintf(stderr, "nets: unknown option '%s'\n", argv[i]);
            kc_nets_options_free(&opts);
            return 1;
        }
    }
    if (kc_nets_read_stdin(&data, &size) != 0) {
        fprintf(stderr, "nets: failed to read stdin\n");
        kc_nets_options_free(&opts);
        return 1;
    }
    if (kc_nets_open(&ctx, &opts) != KC_NETS_OK) {
        fprintf(stderr, "nets: out of memory\n");
        free(data);
        kc_nets_options_free(&opts);
        return 1;
    }
    kc_nets_listen_signals(ctx);
    rc = kc_nets_send(ctx, host, port, proto, data, size);
    free(data);
    kc_nets_close(ctx);
    kc_nets_options_free(&opts);
    if (rc != KC_NETS_OK) {
        fprintf(stderr, "nets: %s\n", kc_nets_strerror(rc));
        return 1;
    }
    return 0;
}
