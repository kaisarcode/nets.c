/**
 * libnets.c - Network sender.
 * Summary: Core implementation for sending byte buffers over TCP or UDP.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#endif

#include "nets.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <stddef.h>
#include <signal.h>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <signal.h>
#endif

typedef struct {
    int sig;
    kc_nets_signal_callback_t cb;
} kc_nets_signal_entry_t;

static kc_nets_t **g_signal_ctx_list = NULL;
static int g_signal_ctx_cap = 0;
static int g_signal_ctx_count = 0;

struct kc_nets {
    kc_nets_options_t opts;
    kc_nets_signal_entry_t *signal_handlers;
    int n_signal_handlers;
    int signal_handlers_capacity;
    volatile sig_atomic_t stop_requested;
};

/**
 * Returns default-initialized options.
 * @return Default-initialized options.
 */
kc_nets_options_t kc_nets_options_default(void) {
    kc_nets_options_t opts;
    memset(&opts, 0, sizeof(opts));
    return opts;
}

/**
 * Loads environment variables into options.
 * @param opts Options to update.
 * @return None.
 */
void kc_nets_options_load_env(kc_nets_options_t *opts) {
    (void)opts;
}

/**
 * Frees options resources.
 * @param opts Options to free.
 * @return None.
 */
void kc_nets_options_free(kc_nets_options_t *opts) {
    (void)opts;
}

/**
 * Initialize a new nets context.
 * @param ctx_out Destination context pointer.
 * @param opts    Configuration options.
 * @return KC_NETS_OK on success, KC_NETS_EINVAL on failure.
 */
int kc_nets_open(kc_nets_t **ctx_out, kc_nets_options_t *opts) {
    if (!ctx_out || !opts) return KC_NETS_EINVAL;
    *ctx_out = NULL;
    kc_nets_t *ctx = (kc_nets_t *)calloc(1, sizeof(kc_nets_t));
    if (!ctx) return KC_NETS_EINVAL;
    ctx->opts = *opts;
    *ctx_out = ctx;
    return KC_NETS_OK;
}

/**
 * Release a nets context.
 * @param ctx Context pointer.
 * @return KC_NETS_OK.
 */
int kc_nets_close(kc_nets_t *ctx) {
    int i;
    if (!ctx) return KC_NETS_OK;
    for (i = 0; i < g_signal_ctx_count; i++) {
        if (g_signal_ctx_list[i] == ctx) {
            g_signal_ctx_list[i] = g_signal_ctx_list[--g_signal_ctx_count];
            break;
        }
    }
    free(ctx->signal_handlers);
    free(ctx);
    return KC_NETS_OK;
}

/**
 * Request stop for a specific nets context.
 * @param ctx Context handle.
 * @return KC_NETS_OK on success, KC_NETS_EINVAL on failure.
 */
int kc_nets_stop(kc_nets_t *ctx) {
    if (!ctx) return KC_NETS_EINVAL;
    ctx->stop_requested = 1;
    return KC_NETS_OK;
}

/**
 * Registers a signal callback.
 * @param ctx Context handle.
 * @param sig Signal number.
 * @param cb Callback function, or NULL to unregister.
 * @return KC_NETS_OK on success, or a negative error code.
 */
int kc_nets_on_signal(kc_nets_t *ctx, int sig, kc_nets_signal_callback_t cb) {
    int i;
    if (!ctx) return KC_NETS_EINVAL;
    for (i = 0; i < ctx->n_signal_handlers; i++) {
        if (ctx->signal_handlers[i].sig == sig) {
            if (cb) {
                ctx->signal_handlers[i].cb = cb;
            } else {
                int tail = ctx->n_signal_handlers - i - 1;
                if (tail > 0) {
                    memmove(&ctx->signal_handlers[i], &ctx->signal_handlers[i + 1],
                            (size_t)tail * sizeof(kc_nets_signal_entry_t));
                }
                ctx->n_signal_handlers--;
            }
            return KC_NETS_OK;
        }
    }
    if (!cb) return KC_NETS_OK;
    if (ctx->n_signal_handlers >= ctx->signal_handlers_capacity) {
        int new_cap = ctx->signal_handlers_capacity ? ctx->signal_handlers_capacity * 2 : 4;
        kc_nets_signal_entry_t *p = (kc_nets_signal_entry_t *)realloc(ctx->signal_handlers,
            (size_t)new_cap * sizeof(kc_nets_signal_entry_t));
        if (!p) return KC_NETS_EINVAL;
        ctx->signal_handlers = p;
        ctx->signal_handlers_capacity = new_cap;
    }
    ctx->signal_handlers[ctx->n_signal_handlers].sig = sig;
    ctx->signal_handlers[ctx->n_signal_handlers].cb = cb;
    ctx->n_signal_handlers++;
    return KC_NETS_OK;
}

