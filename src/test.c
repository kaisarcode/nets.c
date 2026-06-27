/**
 * test.c - libnets portable contract tests.
 * Summary: Validates exported libnets behavior through the public C API.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "nets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <process.h>
#include <winsock2.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#define KC_TEST_HOST "127.0.0.1"

#ifdef _WIN32
typedef SOCKET kc_socket_t;
typedef HANDLE kc_thread_t;
#define KC_BAD_SOCKET INVALID_SOCKET
#else
typedef int kc_socket_t;
typedef pthread_t kc_thread_t;
#define KC_BAD_SOCKET -1
#endif

typedef struct {
    unsigned short port;
    int proto;
    char received[8192];
    size_t received_size;
    int result;
    kc_thread_t thread;
} kc_server_t;

static int signal_count = 0;
static int signal_count_b = 0;
static kc_nets_t *signal_ctx_seen = NULL;

/**
 * Stores one observed signal callback.
 * @param ctx Context passed by the library.
 * @return None.
 */
static void count_signal(kc_nets_t *ctx) {
    if (ctx != NULL) {
        signal_count++;
        signal_ctx_seen = ctx;
    }
}

/**
 * Stores one observed secondary signal callback.
 * @param ctx Context passed by the library.
 * @return None.
 */
static void count_signal_b(kc_nets_t *ctx) {
    if (ctx != NULL) {
        signal_count_b++;
        signal_ctx_seen = ctx;
    }
}

/**
 * Returns a process-specific base port.
 * @return Port base.
 */
static unsigned short port_base(void) {
#ifdef _WIN32
    return (unsigned short)(25000UL + ((unsigned long)_getpid() % 20000UL));
#else
    return (unsigned short)(25000UL + ((unsigned long)getpid() % 20000UL));
#endif
}

/**
 * Sleeps for a bounded number of milliseconds.
 * @param ms Milliseconds to sleep.
 * @return None.
 */
static void sleep_ms(unsigned int ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    struct timespec ts;

    ts.tv_sec = (time_t)(ms / 1000U);
    ts.tv_nsec = (long)(ms % 1000U) * 1000000L;
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
    }
#endif
}

/**
 * Initializes process socket state for test-owned sockets.
 * @return 0 on success, 1 on failure.
 */
static int socket_start(void) {
#ifdef _WIN32
    WSADATA data;

    return WSAStartup(MAKEWORD(2, 2), &data) == 0 ? 0 : 1;
#else
    signal(SIGPIPE, SIG_IGN);
    return 0;
#endif
}

/**
 * Stops process socket state for test-owned sockets.
 * @return 0 on success.
 */
static int socket_stop(void) {
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}

/**
 * Closes one socket.
 * @param fd Socket descriptor.
 * @return 0 on success.
 */
static int socket_close(kc_socket_t fd) {
#ifdef _WIN32
    return closesocket(fd) == 0 ? 0 : 1;
#else
    return close(fd) == 0 ? 0 : 1;
#endif
}

/**
 * Sets a receive timeout on one socket.
 * @param fd Socket descriptor.
 * @param ms Timeout in milliseconds.
 * @return None.
 */
static void socket_timeout(kc_socket_t fd, unsigned int ms) {
#ifdef _WIN32
    DWORD tv = (DWORD)ms;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
#else
    struct timeval tv;

    tv.tv_sec = (time_t)(ms / 1000U);
    tv.tv_usec = (suseconds_t)(ms % 1000U) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
#endif
}

/**
 * Enables local socket reuse.
 * @param fd Socket descriptor.
 * @return None.
 */
static void socket_reuse(kc_socket_t fd) {
    int one;

    one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof(one));
}

/**
 * Verifies one integer result.
 * @param name Check name.
 * @param expected Expected value.
 * @param actual Actual value.
 * @return 0 on success, 1 on failure.
 */
static int expect_int(const char *name, int expected, int actual) {
    if (expected != actual) {
        fprintf(stderr, "%s: expected %d, got %d\n", name, expected, actual);
        return 1;
    }
    return 0;
}

/**
 * Verifies one boolean result.
 * @param name Check name.
 * @param condition Condition expected to be true.
 * @return 0 on success, 1 on failure.
 */
