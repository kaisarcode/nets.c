/**
 * libnets.c - Network sender.
 * Summary: Core implementation for sending byte buffers over TCP or UDP.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netdb.h>
#include <unistd.h>
#endif

#include "libnets.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <stddef.h>
#include <signal.h>

#ifdef KC_NETS_OPENSSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

typedef struct {
    int sig;
    kc_nets_signal_callback_t cb;
} kc_nets_signal_entry_t;

typedef struct {
    char *cmd;
    kc_nets_ctrl_callback_t cb;
} kc_nets_ctrl_entry_t;

typedef struct {
    int fd;
    char *buf;
    size_t used;
    size_t cap;
} kc_nets_ctrl_conn_t;

static kc_nets_t **g_signal_ctx_list = NULL;
static int g_signal_ctx_cap = 0;
static int g_signal_ctx_count = 0;

struct kc_nets {
    kc_nets_options_t opts;
    kc_nets_signal_entry_t *signal_handlers;
    int n_signal_handlers;
    int signal_handlers_capacity;
    volatile sig_atomic_t stop_requested;
    int ctrl_fd;
    char *ctrl_path;
    kc_nets_ctrl_entry_t *ctrl_handlers;
    int n_ctrl_handlers;
    int ctrl_handlers_cap;
    kc_nets_ctrl_conn_t *ctrl_conns;
    int n_ctrl_conns;
    int ctrl_conns_cap;
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
    const char *value;

    if (!opts) {
        return;
    }

    value = getenv("KC_NETS_CTRL");
    if (!value) {
        return;
    }

    free(opts->ctrl_path);
    opts->ctrl_path = strdup(value);
}

/**
 * Frees options resources.
 * @param opts Options to free.
 * @return None.
 */
void kc_nets_options_free(kc_nets_options_t *opts) {
    if (!opts) {
        return;
    }

    free(opts->ctrl_path);
    opts->ctrl_path = NULL;
}

/**
 * Initialize a new nets context.
 * @param ctx_out Destination context pointer.
 * @param opts    Configuration options.
 * @return KC_NETS_OK on success, KC_NETS_EINVAL on failure.
 */