/**
 * Raises a signal to registered callbacks.
 * @param ctx Context handle.
 * @param sig Signal number.
 * @return KC_NETS_OK on success, or a negative error code.
 */
int kc_nets_raise_signal(kc_nets_t *ctx, int sig) {
    int i;
    if (!ctx) return KC_NETS_EINVAL;
    for (i = 0; i < ctx->n_signal_handlers; i++) {
        if (ctx->signal_handlers[i].sig == sig) {
            ctx->signal_handlers[i].cb(ctx);
            return KC_NETS_OK;
        }
    }
    return KC_NETS_EINVAL;
}

/**
 * Store context internally for use by the static signal listener.
 * @param ctx Context handle.
 * @return KC_NETS_OK on success, KC_NETS_EINVAL on failure.
 */
int kc_nets_listen_signals(kc_nets_t *ctx) {
    if (!ctx) return KC_NETS_EINVAL;
    if (g_signal_ctx_count >= g_signal_ctx_cap) {
        int new_cap = g_signal_ctx_cap ? g_signal_ctx_cap * 2 : 4;
        kc_nets_t **new_list = (kc_nets_t **)realloc(g_signal_ctx_list,
            (size_t)new_cap * sizeof(kc_nets_t *));
        if (!new_list) return KC_NETS_EINVAL;
        g_signal_ctx_list = new_list;
        g_signal_ctx_cap = new_cap;
    }
    g_signal_ctx_list[g_signal_ctx_count++] = ctx;
    return KC_NETS_OK;
}

/**
 * Wire a specific OS signal to the library listener.
 * @param ctx    Context handle.
 * @param sig_id OS signal ID.
 * @return KC_NETS_OK on success, KC_NETS_EINVAL on failure.
 */
int kc_nets_listen_signal(kc_nets_t *ctx, int sig_id) {
    if (!ctx) return KC_NETS_EINVAL;
    if (g_signal_ctx_count >= g_signal_ctx_cap) {
        int new_cap = g_signal_ctx_cap ? g_signal_ctx_cap * 2 : 4;
        kc_nets_t **new_list = (kc_nets_t **)realloc(g_signal_ctx_list,
            (size_t)new_cap * sizeof(kc_nets_t *));
        if (!new_list) return KC_NETS_EINVAL;
        g_signal_ctx_list = new_list;
        g_signal_ctx_cap = new_cap;
    }
    g_signal_ctx_list[g_signal_ctx_count++] = ctx;
#ifdef _WIN32
    (void)sig_id;
#else
    signal(sig_id, kc_nets_signal_listener);
#endif
    return KC_NETS_OK;
}

/**
 * Default signal listener.
 * @param sig Signal number.
 * @return None.
 */