static int expect_true(const char *name, int condition) {
    if (!condition) {
        fprintf(stderr, "%s: expected true, got false\n", name);
        return 1;
    }
    return 0;
}

/**
 * Verifies one string result.
 * @param name Check name.
 * @param expected Expected string.
 * @param actual Actual string.
 * @return 0 on success, 1 on failure.
 */
static int expect_string(const char *name, const char *expected, const char *actual) {
    if (actual == NULL || strcmp(expected, actual) != 0) {
        fprintf(stderr, "%s: expected '%s', got '%s'\n", name, expected,
            actual != NULL ? actual : "NULL");
        return 1;
    }
    return 0;
}

#ifdef _WIN32
/**
 * Runs one TCP or UDP receiver thread.
 * @param arg Server state pointer.
 * @return Thread return value.
 */
static DWORD WINAPI server_main(void *arg)
#else
/**
 * Runs one TCP or UDP receiver thread.
 * @param arg Server state pointer.
 * @return Thread return value.
 */
static void *server_main(void *arg)
#endif
{
    kc_server_t *server;
    kc_socket_t fd;
    struct sockaddr_in addr;
    int n;

    server = (kc_server_t *)arg;
    fd = socket(AF_INET, server->proto == KC_NETS_UDP ? SOCK_DGRAM : SOCK_STREAM, 0);
    if (fd == KC_BAD_SOCKET) {
        server->result = 1;
#ifdef _WIN32
        return 0;
#else
        return NULL;
#endif
    }
    socket_reuse(fd);
    socket_timeout(fd, 5000U);
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(server->port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(fd, (const struct sockaddr *)&addr, sizeof(addr)) != 0) {
        socket_close(fd);
        server->result = 1;
#ifdef _WIN32
        return 0;
#else
        return NULL;
#endif
    }
    if (server->proto == KC_NETS_TCP) {
        kc_socket_t client;
        if (listen(fd, 1) != 0) {
            socket_close(fd);
            server->result = 1;
#ifdef _WIN32
            return 0;
#else
            return NULL;
#endif
        }
        client = accept(fd, NULL, NULL);
        if (client == KC_BAD_SOCKET) {
            socket_close(fd);
            server->result = 1;
#ifdef _WIN32
            return 0;
#else
            return NULL;
#endif
        }
        socket_timeout(client, 5000U);
        n = (int)recv(client, server->received, (int)sizeof(server->received), 0);
        if (n > 0) server->received_size = (size_t)n;
        socket_close(client);
    } else {
        n = (int)recv(fd, server->received, (int)sizeof(server->received), 0);
        if (n > 0) server->received_size = (size_t)n;
    }
    socket_close(fd);
    server->result = server->received_size > 0 ? 0 : 1;
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

/**
 * Starts one test server thread.
 * @param server Server state.
 * @param proto Protocol.
 * @param port Listen port.
 * @return 0 on success, 1 on failure.
 */
static int server_start(kc_server_t *server, int proto, unsigned short port) {
    memset(server, 0, sizeof(*server));
    server->proto = proto;
    server->port = port;
    server->result = 1;
#ifdef _WIN32
    server->thread = CreateThread(NULL, 0, server_main, server, 0, NULL);
    if (server->thread == NULL) return 1;
#else
    if (pthread_create(&server->thread, NULL, server_main, server) != 0) return 1;
#endif
    sleep_ms(200U);
    return 0;
}

/**
 * Joins one test server thread.
 * @param server Server state.
 * @return 0 on success, 1 on failure.
 */
static int server_join(kc_server_t *server) {
#ifdef _WIN32
    if (WaitForSingleObject(server->thread, 10000U) != WAIT_OBJECT_0) return 1;
    CloseHandle(server->thread);
#else
    if (pthread_join(server->thread, NULL) != 0) return 1;
#endif
    return server->result == 0 ? 0 : 1;
}

/**
 * Verifies options, status strings, and version behavior.
 * @return 0 on success, 1 on failure.
 */
static int case_options_status(void) {
    kc_nets_options_t opts;
    int rc;

    opts = kc_nets_options_default();
    rc = 0;
    rc += expect_int("default reserved is zero", 0, opts.reserved);
    opts.reserved = 7;
    kc_nets_options_load_env(&opts);
    rc += expect_int("load_env keeps reserved unchanged", 7, opts.reserved);
    kc_nets_options_free(&opts);
    kc_nets_options_load_env(NULL);
    kc_nets_options_free(NULL);
    rc += expect_string("strerror OK", "ok", kc_nets_strerror(KC_NETS_OK));
    rc += expect_string("strerror EINVAL", "invalid argument",
        kc_nets_strerror(KC_NETS_EINVAL));
    rc += expect_string("strerror ENET", "network error",
        kc_nets_strerror(KC_NETS_ENET));
    rc += expect_string("strerror ESTOP", "operation stopped",
        kc_nets_strerror(KC_NETS_ESTOP));
    rc += expect_string("strerror unknown", "unknown error", kc_nets_strerror(999));
    rc += expect_true("version returns non-zero build timestamp",
        kc_nets_version() != 0U);
    return rc == 0 ? 0 : 1;
}

/**
 * Verifies context lifecycle, NULL guards, and stop behavior.
 * @return 0 on success, 1 on failure.
 */
static int case_lifecycle(void) {
    kc_nets_options_t opts;
    kc_nets_t *ctx;
    int rc;

    opts = kc_nets_options_default();
    ctx = NULL;
    rc = 0;
    rc += expect_int("open(NULL)", KC_NETS_EINVAL, kc_nets_open(NULL, &opts));
    rc += expect_int("open(out, NULL)", KC_NETS_EINVAL, kc_nets_open(&ctx, NULL));
    rc += expect_int("open(out, opts)", KC_NETS_OK, kc_nets_open(&ctx, &opts));
    rc += expect_true("open sets context", ctx != NULL);
    rc += expect_int("stop(NULL)", KC_NETS_EINVAL, kc_nets_stop(NULL));
    rc += expect_int("stop(ctx)", KC_NETS_OK, kc_nets_stop(ctx));
    rc += expect_int("stop(ctx) repeated", KC_NETS_OK, kc_nets_stop(ctx));
    rc += expect_int("close(NULL)", KC_NETS_OK, kc_nets_close(NULL));
    rc += expect_int("close(ctx)", KC_NETS_OK, kc_nets_close(ctx));
    return rc == 0 ? 0 : 1;
}

/**
 * Verifies signal registration, replacement, growth, and listener dispatch.
 * @return 0 on success, 1 on failure.
 */
static int case_signal(void) {
    kc_nets_options_t opts;
    kc_nets_t *ctx;
    kc_nets_t *second;
    int rc;
    int i;

    opts = kc_nets_options_default();
    ctx = NULL;
    second = NULL;
    rc = 0;
    signal_count = 0;
    signal_count_b = 0;
    signal_ctx_seen = NULL;
    rc += expect_int("on_signal(NULL)", KC_NETS_EINVAL,
        kc_nets_on_signal(NULL, 10, count_signal));
    rc += expect_int("raise_signal(NULL)", KC_NETS_EINVAL,
        kc_nets_raise_signal(NULL, 10));
    rc += expect_int("listen_signals(NULL)", KC_NETS_EINVAL,
        kc_nets_listen_signals(NULL));
    rc += expect_int("listen_signal(NULL)", KC_NETS_EINVAL,
        kc_nets_listen_signal(NULL, 10));
    rc += expect_int("open", KC_NETS_OK, kc_nets_open(&ctx, &opts));
    rc += expect_int("raise unhandled", KC_NETS_EINVAL,
        kc_nets_raise_signal(ctx, 10));
    rc += expect_int("register handler", KC_NETS_OK,
        kc_nets_on_signal(ctx, 10, count_signal));
    rc += expect_int("raise handled", KC_NETS_OK,
        kc_nets_raise_signal(ctx, 10));
    rc += expect_int("callback count", 1, signal_count);
    rc += expect_true("callback saw ctx", signal_ctx_seen == ctx);
    rc += expect_int("replace handler", KC_NETS_OK,
        kc_nets_on_signal(ctx, 10, count_signal_b));
    signal_count = 0;
    signal_count_b = 0;
    rc += expect_int("raise replaced", KC_NETS_OK, kc_nets_raise_signal(ctx, 10));
    rc += expect_int("old handler not called", 0, signal_count);
    rc += expect_int("new handler called", 1, signal_count_b);
    rc += expect_int("remove handler", KC_NETS_OK,
        kc_nets_on_signal(ctx, 10, NULL));
    rc += expect_int("raise removed", KC_NETS_EINVAL, kc_nets_raise_signal(ctx, 10));
    rc += expect_int("remove absent handler", KC_NETS_OK,
        kc_nets_on_signal(ctx, 10, NULL));
    for (i = 0; i < 10; i++) {
        rc += expect_int("register growth handler", KC_NETS_OK,
            kc_nets_on_signal(ctx, 100 + i, count_signal));
    }
    signal_count = 0;
    rc += expect_int("raise growth handler", KC_NETS_OK,
        kc_nets_raise_signal(ctx, 109));
    rc += expect_int("growth callback count", 1, signal_count);
    rc += expect_int("listen signals", KC_NETS_OK, kc_nets_listen_signals(ctx));
    rc += expect_int("listen one signal", KC_NETS_OK, kc_nets_listen_signal(ctx, 12));
    rc += expect_int("open second", KC_NETS_OK, kc_nets_open(&second, &opts));
    rc += expect_int("second listens", KC_NETS_OK, kc_nets_listen_signals(second));
    rc += expect_int("second handler", KC_NETS_OK,
        kc_nets_on_signal(second, 77, count_signal_b));
    signal_count_b = 0;
    signal_ctx_seen = NULL;
    kc_nets_signal_listener(77);
    rc += expect_int("listener dispatched", 1, signal_count_b);
    rc += expect_true("listener saw second ctx", signal_ctx_seen == second);
    kc_nets_close(ctx);
    kc_nets_close(second);
    return rc == 0 ? 0 : 1;
}

/**
 * Verifies send input guards and stopped-context behavior.
 * @return 0 on success, 1 on failure.
 */
static int case_send_guards(void) {
    kc_nets_options_t opts;
    kc_nets_t *ctx;
    int rc;

    opts = kc_nets_options_default();
    ctx = NULL;
    rc = 0;
    rc += expect_int("send(NULL ctx)", KC_NETS_EINVAL,
        kc_nets_send(NULL, KC_TEST_HOST, 9, KC_NETS_TCP, "x", 1));
    rc += expect_int("open", KC_NETS_OK, kc_nets_open(&ctx, &opts));
    rc += expect_int("send(NULL host)", KC_NETS_EINVAL,
        kc_nets_send(ctx, NULL, 9, KC_NETS_TCP, "x", 1));
    rc += expect_int("send(empty host)", KC_NETS_EINVAL,
        kc_nets_send(ctx, "", 9, KC_NETS_TCP, "x", 1));
    rc += expect_int("send(NULL data)", KC_NETS_EINVAL,
        kc_nets_send(ctx, KC_TEST_HOST, 9, KC_NETS_TCP, NULL, 1));
    rc += expect_int("send(invalid proto)", KC_NETS_EINVAL,
        kc_nets_send(ctx, KC_TEST_HOST, 9, 999, "x", 1));
    rc += expect_int("send(unresolvable host)", KC_NETS_ENET,
        kc_nets_send(ctx, "invalid.invalid", 9, KC_NETS_TCP, "x", 1));
    rc += expect_int("stop", KC_NETS_OK, kc_nets_stop(ctx));
    rc += expect_int("send stopped", KC_NETS_ESTOP,
        kc_nets_send(ctx, KC_TEST_HOST, 9, KC_NETS_TCP, "x", 1));
    kc_nets_close(ctx);
    return rc == 0 ? 0 : 1;
}

/**
 * Verifies TCP payload delivery through kc_nets_send.
 * @return 0 on success, 1 on failure.
 */
static int case_tcp_send(void) {
    kc_nets_options_t opts;
    kc_nets_t *ctx;
    kc_server_t server;
    unsigned short port;
    int rc;

    opts = kc_nets_options_default();
    ctx = NULL;
    port = (unsigned short)(port_base() + 1U);
    rc = 0;
    if (server_start(&server, KC_NETS_TCP, port) != 0) return 1;
    rc += expect_int("open", KC_NETS_OK, kc_nets_open(&ctx, &opts));
    rc += expect_int("tcp send", KC_NETS_OK,
        kc_nets_send(ctx, KC_TEST_HOST, port, KC_NETS_TCP, "hello tcp", 9));
    rc += expect_int("tcp server join", 0, server_join(&server));
    rc += expect_true("tcp payload size", server.received_size == 9U);
    rc += expect_true("tcp payload bytes",
        memcmp(server.received, "hello tcp", 9) == 0);
    kc_nets_close(ctx);
    return rc == 0 ? 0 : 1;
}

/**
 * Verifies UDP payload delivery through kc_nets_send.
 * @return 0 on success, 1 on failure.
 */
static int case_udp_send(void) {
    kc_nets_options_t opts;
    kc_nets_t *ctx;
    kc_server_t server;
    unsigned short port;
    int rc;

    opts = kc_nets_options_default();
    ctx = NULL;
    port = (unsigned short)(port_base() + 20U);
    rc = 0;
    if (server_start(&server, KC_NETS_UDP, port) != 0) return 1;
    rc += expect_int("open", KC_NETS_OK, kc_nets_open(&ctx, &opts));
    rc += expect_int("udp send", KC_NETS_OK,
        kc_nets_send(ctx, KC_TEST_HOST, port, KC_NETS_UDP, "hello udp", 9));
    rc += expect_int("udp server join", 0, server_join(&server));
    rc += expect_true("udp payload size", server.received_size == 9U);
    rc += expect_true("udp payload bytes",
        memcmp(server.received, "hello udp", 9) == 0);
    kc_nets_close(ctx);
    return rc == 0 ? 0 : 1;
}

/**
 * Verifies stopped context isolation across two contexts.
 * @return 0 on success, 1 on failure.
 */
static int case_multictx_stop(void) {
    kc_nets_options_t opts;
    kc_nets_t *stopped;
    kc_nets_t *active;
    kc_server_t server;
    unsigned short port;
    int rc;

    opts = kc_nets_options_default();
    stopped = NULL;
    active = NULL;
    port = (unsigned short)(port_base() + 40U);
    rc = 0;
    if (server_start(&server, KC_NETS_TCP, port) != 0) return 1;
    rc += expect_int("open stopped", KC_NETS_OK, kc_nets_open(&stopped, &opts));
    rc += expect_int("open active", KC_NETS_OK, kc_nets_open(&active, &opts));
    rc += expect_int("stop stopped", KC_NETS_OK, kc_nets_stop(stopped));
    rc += expect_int("stopped send returns ESTOP", KC_NETS_ESTOP,
        kc_nets_send(stopped, KC_TEST_HOST, port, KC_NETS_TCP, "fail", 4));
    rc += expect_int("active send succeeds", KC_NETS_OK,
        kc_nets_send(active, KC_TEST_HOST, port, KC_NETS_TCP, "hello multi", 11));
    rc += expect_int("server join", 0, server_join(&server));
    rc += expect_true("multi payload size", server.received_size == 11U);
    rc += expect_true("multi payload bytes",
        memcmp(server.received, "hello multi", 11) == 0);
    kc_nets_close(stopped);
    kc_nets_close(active);
    return rc == 0 ? 0 : 1;
}

/**
 * Runs one libnets contract test case.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return 0 on success, 1 or 2 on failure.
 */
int main(int argc, char **argv) {
    int rc;

    if (argc != 2) {
        fprintf(stderr, "test case: expected one argument, got %d\n", argc - 1);
        return 2;
    }
    if (socket_start() != 0) return 1;
    if (strcmp(argv[1], "options-status") == 0) rc = case_options_status();
    else if (strcmp(argv[1], "lifecycle") == 0) rc = case_lifecycle();
    else if (strcmp(argv[1], "signal") == 0) rc = case_signal();
    else if (strcmp(argv[1], "send-guards") == 0) rc = case_send_guards();
    else if (strcmp(argv[1], "tcp-send") == 0) rc = case_tcp_send();
    else if (strcmp(argv[1], "udp-send") == 0) rc = case_udp_send();
    else if (strcmp(argv[1], "multictx-stop") == 0) rc = case_multictx_stop();
    else {
        fprintf(stderr, "unknown test case: %s\n", argv[1]);
        rc = 2;
    }
    socket_stop();
    return rc;
}