int kc_nets_open(kc_nets_t **ctx_out, kc_nets_options_t *opts) {
    kc_nets_t *ctx;

    if (!ctx_out || !opts) return KC_NETS_EINVAL;
    *ctx_out = NULL;
    ctx = (kc_nets_t *)calloc(1, sizeof(kc_nets_t));
    if (!ctx) return KC_NETS_EINVAL;
    ctx->opts = *opts;
    ctx->opts.ctrl_path = opts->ctrl_path ? strdup(opts->ctrl_path) : NULL;
    ctx->ctrl_fd = -1;
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

    kc_nets_ctrl_close(ctx);
    kc_nets_options_free(&ctx->opts);
    for (i = 0; i < ctx->n_ctrl_handlers; i++) {
        free(ctx->ctrl_handlers[i].cmd);
    }
    free(ctx->signal_handlers);
    free(ctx->ctrl_handlers);
    free(ctx->ctrl_conns);
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
 * Check whether a stop has been requested on the context.
 * @param ctx Context pointer.
 * @return 1 if stop was requested, 0 otherwise.
 */
int kc_nets_stop_requested(kc_nets_t *ctx) {
    if (!ctx) {
        return 0;
    }

    return ctx->stop_requested ? 1 : 0;
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

#ifdef KC_NETS_OPENSSL

/**
 * Returns a lazily-initialized shared SSL context.
 * @return SSL_CTX pointer, or NULL on failure.
 */
static SSL_CTX *kc_nets_ssl_ctx(void) {
    static SSL_CTX *ctx = NULL;
    if (!ctx) {
        OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS, NULL);
        ctx = SSL_CTX_new(TLS_client_method());
    }
    return ctx;
}
#endif

#ifndef _WIN32

/**
 * Sends a text message to a control connection.
 * @param fd File descriptor.
 * @param msg Message string.
 * @return KC_NETS_OK on success, or a negative error code.
 */
static int kc_nets_ctrl_send(int fd, const char *msg) {
    size_t len;

    if (!msg) {
        return KC_NETS_EINVAL;
    }

    len = strlen(msg);
    return (size_t)write(fd, msg, len) == len ? KC_NETS_OK : KC_NETS_ENET;
}

/**
 * Handles the HELP control command.
 * @param ctx Context handle.
 * @param fd Control connection fd.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return KC_NETS_OK on success, or a negative error code.
 */
static int kc_nets_ctrl_default_help(kc_nets_t *ctx, int fd, int argc, char **argv) {
    char tmp[4096];
    size_t pos;
    int i;

    (void)argc;
    (void)argv;

    pos = 0;
    for (i = 0; i < ctx->n_ctrl_handlers; i++) {
        size_t len;

        len = strlen(ctx->ctrl_handlers[i].cmd);
        if (pos + len + 2 > sizeof(tmp)) {
            break;
        }
        if (pos > 0) {
            tmp[pos++] = ' ';
        }
        memcpy(tmp + pos, ctx->ctrl_handlers[i].cmd, len);
        pos += len;
    }

    if (pos + 1 > sizeof(tmp)) {
        pos = sizeof(tmp) - 1;
    }
    tmp[pos] = '\n';
    if (kc_nets_ctrl_send(fd, "OK ") != KC_NETS_OK) {
        return KC_NETS_ENET;
    }
    return (size_t)write(fd, tmp, pos + 1) == pos + 1 ? KC_NETS_OK : KC_NETS_ENET;
}

/**
 * Handles the STOP control command.
 * @param ctx Context handle.
 * @param fd Control connection fd.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return KC_NETS_OK on success, or a negative error code.
 */
static int kc_nets_ctrl_default_stop(kc_nets_t *ctx, int fd, int argc, char **argv) {
    (void)argc;
    (void)argv;

    if (kc_nets_stop(ctx) != KC_NETS_OK) {
        return kc_nets_ctrl_send(fd, "ERR\n");
    }
    return kc_nets_ctrl_send(fd, "OK\n");
}

/**
 * Handles the GET control command.
 * @param ctx Context handle.
 * @param fd Control connection fd.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return KC_NETS_OK on success, or a negative error code.
 */
static int kc_nets_ctrl_default_get(kc_nets_t *ctx, int fd, int argc, char **argv) {
    char tmp[512];

    if (argc < 2) {
        return kc_nets_ctrl_send(fd, "ERR missing key\n");
    }

    if (strcmp(argv[1], "ctrl_path") == 0) {
        snprintf(tmp, sizeof(tmp), "OK %s\n", ctx->opts.ctrl_path ? ctx->opts.ctrl_path : "");
        return kc_nets_ctrl_send(fd, tmp);
    }

    if (strcmp(argv[1], "reserved") == 0) {
        snprintf(tmp, sizeof(tmp), "OK %d\n", ctx->opts.reserved);
        return kc_nets_ctrl_send(fd, tmp);
    }

    return kc_nets_ctrl_send(fd, "ERR unknown key\n");
}

/**
 * Handles the SET control command.
 * @param ctx Context handle.
 * @param fd Control connection fd.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return KC_NETS_OK on success, or a negative error code.
 */
static int kc_nets_ctrl_default_set(kc_nets_t *ctx, int fd, int argc, char **argv) {
    char *copy;

    if (argc < 3) {
        return kc_nets_ctrl_send(fd, "ERR missing value\n");
    }

    if (strcmp(argv[1], "ctrl_path") == 0) {
        copy = strdup(argv[2]);
        if (!copy) {
            return kc_nets_ctrl_send(fd, "ERR out of memory\n");
        }
        free(ctx->opts.ctrl_path);
        ctx->opts.ctrl_path = copy;
        return kc_nets_ctrl_send(fd, "OK\n");
    }

    return kc_nets_ctrl_send(fd, "ERR unknown key\n");
}

/**
 * Parses one command line and dispatches to the registered handler.
 * @param ctx Context handle.
 * @param fd Control connection fd.
 * @param line Complete command line.
 * @return KC_NETS_OK on success, or a negative error code.
 */
static int kc_nets_ctrl_dispatch(kc_nets_t *ctx, int fd, const char *line) {
    char *copy;
    char *argv[64];
    int argc;
    int i;

    copy = strdup(line);
    if (!copy) {
        return KC_NETS_EINVAL;
    }

    argc = 0;
    argv[argc] = strtok(copy, " \t\r\n");
    if (argv[argc]) {
        argc++;
        while (argc < 64 && (argv[argc] = strtok(NULL, " \t\r\n")) != NULL) {
            argc++;
        }
    }

    if (argc == 0) {
        free(copy);
        return KC_NETS_OK;
    }

    for (i = 0; i < ctx->n_ctrl_handlers; i++) {
        if (strcmp(ctx->ctrl_handlers[i].cmd, argv[0]) == 0) {
            int rc;

            rc = ctx->ctrl_handlers[i].cb(ctx, fd, argc, argv);
            free(copy);
            return rc;
        }
    }

    free(copy);
    return kc_nets_ctrl_send(fd, "ERR unknown command\n");
}

#endif

/**
 * Register a control command handler.
 * @param ctx Context handle.
 * @param cmd Command name.
 * @param cb Callback function, or NULL to remove.
 * @return KC_NETS_OK on success, or a negative error code.
 */
int kc_nets_ctrl_on(kc_nets_t *ctx, const char *cmd, kc_nets_ctrl_callback_t cb) {
    int i;

    if (!ctx || !cmd) {
        return KC_NETS_EINVAL;
    }

    for (i = 0; i < ctx->n_ctrl_handlers; i++) {
        if (strcmp(ctx->ctrl_handlers[i].cmd, cmd) == 0) {
            if (cb) {
                ctx->ctrl_handlers[i].cb = cb;
            } else {
                int tail;

                free(ctx->ctrl_handlers[i].cmd);
                tail = ctx->n_ctrl_handlers - i - 1;
                if (tail > 0) {
                    memmove(&ctx->ctrl_handlers[i], &ctx->ctrl_handlers[i + 1],
                        (size_t)tail * sizeof(kc_nets_ctrl_entry_t));
                }
                ctx->n_ctrl_handlers--;
            }
            return KC_NETS_OK;
        }
    }

    if (!cb) {
        return KC_NETS_OK;
    }

    if (ctx->n_ctrl_handlers >= ctx->ctrl_handlers_cap) {
        int new_cap;
        kc_nets_ctrl_entry_t *grown;

        new_cap = ctx->ctrl_handlers_cap ? ctx->ctrl_handlers_cap * 2 : 4;
        grown = (kc_nets_ctrl_entry_t *)realloc(ctx->ctrl_handlers,
            (size_t)new_cap * sizeof(kc_nets_ctrl_entry_t));
        if (!grown) {
            return KC_NETS_EINVAL;
        }
        ctx->ctrl_handlers = grown;
        ctx->ctrl_handlers_cap = new_cap;
    }

    ctx->ctrl_handlers[ctx->n_ctrl_handlers].cmd = strdup(cmd);
    if (!ctx->ctrl_handlers[ctx->n_ctrl_handlers].cmd) {
        return KC_NETS_EINVAL;
    }
    ctx->ctrl_handlers[ctx->n_ctrl_handlers].cb = cb;
    ctx->n_ctrl_handlers++;
    return KC_NETS_OK;
}

/**
 * Remove a control command handler.
 * @param ctx Context handle.
 * @param cmd Command name.
 * @return KC_NETS_OK on success, or a negative error code.
 */
int kc_nets_ctrl_off(kc_nets_t *ctx, const char *cmd) {
    return kc_nets_ctrl_on(ctx, cmd, NULL);
}

/**
 * Open a Unix domain socket for control commands.
 * @param ctx Context handle.
 * @param path Socket path.
 * @return KC_NETS_OK on success, or a negative error code.
 */
int kc_nets_ctrl_open(kc_nets_t *ctx, const char *path) {
#ifndef _WIN32
    struct sockaddr_un addr;
    int fd;
    int flags;

    if (!ctx || !path) {
        return KC_NETS_EINVAL;
    }

    if (ctx->ctrl_fd >= 0) {
        kc_nets_ctrl_close(ctx);
    }

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return KC_NETS_ENET;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    unlink(path);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return KC_NETS_ENET;
    }

    if (listen(fd, 4) < 0) {
        close(fd);
        unlink(path);
        return KC_NETS_ENET;
    }

    flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    free(ctx->ctrl_path);
    ctx->ctrl_path = strdup(path);
    if (!ctx->ctrl_path) {
        close(fd);
        unlink(path);
        return KC_NETS_EINVAL;
    }
    ctx->ctrl_fd = fd;

    if (kc_nets_ctrl_on(ctx, "HELP", kc_nets_ctrl_default_help) != KC_NETS_OK ||
        kc_nets_ctrl_on(ctx, "STOP", kc_nets_ctrl_default_stop) != KC_NETS_OK ||
        kc_nets_ctrl_on(ctx, "GET", kc_nets_ctrl_default_get) != KC_NETS_OK ||
        kc_nets_ctrl_on(ctx, "SET", kc_nets_ctrl_default_set) != KC_NETS_OK) {
        kc_nets_ctrl_close(ctx);
        return KC_NETS_EINVAL;
    }

    return KC_NETS_OK;
#else
    (void)ctx;
    (void)path;
    return KC_NETS_EINVAL;
#endif
}