void kc_nets_signal_listener(int sig) {
    int i;
    for (i = 0; i < g_signal_ctx_count; i++) {
        if (g_signal_ctx_list[i] &&
            kc_nets_raise_signal(g_signal_ctx_list[i], sig) == 0)
            return;
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

#ifdef _WIN32
typedef SOCKET kc_nets_socket_t;
#define KC_NETS_BAD_SOCKET INVALID_SOCKET
#else
typedef int kc_nets_socket_t;
#define KC_NETS_BAD_SOCKET -1
#endif

/**
 * Closes one socket.
 * @param sock Socket handle.
 * @return None.
 */
static void kc_nets_close_sock(kc_nets_socket_t sock) {
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
}

/**
 * Initializes the socket layer.
 * @return 0 on success, or -1 on failure.
 */
static int kc_nets_platform_init(void) {
#ifdef _WIN32
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0 ? 0 : -1;
#else
    return 0;
#endif
}

/**
 * Cleans up the socket layer.
 * @return None.
 */
static void kc_nets_platform_cleanup(void) {
#ifdef _WIN32
    WSACleanup();
#endif
}

/**
 * Sends all bytes over a connected TCP socket.
 * @param ctx  Context handle.
 * @param sock Socket handle.
 * @param data Buffer pointer.
 * @param size Buffer size.
 * @return KC_NETS_OK on success, KC_NETS_ENET on error,
 *         KC_NETS_ESTOP if stopped.
 */
static int kc_nets_send_all(kc_nets_t *ctx, kc_nets_socket_t sock, const char *data, size_t size) {
    size_t sent = 0;

    while (sent < size) {
        if (ctx && ctx->stop_requested) return KC_NETS_ESTOP;
#ifdef _WIN32
        int n = send(sock, data + sent, (int)(size - sent), 0);
#else
        ssize_t n = send(sock, data + sent, size - sent, 0);
#endif
        if (n <= 0) return KC_NETS_ENET;
        sent += (size_t)n;
    }
    return KC_NETS_OK;
}

/**
 * Sends bytes through one resolved address.
 * @param ctx  Context handle.
 * @param ai   Resolved address.
 * @param proto Protocol selector.
 * @param data Buffer pointer.
 * @param size Buffer size.
 * @return KC_NETS_OK on success, or a negative error code.
 */
static int kc_nets_send_addr(
kc_nets_t *ctx,
const struct addrinfo *ai,
int proto,
const void *data,
size_t size
) {
    kc_nets_socket_t sock;
    int type = proto == KC_NETS_UDP ? SOCK_DGRAM : SOCK_STREAM;
    int rc;

    if (ctx && ctx->stop_requested) return KC_NETS_ESTOP;

    sock = socket(ai->ai_family, type, ai->ai_protocol);
    if (sock == KC_NETS_BAD_SOCKET) return KC_NETS_ENET;

    if (proto == KC_NETS_UDP) {
#ifdef _WIN32
        int n = sendto(sock, (const char *)data, (int)size, 0, ai->ai_addr, (int)ai->ai_addrlen);
#else
        ssize_t n = sendto(sock, data, size, 0, ai->ai_addr, ai->ai_addrlen);
#endif
        kc_nets_close_sock(sock);
        return n < 0 || (size_t)n != size ? KC_NETS_ENET : KC_NETS_OK;
    }

    if (connect(sock, ai->ai_addr, ai->ai_addrlen) != 0) {
        kc_nets_close_sock(sock);
        return KC_NETS_ENET;
    }
    rc = kc_nets_send_all(ctx, sock, (const char *)data, size);
    if (rc != KC_NETS_OK) {
        kc_nets_close_sock(sock);
        return rc;
    }
    kc_nets_close_sock(sock);
    return KC_NETS_OK;
}

/**
 * Sends bytes to one network address.
 * @param ctx  Context handle.
 * @param host Destination host or IP address.
 * @param port Destination port.
 * @param proto KC_NETS_TCP or KC_NETS_UDP.
 * @param data Buffer to send.
 * @param size Buffer size in bytes.
 * @return KC_NETS_OK on success, or a negative error code.
 */
int kc_nets_send(
kc_nets_t *ctx,
const char *host,
unsigned short port,
int proto,
const void *data,
size_t size
) {
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    struct addrinfo *ai;
    char port_text[16];
    int rc;

    if (!ctx || !host || !host[0] || !data || (proto != KC_NETS_TCP && proto != KC_NETS_UDP)) {
        return KC_NETS_EINVAL;
    }
    if (kc_nets_platform_init() != 0) return KC_NETS_ENET;
    if (ctx->stop_requested) return KC_NETS_ESTOP;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = proto == KC_NETS_UDP ? SOCK_DGRAM : SOCK_STREAM;

    snprintf(port_text, sizeof(port_text), "%u", (unsigned int)port);
    rc = getaddrinfo(host, port_text, &hints, &res);
    if (rc != 0) {
        kc_nets_platform_cleanup();
        return KC_NETS_ENET;
    }

    rc = KC_NETS_ENET;
    for (ai = res; ai; ai = ai->ai_next) {
        rc = kc_nets_send_addr(ctx, ai, proto, data, size);
        if (rc == KC_NETS_OK) break;
    }

    freeaddrinfo(res);
    kc_nets_platform_cleanup();
    return rc;
}

/**
 * Returns a static message for a nets status code.
 * @param code Status code.
 * @return Static message.
 */
const char *kc_nets_strerror(int code) {
    switch (code) {
        case KC_NETS_OK: return "ok";
        case KC_NETS_EINVAL: return "invalid argument";
        case KC_NETS_ENET: return "network error";
        case KC_NETS_ESTOP: return "operation stopped";
        default: return "unknown error";
    }
}

#ifndef KC_NETS_BUILD_VERSION
#define KC_NETS_BUILD_VERSION 0
#endif

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_nets_version(void) {
    return (uint64_t)KC_NETS_BUILD_VERSION;
}
