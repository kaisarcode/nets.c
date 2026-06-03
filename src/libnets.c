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

static kc_nets_signal_entry_t *g_signal_handlers = NULL;
static int g_n_signal_handlers = 0;
static int g_signal_handlers_capacity = 0;

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
 * Registers a signal callback.
 * @param sig Signal number.
 * @param cb Callback function, or NULL to unregister.
 * @return KC_NETS_OK on success, or a negative error code.
 */
int kc_nets_on_signal(int sig, kc_nets_signal_callback_t cb) {
    int i;
    for (i = 0; i < g_n_signal_handlers; i++) {
        if (g_signal_handlers[i].sig == sig) {
            if (cb) {
                g_signal_handlers[i].cb = cb;
            } else {
                int tail = g_n_signal_handlers - i - 1;
                if (tail > 0) {
                    memmove(&g_signal_handlers[i], &g_signal_handlers[i + 1],
                            (size_t)tail * sizeof(kc_nets_signal_entry_t));
                }
                g_n_signal_handlers--;
            }
            return KC_NETS_OK;
        }
    }
    if (!cb) return KC_NETS_OK;
    if (g_n_signal_handlers >= g_signal_handlers_capacity) {
        int new_cap = g_signal_handlers_capacity ? g_signal_handlers_capacity * 2 : 4;
        kc_nets_signal_entry_t *p = (kc_nets_signal_entry_t *)realloc(g_signal_handlers,
            (size_t)new_cap * sizeof(kc_nets_signal_entry_t));
        if (!p) return KC_NETS_EINVAL;
        g_signal_handlers = p;
        g_signal_handlers_capacity = new_cap;
    }
    g_signal_handlers[g_n_signal_handlers].sig = sig;
    g_signal_handlers[g_n_signal_handlers].cb = cb;
    g_n_signal_handlers++;
    return KC_NETS_OK;
}

/**
 * Raises a signal to registered callbacks.
 * @param sig Signal number.
 * @return KC_NETS_OK on success, or a negative error code.
 */
int kc_nets_raise_signal(int sig) {
    int i;
    for (i = 0; i < g_n_signal_handlers; i++) {
        if (g_signal_handlers[i].sig == sig) {
            g_signal_handlers[i].cb();
            return KC_NETS_OK;
        }
    }
    return KC_NETS_EINVAL;
}

/**
 * Listens for registered signals.
 * @return KC_NETS_OK on success, or a negative error code.
 */
int kc_nets_listen_signals(void) {
    return KC_NETS_OK;
}

/**
 * Listens for a specific signal.
 * @param sig_id Signal number.
 * @return KC_NETS_OK on success, or a negative error code.
 */
int kc_nets_listen_signal(int sig_id) {
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
    if (kc_nets_raise_signal(sig) == 0)
        return;
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
static void kc_nets_close(kc_nets_socket_t sock) {
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
 * @param sock Socket handle.
 * @param data Buffer pointer.
 * @param size Buffer size.
 * @return 0 on success, or -1 on failure.
 */
static int kc_nets_send_all(kc_nets_socket_t sock, const char *data, size_t size) {
    size_t sent = 0;

    while (sent < size) {
#ifdef _WIN32
        int n = send(sock, data + sent, (int)(size - sent), 0);
#else
        ssize_t n = send(sock, data + sent, size - sent, 0);
#endif
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    return 0;
}

/**
 * Sends bytes through one resolved address.
 * @param ai Resolved address.
 * @param proto Protocol selector.
 * @param data Buffer pointer.
 * @param size Buffer size.
 * @return KC_NETS_OK on success, or KC_NETS_ENET.
 */
static int kc_nets_send_addr(
const struct addrinfo *ai,
int proto,
const void *data,
size_t size
) {
    kc_nets_socket_t sock;
    int type = proto == KC_NETS_UDP ? SOCK_DGRAM : SOCK_STREAM;

    sock = socket(ai->ai_family, type, ai->ai_protocol);
    if (sock == KC_NETS_BAD_SOCKET) return KC_NETS_ENET;

    if (proto == KC_NETS_UDP) {
#ifdef _WIN32
        int n = sendto(sock, (const char *)data, (int)size, 0, ai->ai_addr, (int)ai->ai_addrlen);
#else
        ssize_t n = sendto(sock, data, size, 0, ai->ai_addr, ai->ai_addrlen);
#endif
        kc_nets_close(sock);
        return n < 0 || (size_t)n != size ? KC_NETS_ENET : KC_NETS_OK;
    }

    if (connect(sock, ai->ai_addr, ai->ai_addrlen) != 0) {
        kc_nets_close(sock);
        return KC_NETS_ENET;
    }
    if (kc_nets_send_all(sock, (const char *)data, size) != 0) {
        kc_nets_close(sock);
        return KC_NETS_ENET;
    }
    kc_nets_close(sock);
    return KC_NETS_OK;
}

/**
 * Sends bytes to one network address.
 * @param host Destination host or IP address.
 * @param port Destination port.
 * @param proto KC_NETS_TCP or KC_NETS_UDP.
 * @param data Buffer to send.
 * @param size Buffer size in bytes.
 * @return KC_NETS_OK on success, or a negative error code.
 */
int kc_nets_send(
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

    if (!host || !host[0] || !data || (proto != KC_NETS_TCP && proto != KC_NETS_UDP)) {
        return KC_NETS_EINVAL;
    }
    if (kc_nets_platform_init() != 0) return KC_NETS_ENET;

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
        rc = kc_nets_send_addr(ai, proto, data, size);
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
        default: return "unknown error";
    }
}