/**
 * Close the control socket and all active connections.
 * @param ctx Context handle.
 * @return KC_NETS_OK on success, or a negative error code.
 */
int kc_nets_ctrl_close(kc_nets_t *ctx) {
#ifndef _WIN32
    int i;

    if (!ctx) {
        return KC_NETS_OK;
    }

    for (i = 0; i < ctx->n_ctrl_conns; i++) {
        if (ctx->ctrl_conns[i].fd >= 0) {
            close(ctx->ctrl_conns[i].fd);
        }
        free(ctx->ctrl_conns[i].buf);
        ctx->ctrl_conns[i].buf = NULL;
    }
    ctx->n_ctrl_conns = 0;

    if (ctx->ctrl_fd >= 0) {
        close(ctx->ctrl_fd);
        ctx->ctrl_fd = -1;
    }

    if (ctx->ctrl_path) {
        unlink(ctx->ctrl_path);
        free(ctx->ctrl_path);
        ctx->ctrl_path = NULL;
    }

    return KC_NETS_OK;
#else
    (void)ctx;
    return KC_NETS_OK;
#endif
}

/**
 * Non-blocking poll: accept connections, read and dispatch commands.
 * @param ctx Context handle.
 * @return Number of commands handled, or a negative error code.
 */
int kc_nets_ctrl_poll(kc_nets_t *ctx) {
#ifndef _WIN32
    struct pollfd pfds[64];
    int nfds;
    int handled;
    int i;

    if (!ctx || ctx->ctrl_fd < 0) {
        return 0;
    }

    nfds = 0;
    pfds[nfds].fd = ctx->ctrl_fd;
    pfds[nfds].events = POLLIN;
    pfds[nfds].revents = 0;
    nfds++;

    for (i = 0; i < ctx->n_ctrl_conns && nfds < 64; i++) {
        if (ctx->ctrl_conns[i].fd >= 0) {
            pfds[nfds].fd = ctx->ctrl_conns[i].fd;
            pfds[nfds].events = POLLIN;
            pfds[nfds].revents = 0;
            nfds++;
        }
    }

    if (poll(pfds, (nfds_t)nfds, 0) < 0) {
        return KC_NETS_ENET;
    }

    handled = 0;
    if (pfds[0].revents & POLLIN) {
        int conn_fd;

        conn_fd = accept(ctx->ctrl_fd, NULL, NULL);
        if (conn_fd >= 0) {
            int flags;

            flags = fcntl(conn_fd, F_GETFL, 0);
            if (flags >= 0) {
                fcntl(conn_fd, F_SETFL, flags | O_NONBLOCK);
            }
            if (ctx->n_ctrl_conns >= ctx->ctrl_conns_cap) {
                int new_cap;
                kc_nets_ctrl_conn_t *grown;

                new_cap = ctx->ctrl_conns_cap ? ctx->ctrl_conns_cap * 2 : 4;
                grown = (kc_nets_ctrl_conn_t *)realloc(ctx->ctrl_conns,
                    (size_t)new_cap * sizeof(kc_nets_ctrl_conn_t));
                if (grown) {
                    ctx->ctrl_conns = grown;
                    ctx->ctrl_conns_cap = new_cap;
                }
            }
            if (ctx->n_ctrl_conns < ctx->ctrl_conns_cap) {
                ctx->ctrl_conns[ctx->n_ctrl_conns].fd = conn_fd;
                ctx->ctrl_conns[ctx->n_ctrl_conns].buf = NULL;
                ctx->ctrl_conns[ctx->n_ctrl_conns].used = 0;
                ctx->ctrl_conns[ctx->n_ctrl_conns].cap = 0;
                ctx->n_ctrl_conns++;
            } else {
                close(conn_fd);
            }
        }
    }

    for (i = 0; i < ctx->n_ctrl_conns; i++) {
        int j;
        int pidx;

        pidx = -1;
        for (j = 1; j < nfds; j++) {
            if (pfds[j].fd == ctx->ctrl_conns[i].fd) {
                pidx = j;
                break;
            }
        }
        if (pidx < 0 || !(pfds[pidx].revents & POLLIN)) {
            continue;
        }

        for (;;) {
            char chunk[256];
            ssize_t n;

            n = read(ctx->ctrl_conns[i].fd, chunk, sizeof(chunk));
            if (n < 0) {
                break;
            }
            if (n == 0) {
                close(ctx->ctrl_conns[i].fd);
                ctx->ctrl_conns[i].fd = -1;
                free(ctx->ctrl_conns[i].buf);
                ctx->ctrl_conns[i].buf = NULL;
                ctx->ctrl_conns[i].used = 0;
                ctx->ctrl_conns[i].cap = 0;
                break;
            }

            {
                size_t offset;

                offset = 0;
                while ((size_t)n > offset) {
                    char *nl;

                    nl = (char *)memchr(chunk + offset, '\n', (size_t)n - offset);
                    if (!nl) {
                        size_t avail;
                        size_t new_cap;
                        char *grown;

                        avail = (size_t)n - offset;
                        if (ctx->ctrl_conns[i].used + avail + 1 > ctx->ctrl_conns[i].cap) {
                            new_cap = ctx->ctrl_conns[i].cap ? ctx->ctrl_conns[i].cap * 2 : 256;
                            while (new_cap < ctx->ctrl_conns[i].used + avail + 1) {
                                new_cap *= 2;
                            }
                            grown = (char *)realloc(ctx->ctrl_conns[i].buf, new_cap);
                            if (!grown) {
                                break;
                            }
                            ctx->ctrl_conns[i].buf = grown;
                            ctx->ctrl_conns[i].cap = new_cap;
                        }
                        memcpy(ctx->ctrl_conns[i].buf + ctx->ctrl_conns[i].used, chunk + offset, avail);
                        ctx->ctrl_conns[i].used += avail;
                        ctx->ctrl_conns[i].buf[ctx->ctrl_conns[i].used] = '\0';
                        offset = (size_t)n;
                    } else {
                        size_t line_len;
                        size_t total;
                        size_t new_cap;
                        char *grown;

                        line_len = (size_t)(nl - (chunk + offset));
                        total = ctx->ctrl_conns[i].used + line_len;
                        if (total + 1 > ctx->ctrl_conns[i].cap) {
                            new_cap = ctx->ctrl_conns[i].cap ? ctx->ctrl_conns[i].cap * 2 : 256;
                            while (new_cap < total + 1) {
                                new_cap *= 2;
                            }
                            grown = (char *)realloc(ctx->ctrl_conns[i].buf, new_cap);
                            if (!grown) {
                                break;
                            }
                            ctx->ctrl_conns[i].buf = grown;
                            ctx->ctrl_conns[i].cap = new_cap;
                        }
                        if (line_len > 0) {
                            memcpy(ctx->ctrl_conns[i].buf + ctx->ctrl_conns[i].used,
                                chunk + offset, line_len);
                        }
                        ctx->ctrl_conns[i].buf[total] = '\0';
                        if (kc_nets_ctrl_dispatch(ctx, ctx->ctrl_conns[i].fd,
                                ctx->ctrl_conns[i].buf) < 0) {
                            return KC_NETS_ENET;
                        }
                        handled++;
                        ctx->ctrl_conns[i].used = 0;
                        offset += line_len + 1;
                    }
                }
            }
        }
    }

    {
        int write_idx;

        write_idx = 0;
        for (i = 0; i < ctx->n_ctrl_conns; i++) {
            if (ctx->ctrl_conns[i].fd >= 0) {
                if (write_idx != i) {
                    ctx->ctrl_conns[write_idx] = ctx->ctrl_conns[i];
                }
                write_idx++;
            }
        }
        ctx->n_ctrl_conns = write_idx;
    }

    return handled;
#else
    (void)ctx;
    return 0;
#endif
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
 * @param host Original hostname (for TLS SNI).
 * @return KC_NETS_OK on success, or a negative error code.
 */
static int kc_nets_send_addr(
kc_nets_t *ctx,
const struct addrinfo *ai,
int proto,
const void *data,
size_t size,
const char *host
) {
    kc_nets_socket_t sock;
    int type = proto == KC_NETS_UDP ? SOCK_DGRAM : SOCK_STREAM;
    int rc;
#ifndef KC_NETS_OPENSSL
    (void)host;
#endif

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

#ifdef KC_NETS_OPENSSL
    if (proto == KC_NETS_TLS) {
        SSL *ssl;
        size_t sent;
        int n;
        char buf[65536];

        ssl = SSL_new(kc_nets_ssl_ctx());
        if (!ssl) { kc_nets_close_sock(sock); return KC_NETS_ENET; }
        SSL_set_fd(ssl, (int)sock);
        SSL_set_tlsext_host_name(ssl, host);
        if (SSL_connect(ssl) != 1) {
            SSL_free(ssl);
            kc_nets_close_sock(sock);
            return KC_NETS_ENET;
        }

        sent = 0;
        while (sent < size) {
            if (ctx && ctx->stop_requested) {
                SSL_free(ssl);
                kc_nets_close_sock(sock);
                return KC_NETS_ESTOP;
            }
            n = SSL_write(ssl, (const char *)data + sent, (int)(size - sent));
            if (n <= 0) {
                SSL_free(ssl);
                kc_nets_close_sock(sock);
                return KC_NETS_ENET;
            }
            sent += (size_t)n;
        }

        SSL_shutdown(ssl);
        while ((n = SSL_read(ssl, buf, sizeof(buf))) > 0) {
            if (fwrite(buf, 1, (size_t)n, stdout) != (size_t)n) break;
        }
        fflush(stdout);
        SSL_free(ssl);
        kc_nets_close_sock(sock);
        return KC_NETS_OK;
    }
#endif

    rc = kc_nets_send_all(ctx, sock, (const char *)data, size);
    if (rc != KC_NETS_OK) {
        kc_nets_close_sock(sock);
        return rc;
    }
#ifndef _WIN32
    shutdown(sock, SHUT_WR);
    for (;;) {
        char buf[65536];
        ssize_t n = read(sock, buf, sizeof(buf));
        if (n <= 0) break;
        if (fwrite(buf, 1, (size_t)n, stdout) != (size_t)n) break;
    }
    fflush(stdout);
#endif
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

    if (!ctx || !host || !host[0] || !data ||
        (proto != KC_NETS_TCP && proto != KC_NETS_UDP && proto != KC_NETS_TLS)) {
        return KC_NETS_EINVAL;
    }
#ifndef KC_NETS_OPENSSL
    if (proto == KC_NETS_TLS) return KC_NETS_ENET;
#endif
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
        rc = kc_nets_send_addr(ctx, ai, proto, data, size, host);
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

/**
 * Check if TLS support is compiled in.
 * @return 1 if TLS is available, 0 otherwise.
 */
int kc_nets_tls_available(void) {
#ifdef KC_NETS_OPENSSL
    return 1;
#else
    return 0;
#endif
}
