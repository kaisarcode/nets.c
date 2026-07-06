/**
 * test.c - libnets public API contract tests.
 * Summary: Validates each exported nets function through one dedicated test case.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "nets.h"

#include <signal.h>
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
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#endif

#define TEST_HOST "127.0.0.1"

#ifdef _WIN32
typedef SOCKET test_socket_t;
typedef HANDLE test_thread_t;
#define TEST_BAD_SOCKET INVALID_SOCKET
#else
typedef int test_socket_t;
typedef pthread_t test_thread_t;
#define TEST_BAD_SOCKET -1
#endif

typedef struct {
    unsigned short port;
    int proto;
    char received[8192];
    size_t received_size;
    int result;
    test_thread_t thread;
} test_server_t;

static int signal_count;
static int signal_count_b;
static kc_nets_t *signal_ctx_seen;

/**
 * Stores one observed signal callback.
 * @param ctx Context passed by the library.
 * @return None.
 */
static void count_signal(kc_nets_t *ctx) {
    signal_count++;
    signal_ctx_seen = ctx;
}

/**
 * Stores one observed secondary signal callback.
 * @param ctx Context passed by the library.
 * @return None.
 */
static void count_signal_b(kc_nets_t *ctx) {
    signal_count_b++;
    signal_ctx_seen = ctx;
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
static int socket_close(test_socket_t fd) {
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
static void socket_timeout(test_socket_t fd, unsigned int ms) {
#ifdef _WIN32
    DWORD tv;

    tv = (DWORD)ms;
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
static void socket_reuse(test_socket_t fd) {
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

/**
 * Sets one environment variable for the current process.
 * @param name Variable name.
 * @param value Variable value.
 * @return 0 on success, 1 on failure.
 */
static int test_env_set(const char *name, const char *value) {
#ifdef _WIN32
    return _putenv_s(name, value) == 0 ? 0 : 1;
#else
    return setenv(name, value, 1) == 0 ? 0 : 1;
#endif
}

/**
 * Clears one environment variable for the current process.
 * @param name Variable name.
 * @return 0 on success, 1 on failure.
 */
static int test_env_unset(const char *name) {
#ifdef _WIN32
    return _putenv_s(name, "") == 0 ? 0 : 1;
#else
    return unsetenv(name) == 0 ? 0 : 1;
#endif
}

#ifndef _WIN32

/**
 * Connects one client to a Unix control socket.
 * @param sock_path Socket path.
 * @return Connected file descriptor, or -1 on failure.
 */
static int ctrl_connect(const char *sock_path) {
    struct sockaddr_un addr;
    int client_fd;

    client_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (client_fd < 0) {
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);
    if (connect(client_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(client_fd);
        return -1;
    }

    return client_fd;
}

/**
 * Exchanges one line with the control socket.
 * @param ctx Context handle.
 * @param client_fd Connected client descriptor.
 * @param request Request line.
 * @param response Response buffer.
 * @param response_cap Response buffer capacity.
 * @return Response byte count, or -1 on failure.
 */
static ssize_t ctrl_roundtrip(
kc_nets_t *ctx,
int client_fd,
const char *request,
char *response,
size_t response_cap
) {
    size_t request_len;
    ssize_t n;

    request_len = strlen(request);
    kc_nets_ctrl_poll(ctx);
    if (write(client_fd, request, request_len) != (ssize_t)request_len) {
        return -1;
    }
    kc_nets_ctrl_poll(ctx);
    n = read(client_fd, response, response_cap - 1);
    if (n >= 0) {
        response[n] = '\0';
    }
    return n;
}

#endif

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
    test_server_t *server;
    test_socket_t fd;
    struct sockaddr_in addr;
    int n;

    server = (test_server_t *)arg;
    fd = socket(AF_INET, server->proto == KC_NETS_UDP ? SOCK_DGRAM : SOCK_STREAM, 0);
    if (fd == TEST_BAD_SOCKET) {
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
        test_socket_t client;

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
        if (client == TEST_BAD_SOCKET) {
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
static int server_start(test_server_t *server, int proto, unsigned short port) {
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
static int server_join(test_server_t *server) {
#ifdef _WIN32
    if (WaitForSingleObject(server->thread, 10000U) != WAIT_OBJECT_0) return 1;
    CloseHandle(server->thread);
#else
    if (pthread_join(server->thread, NULL) != 0) return 1;
#endif
    return server->result == 0 ? 0 : 1;
}

/**
 * Tests kc_nets_options_default.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_nets_options_default(void) {
    kc_nets_options_t opts;
    int rc;

    opts = kc_nets_options_default();
    rc = 0;
    rc += expect_int("default reserved is zero", 0, opts.reserved);
    rc += expect_true("default ctrl_path is NULL", opts.ctrl_path == NULL);
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_nets_options_load_env.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_nets_options_load_env(void) {
    kc_nets_options_t opts;
    int rc;

    opts = kc_nets_options_default();
    opts.reserved = 7;
    rc = 0;
    rc += expect_int("set KC_NETS_CTRL", 0, test_env_set("KC_NETS_CTRL", "/tmp/nets_env.sock"));
    kc_nets_options_load_env(&opts);
    kc_nets_options_load_env(NULL);
    rc += expect_int("load_env keeps reserved unchanged", 7, opts.reserved);
    rc += expect_string("load_env reads ctrl path", "/tmp/nets_env.sock", opts.ctrl_path);
    rc += expect_int("unset KC_NETS_CTRL", 0, test_env_unset("KC_NETS_CTRL"));
    kc_nets_options_free(&opts);
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_nets_options_free.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_nets_options_free(void) {
    kc_nets_options_t opts;
    int rc;

    opts = kc_nets_options_default();
    opts.ctrl_path = strdup("/tmp/nets_free.sock");
    opts.reserved = 9;
    rc = 0;
    kc_nets_options_free(&opts);
    kc_nets_options_free(NULL);
    rc += expect_int("options remain reusable after free", 9, opts.reserved);
    rc += expect_true("options free clears ctrl_path", opts.ctrl_path == NULL);
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_nets_open.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_nets_open(void) {
    kc_nets_options_t opts;
    kc_nets_t *ctx;
    int rc;

    opts = kc_nets_options_default();
    ctx = NULL;
    rc = 0;
    rc += expect_int("open NULL out", KC_NETS_EINVAL, kc_nets_open(NULL, &opts));
    rc += expect_int("open NULL opts", KC_NETS_EINVAL, kc_nets_open(&ctx, NULL));
    rc += expect_int("open valid context", KC_NETS_OK, kc_nets_open(&ctx, &opts));
    rc += expect_true("open sets context", ctx != NULL);
    kc_nets_close(ctx);
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_nets_close.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_nets_close(void) {
    kc_nets_options_t opts;
    kc_nets_t *ctx;
    int rc;

    opts = kc_nets_options_default();
    ctx = NULL;
    rc = 0;
    rc += expect_int("open before close", KC_NETS_OK, kc_nets_open(&ctx, &opts));
    rc += expect_int("listen before close", KC_NETS_OK, kc_nets_listen_signals(ctx));
    rc += expect_int("close NULL", KC_NETS_OK, kc_nets_close(NULL));
    rc += expect_int("close context", KC_NETS_OK, kc_nets_close(ctx));
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_nets_stop.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_nets_stop(void) {
    kc_nets_options_t opts;
    kc_nets_t *ctx;
    int rc;

    opts = kc_nets_options_default();
    ctx = NULL;
    rc = 0;
    rc += expect_int("stop NULL", KC_NETS_EINVAL, kc_nets_stop(NULL));
    rc += expect_int("open before stop", KC_NETS_OK, kc_nets_open(&ctx, &opts));
    rc += expect_int("stop context", KC_NETS_OK, kc_nets_stop(ctx));
    rc += expect_int("stop context repeated", KC_NETS_OK, kc_nets_stop(ctx));
    kc_nets_close(ctx);
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_nets_on_signal.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_nets_on_signal(void) {
    kc_nets_options_t opts;
    kc_nets_t *ctx;
    int rc;

    opts = kc_nets_options_default();
    ctx = NULL;
    rc = 0;
    signal_count = 0;
    signal_count_b = 0;
    rc += expect_int("on_signal NULL ctx", KC_NETS_EINVAL,
        kc_nets_on_signal(NULL, 10, count_signal));
    rc += expect_int("open for on_signal", KC_NETS_OK, kc_nets_open(&ctx, &opts));
    rc += expect_int("register first handler", KC_NETS_OK,
        kc_nets_on_signal(ctx, 10, count_signal));
    rc += expect_int("replace handler", KC_NETS_OK,
        kc_nets_on_signal(ctx, 10, count_signal_b));
    rc += expect_int("raise replaced signal", KC_NETS_OK,
        kc_nets_raise_signal(ctx, 10));
    rc += expect_int("old handler not called", 0, signal_count);
    rc += expect_int("new handler called", 1, signal_count_b);
    rc += expect_int("remove handler", KC_NETS_OK,
        kc_nets_on_signal(ctx, 10, NULL));
    rc += expect_int("remove absent handler", KC_NETS_OK,
        kc_nets_on_signal(ctx, 10, NULL));
    kc_nets_close(ctx);
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_nets_raise_signal.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_nets_raise_signal(void) {
    kc_nets_options_t opts;
    kc_nets_t *ctx;
    int rc;

    opts = kc_nets_options_default();
    ctx = NULL;
    rc = 0;
    signal_count = 0;
    signal_ctx_seen = NULL;
    rc += expect_int("raise NULL ctx", KC_NETS_EINVAL,
        kc_nets_raise_signal(NULL, 10));
    rc += expect_int("open for raise_signal", KC_NETS_OK, kc_nets_open(&ctx, &opts));
    rc += expect_int("raise unhandled signal", KC_NETS_EINVAL,
        kc_nets_raise_signal(ctx, 10));
    rc += expect_int("register signal handler", KC_NETS_OK,
        kc_nets_on_signal(ctx, 10, count_signal));
    rc += expect_int("raise handled signal", KC_NETS_OK,
        kc_nets_raise_signal(ctx, 10));
    rc += expect_int("callback count", 1, signal_count);
    rc += expect_true("callback saw context", signal_ctx_seen == ctx);
    kc_nets_close(ctx);
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_nets_listen_signals.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_nets_listen_signals(void) {
    kc_nets_options_t opts;
    kc_nets_t *ctx;
    int rc;

    opts = kc_nets_options_default();
    ctx = NULL;
    rc = 0;
    rc += expect_int("listen_signals NULL ctx", KC_NETS_EINVAL,
        kc_nets_listen_signals(NULL));
    rc += expect_int("open for listen_signals", KC_NETS_OK, kc_nets_open(&ctx, &opts));
    rc += expect_int("listen signals context", KC_NETS_OK,
        kc_nets_listen_signals(ctx));
    kc_nets_close(ctx);
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_nets_listen_signal.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_nets_listen_signal(void) {
    kc_nets_options_t opts;
    kc_nets_t *ctx;
    int rc;

    opts = kc_nets_options_default();
    ctx = NULL;
    rc = 0;
    rc += expect_int("listen_signal NULL ctx", KC_NETS_EINVAL,
        kc_nets_listen_signal(NULL, SIGINT));
    rc += expect_int("open for listen_signal", KC_NETS_OK, kc_nets_open(&ctx, &opts));
    rc += expect_int("listen one signal", KC_NETS_OK,
        kc_nets_listen_signal(ctx, SIGINT));
    kc_nets_close(ctx);
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_nets_signal_listener.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_nets_signal_listener(void) {
    kc_nets_options_t opts;
    kc_nets_t *first;
    kc_nets_t *second;
    int rc;

    opts = kc_nets_options_default();
    first = NULL;
    second = NULL;
    rc = 0;
    signal_count_b = 0;
    signal_ctx_seen = NULL;
    rc += expect_int("open first", KC_NETS_OK, kc_nets_open(&first, &opts));
    rc += expect_int("open second", KC_NETS_OK, kc_nets_open(&second, &opts));
    rc += expect_int("first listens globally", KC_NETS_OK,
        kc_nets_listen_signals(first));
    rc += expect_int("second listens globally", KC_NETS_OK,
        kc_nets_listen_signals(second));
    rc += expect_int("second handler", KC_NETS_OK,
        kc_nets_on_signal(second, 77, count_signal_b));
    kc_nets_signal_listener(77);
    rc += expect_int("listener dispatched", 1, signal_count_b);
    rc += expect_true("listener saw second ctx", signal_ctx_seen == second);
    kc_nets_close(first);
    kc_nets_close(second);
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_nets_send.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_nets_send(void) {
    kc_nets_options_t opts;
    kc_nets_t *ctx;
    kc_nets_t *stopped;
    test_server_t server;
    unsigned short tcp_port;
    unsigned short udp_port;
    int rc;

    opts = kc_nets_options_default();
    ctx = NULL;
    stopped = NULL;
    tcp_port = (unsigned short)(port_base() + 1U);
    udp_port = (unsigned short)(port_base() + 20U);
    rc = 0;
    rc += expect_int("send NULL ctx", KC_NETS_EINVAL,
        kc_nets_send(NULL, TEST_HOST, 9, KC_NETS_TCP, "x", 1));
    rc += expect_int("open for send", KC_NETS_OK, kc_nets_open(&ctx, &opts));
    rc += expect_int("send NULL host", KC_NETS_EINVAL,
        kc_nets_send(ctx, NULL, 9, KC_NETS_TCP, "x", 1));
    rc += expect_int("send empty host", KC_NETS_EINVAL,
        kc_nets_send(ctx, "", 9, KC_NETS_TCP, "x", 1));
    rc += expect_int("send NULL data", KC_NETS_EINVAL,
        kc_nets_send(ctx, TEST_HOST, 9, KC_NETS_TCP, NULL, 1));
    rc += expect_int("send invalid proto", KC_NETS_EINVAL,
        kc_nets_send(ctx, TEST_HOST, 9, 999, "x", 1));
    rc += expect_int("send unresolvable host", KC_NETS_ENET,
        kc_nets_send(ctx, "invalid.invalid", 9, KC_NETS_TCP, "x", 1));
    rc += expect_int("open stopped context", KC_NETS_OK,
        kc_nets_open(&stopped, &opts));
    rc += expect_int("stop context", KC_NETS_OK, kc_nets_stop(stopped));
    rc += expect_int("send stopped context", KC_NETS_ESTOP,
        kc_nets_send(stopped, TEST_HOST, 9, KC_NETS_TCP, "x", 1));
    kc_nets_close(stopped);

    if (server_start(&server, KC_NETS_TCP, tcp_port) != 0) return 1;
    rc += expect_int("tcp send", KC_NETS_OK,
        kc_nets_send(ctx, TEST_HOST, tcp_port, KC_NETS_TCP, "hello tcp", 9));
    rc += expect_int("tcp server join", 0, server_join(&server));
    rc += expect_true("tcp payload size", server.received_size == 9U);
    rc += expect_true("tcp payload bytes",
        memcmp(server.received, "hello tcp", 9) == 0);

    if (server_start(&server, KC_NETS_UDP, udp_port) != 0) return 1;
    rc += expect_int("udp send", KC_NETS_OK,
        kc_nets_send(ctx, TEST_HOST, udp_port, KC_NETS_UDP, "hello udp", 9));
    rc += expect_int("udp server join", 0, server_join(&server));
    rc += expect_true("udp payload size", server.received_size == 9U);
    rc += expect_true("udp payload bytes",
        memcmp(server.received, "hello udp", 9) == 0);

    kc_nets_close(ctx);
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_nets control HELP command.
 * @return 0 on success, 1 on failure.
 */
static int case_ctrl_help(void) {
#ifndef _WIN32
    kc_nets_options_t opts;
    kc_nets_t *ctx;
    char sock_path[128];
    int client_fd;
    char buf[256];
    ssize_t n;
    int rc;

    snprintf(sock_path, sizeof(sock_path), "/tmp/nets_ctrl_help_%d.sock", (int)getpid());
    unlink(sock_path);
    opts = kc_nets_options_default();
    ctx = NULL;
    rc = 0;
    if (kc_nets_open(&ctx, &opts) != KC_NETS_OK) {
        return 1;
    }
    kc_nets_options_free(&opts);
    if (kc_nets_ctrl_open(ctx, sock_path) != KC_NETS_OK) {
        kc_nets_close(ctx);
        unlink(sock_path);
        return 1;
    }
    client_fd = ctrl_connect(sock_path);
    if (client_fd < 0) {
        kc_nets_close(ctx);
        unlink(sock_path);
        return 1;
    }
    n = ctrl_roundtrip(ctx, client_fd, "HELP\n", buf, sizeof(buf));
    rc += expect_true("ctrl help returns OK", n > 0 && strncmp(buf, "OK ", 3) == 0);
    rc += expect_true("ctrl help lists HELP", n > 0 && strstr(buf, "HELP") != NULL);
    close(client_fd);
    kc_nets_close(ctx);
    unlink(sock_path);
    return rc == 0 ? 0 : 1;
#else
    return expect_true("ctrl test is skipped on Windows", 1);
#endif
}

/**
 * Tests kc_nets control STOP command.
 * @return 0 on success, 1 on failure.
 */
static int case_ctrl_stop(void) {
#ifndef _WIN32
    kc_nets_options_t opts;
    kc_nets_t *ctx;
    char sock_path[128];
    int client_fd;
    char buf[256];
    ssize_t n;
    int rc;

    snprintf(sock_path, sizeof(sock_path), "/tmp/nets_ctrl_stop_%d.sock", (int)getpid());
    unlink(sock_path);
    opts = kc_nets_options_default();
    ctx = NULL;
    rc = 0;
    if (kc_nets_open(&ctx, &opts) != KC_NETS_OK) {
        return 1;
    }
    kc_nets_options_free(&opts);
    if (kc_nets_ctrl_open(ctx, sock_path) != KC_NETS_OK) {
        kc_nets_close(ctx);
        unlink(sock_path);
        return 1;
    }
    client_fd = ctrl_connect(sock_path);
    if (client_fd < 0) {
        kc_nets_close(ctx);
        unlink(sock_path);
        return 1;
    }
    n = ctrl_roundtrip(ctx, client_fd, "STOP\n", buf, sizeof(buf));
    rc += expect_true("ctrl stop returns OK", n > 0 && strcmp(buf, "OK\n") == 0);
    rc += expect_int("ctrl stop sets flag", 1, kc_nets_stop_requested(ctx));
    close(client_fd);
    kc_nets_close(ctx);
    unlink(sock_path);
    return rc == 0 ? 0 : 1;
#else
    return expect_true("ctrl test is skipped on Windows", 1);
#endif
}

/**
 * Tests kc_nets control GET command.
 * @return 0 on success, 1 on failure.
 */
static int case_ctrl_get(void) {
#ifndef _WIN32
    kc_nets_options_t opts;
    kc_nets_t *ctx;
    char sock_path[128];
    int client_fd;
    char buf[256];
    ssize_t n;
    int rc;

    snprintf(sock_path, sizeof(sock_path), "/tmp/nets_ctrl_get_%d.sock", (int)getpid());
    unlink(sock_path);
    opts = kc_nets_options_default();
    opts.ctrl_path = strdup(sock_path);
    opts.reserved = 17;
    ctx = NULL;
    rc = 0;
    if (kc_nets_open(&ctx, &opts) != KC_NETS_OK) {
        kc_nets_options_free(&opts);
        return 1;
    }
    kc_nets_options_free(&opts);
    if (kc_nets_ctrl_open(ctx, sock_path) != KC_NETS_OK) {
        kc_nets_close(ctx);
        unlink(sock_path);
        return 1;
    }
    client_fd = ctrl_connect(sock_path);
    if (client_fd < 0) {
        kc_nets_close(ctx);
        unlink(sock_path);
        return 1;
    }
    n = ctrl_roundtrip(ctx, client_fd, "GET ctrl_path\n", buf, sizeof(buf));
    rc += expect_true("ctrl get ctrl_path returns path", n > 0 && strstr(buf, sock_path) != NULL);
    n = ctrl_roundtrip(ctx, client_fd, "GET reserved\n", buf, sizeof(buf));
    rc += expect_true("ctrl get reserved returns value", n > 0 && strcmp(buf, "OK 17\n") == 0);
    close(client_fd);
    kc_nets_close(ctx);
    unlink(sock_path);
    return rc == 0 ? 0 : 1;
#else
    return expect_true("ctrl test is skipped on Windows", 1);
#endif
}

/**
 * Tests kc_nets control SET command.
 * @return 0 on success, 1 on failure.
 */
static int case_ctrl_set(void) {
#ifndef _WIN32
    kc_nets_options_t opts;
    kc_nets_t *ctx;
    char sock_path[128];
    int client_fd;
    char buf[256];
    ssize_t n;
    int rc;

    snprintf(sock_path, sizeof(sock_path), "/tmp/nets_ctrl_set_%d.sock", (int)getpid());
    unlink(sock_path);
    opts = kc_nets_options_default();
    ctx = NULL;
    rc = 0;
    if (kc_nets_open(&ctx, &opts) != KC_NETS_OK) {
        return 1;
    }
    kc_nets_options_free(&opts);
    if (kc_nets_ctrl_open(ctx, sock_path) != KC_NETS_OK) {
        kc_nets_close(ctx);
        unlink(sock_path);
        return 1;
    }
    client_fd = ctrl_connect(sock_path);
    if (client_fd < 0) {
        kc_nets_close(ctx);
        unlink(sock_path);
        return 1;
    }
    n = ctrl_roundtrip(ctx, client_fd, "SET ctrl_path /tmp/nets_new.sock\n", buf, sizeof(buf));
    rc += expect_true("ctrl set ctrl_path returns OK", n > 0 && strcmp(buf, "OK\n") == 0);
    n = ctrl_roundtrip(ctx, client_fd, "GET ctrl_path\n", buf, sizeof(buf));
    rc += expect_true("ctrl set ctrl_path updates value",
        n > 0 && strcmp(buf, "OK /tmp/nets_new.sock\n") == 0);
    n = ctrl_roundtrip(ctx, client_fd, "SET reserved 3\n", buf, sizeof(buf));
    rc += expect_true("ctrl set unknown key returns error",
        n > 0 && strcmp(buf, "ERR unknown key\n") == 0);
    close(client_fd);
    kc_nets_close(ctx);
    unlink(sock_path);
    return rc == 0 ? 0 : 1;
#else
    return expect_true("ctrl test is skipped on Windows", 1);
#endif
}

/**
 * Tests kc_nets_strerror.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_nets_strerror(void) {
    int rc;

    rc = 0;
    rc += expect_string("strerror OK", "ok", kc_nets_strerror(KC_NETS_OK));
    rc += expect_string("strerror EINVAL", "invalid argument",
        kc_nets_strerror(KC_NETS_EINVAL));
    rc += expect_string("strerror ENET", "network error",
        kc_nets_strerror(KC_NETS_ENET));
    rc += expect_string("strerror ESTOP", "operation stopped",
        kc_nets_strerror(KC_NETS_ESTOP));
    rc += expect_string("strerror unknown", "unknown error",
        kc_nets_strerror(999));
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_nets_version.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_nets_version(void) {
    return expect_true("version returns non-zero build timestamp",
        kc_nets_version() != 0U);
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
    if (strcmp(argv[1], "kc_nets_options_default") == 0) rc = case_kc_nets_options_default();
    else if (strcmp(argv[1], "kc_nets_options_load_env") == 0) rc = case_kc_nets_options_load_env();
    else if (strcmp(argv[1], "kc_nets_options_free") == 0) rc = case_kc_nets_options_free();
    else if (strcmp(argv[1], "kc_nets_open") == 0) rc = case_kc_nets_open();
    else if (strcmp(argv[1], "kc_nets_close") == 0) rc = case_kc_nets_close();
    else if (strcmp(argv[1], "kc_nets_stop") == 0) rc = case_kc_nets_stop();
    else if (strcmp(argv[1], "kc_nets_on_signal") == 0) rc = case_kc_nets_on_signal();
    else if (strcmp(argv[1], "kc_nets_raise_signal") == 0) rc = case_kc_nets_raise_signal();
    else if (strcmp(argv[1], "kc_nets_listen_signals") == 0) rc = case_kc_nets_listen_signals();
    else if (strcmp(argv[1], "kc_nets_listen_signal") == 0) rc = case_kc_nets_listen_signal();
    else if (strcmp(argv[1], "kc_nets_signal_listener") == 0) rc = case_kc_nets_signal_listener();
    else if (strcmp(argv[1], "kc_nets_send") == 0) rc = case_kc_nets_send();
    else if (strcmp(argv[1], "kc_nets_strerror") == 0) rc = case_kc_nets_strerror();
    else if (strcmp(argv[1], "kc_nets_version") == 0) rc = case_kc_nets_version();
    else if (strcmp(argv[1], "ctrl-help") == 0) rc = case_ctrl_help();
    else if (strcmp(argv[1], "ctrl-stop") == 0) rc = case_ctrl_stop();
    else if (strcmp(argv[1], "ctrl-get") == 0) rc = case_ctrl_get();
    else if (strcmp(argv[1], "ctrl-set") == 0) rc = case_ctrl_set();
    else {
        fprintf(stderr, "unknown test case: %s\n", argv[1]);
        rc = 2;
    }
    socket_stop();
    return rc;
}
