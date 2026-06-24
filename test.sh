#!/bin/sh
# Summary: Validation suite for nets network sending.
# Author:  KaisarCode
# Website: https://kaisarcode.com
# License: https://www.gnu.org/licenses/gpl-3.0.html

# Prints one failure line.
# @param $1 Failure message.
# @return 1 on failure.
kc_test_fail() {
    printf '\033[31m[FAIL]\033[0m %s\n' "$1"
    return 1
}

# Prints one success line.
# @param $1 Success message.
# @return 0 on success.
kc_test_pass() {
    printf '\033[32m[PASS]\033[0m %s\n' "$1"
}

# Detects the artifact architecture for the current machine.
# @return Architecture name on stdout.
kc_test_arch() {
    case "$(uname -m)" in
        x86_64 | amd64)            printf '%s\n' "x86_64" ;;
        aarch64 | arm64)           printf '%s\n' "aarch64" ;;
        armv7l | armv7)            printf '%s\n' "armv7" ;;
        i386 | i486 | i586 | i686) printf '%s\n' "i686" ;;
        ppc64le | powerpc64le)     printf '%s\n' "powerpc64le" ;;
        *)                         uname -m ;;
    esac
}

# Detects the artifact platform for the current machine.
# @return Platform name on stdout.
kc_test_platform() {
    case "$(uname -s)" in
        Linux) printf '%s\n' "linux" ;;
        *)     uname -s | tr '[:upper:]' '[:lower:]' ;;
    esac
}

# Returns the CLI path for the current architecture and platform.
# @return CLI path on stdout.
kc_test_binary_path() {
    printf './bin/%s/%s/nets\n' "$(kc_test_arch)" "$(kc_test_platform)"
}

# Verifies the binary exists and is executable.
# @return 0 on success, 1 on failure.
kc_test_check_binary() {
    if [ ! -x "$BIN" ]; then
        kc_test_fail "binary existence: expected executable file at $BIN, but it was not found"
        return 1
    fi
    if ! command -v nc > /dev/null 2>&1; then
        kc_test_fail "nc tool check: expected nc executable in PATH, but it was not found"
        return 1
    fi
    if ! command -v timeout > /dev/null 2>&1; then
        kc_test_fail "timeout tool check: expected timeout executable in PATH, but it was not found"
        return 1
    fi
    return 0
}

# Tests fail-fast CLI behavior.
# @return 0 on success, 1 on failure.
kc_test_cli() {
    if "$BIN" --bad > /dev/null 2>&1; then
        kc_test_fail "cli unknown option: expected non-zero exit, got 0"
        return 1
    fi
    kc_test_pass "cli unknown option fails"

    if "$BIN" 127.0.0.1:bad > /dev/null 2>&1; then
        kc_test_fail "cli invalid address: expected non-zero exit, got 0"
        return 1
    fi
    kc_test_pass "cli invalid address fails"

    return 0
}

# Tests TCP payload delivery to a local server.
# @return 0 on success, 1 on failure.
kc_test_tcp() {
    port=$((30000 + ($$ % 20000)))
    out="$TMPDIR/tcp.out"
    rm -f "$out"
    timeout 5s nc -l -p "$port" > "$out" 2>/dev/null &
    pid=$!
    sleep 0.2
    if ! printf 'hello tcp' | "$BIN" "127.0.0.1:$port"; then
        kill "$pid" 2>/dev/null
        kc_test_fail "tcp send: expected client command exit code 0, got non-zero"
        return 1
    fi
    kc_test_pass "tcp client send command succeeds"

    wait "$pid" || {
        kc_test_fail "tcp receive: expected server command exit code 0, got non-zero"
        return 1
    }
    kc_test_pass "tcp server listener exits successfully"

    actual=$(cat "$out")
    if [ "$actual" != "hello tcp" ]; then
        kc_test_fail "tcp payload: expected 'hello tcp', got '$actual'"
        return 1
    fi
    kc_test_pass "tcp payload delivers matches expected string"
    return 0
}

# Tests UDP payload delivery to a local server.
# @return 0 on success, 1 on failure.
kc_test_udp() {
    port=$((40000 + ($$ % 20000)))
    out="$TMPDIR/udp.out"
    rm -f "$out"
    timeout 5s nc -u -l -p "$port" > "$out" 2>/dev/null &
    pid=$!
    sleep 0.2
    if ! printf 'hello udp' | "$BIN" "127.0.0.1:$port" --udp; then
        kill "$pid" 2>/dev/null
        kc_test_fail "udp send: expected client command exit code 0, got non-zero"
        return 1
    fi
    kc_test_pass "udp client send command succeeds"

    sleep 0.2
    kill "$pid" 2>/dev/null
    wait "$pid" 2>/dev/null || true
    actual=$(cat "$out")
    if [ "$actual" != "hello udp" ]; then
        kc_test_fail "udp payload: expected 'hello udp', got '$actual'"
        return 1
    fi
    kc_test_pass "udp payload delivers matches expected string"
    return 0
}

# Validate public static-library embedding.
# @return 0 on success, 1 on failure.
kc_test_api() {
    LIB_PATH="$ROOT/bin/$(kc_test_arch)/$(kc_test_platform)/libnets.a"
    if [ ! -f "$LIB_PATH" ]; then
        kc_test_fail "static library existence: expected file at $LIB_PATH, but it was not found"
        return 1
    fi

    port_tcp=$((30000 + ($$ % 10000)))
    port_udp=$((40000 + ($$ % 10000)))

    out_tcp="$TMPDIR/api_tcp.out"
    out_udp="$TMPDIR/api_udp.out"
    rm -f "$out_tcp" "$out_udp"

    timeout 5s nc -l -p "$port_tcp" > "$out_tcp" 2>/dev/null &
    pid_tcp=$!

    timeout 5s nc -u -l -p "$port_udp" > "$out_udp" 2>/dev/null &
    pid_udp=$!

    sleep 0.2

    {
        printf '%s\n' '#include "nets.h"'
        printf '%s\n' '#include <stdio.h>'
        printf '%s\n' '#include <stdlib.h>'
        printf '%s\n' 'int main(int argc, char **argv) {'
        printf '%s\n' '    if (argc != 3) return 1;'
        printf '%s\n' '    unsigned short port_tcp = (unsigned short)atoi(argv[1]);'
        printf '%s\n' '    unsigned short port_udp = (unsigned short)atoi(argv[2]);'
        printf '%s\n' '    kc_nets_options_t opts = kc_nets_options_default();'
        printf '%s\n' '    kc_nets_t *ctx = NULL;'
        printf '%s\n' '    if (kc_nets_open(NULL, &opts) != KC_NETS_EINVAL) return 1;'
        printf '%s\n' '    if (kc_nets_open(&ctx, NULL) != KC_NETS_EINVAL) return 1;'
        printf '%s\n' '    if (kc_nets_open(&ctx, &opts) != KC_NETS_OK) return 2;'
        printf '%s\n' '    if (kc_nets_send(NULL, "127.0.0.1", port_tcp, KC_NETS_TCP, "data", 4) != KC_NETS_EINVAL) return 3;'
        printf '%s\n' '    if (kc_nets_send(ctx, NULL, port_tcp, KC_NETS_TCP, "data", 4) != KC_NETS_EINVAL) return 3;'
        printf '%s\n' '    if (kc_nets_send(ctx, "127.0.0.1", port_tcp, 999, "data", 4) != KC_NETS_EINVAL) return 3;'
        printf '%s\n' '    if (kc_nets_send(ctx, "127.0.0.1", port_tcp, KC_NETS_TCP, "hello api tcp", 13) != KC_NETS_OK) return 4;'
        printf '%s\n' '    if (kc_nets_send(ctx, "127.0.0.1", port_udp, KC_NETS_UDP, "hello api udp", 13) != KC_NETS_OK) return 5;'
        printf '%s\n' '    if (kc_nets_close(NULL) != KC_NETS_OK) return 6;'
        printf '%s\n' '    if (kc_nets_close(ctx) != KC_NETS_OK) return 6;'
        printf '%s\n' '    return 0;'
        printf '%s\n' '}'
    } > "$TMPDIR/consumer.c"

    if ! cc -I "$ROOT/src" "$TMPDIR/consumer.c" "$LIB_PATH" -o "$TMPDIR/consumer"; then
        kill "$pid_tcp" "$pid_udp" 2>/dev/null
        kc_test_fail "static library compile: expected consumer.c to compile, but compilation failed"
        return 1
    fi
    kc_test_pass "static library consumer program compiles successfully"

    if ! "$TMPDIR/consumer" "$port_tcp" "$port_udp"; then
        kill "$pid_tcp" "$pid_udp" 2>/dev/null
        kc_test_fail "static library execution: expected consumer program to return 0, got non-zero"
        return 1
    fi
    kc_test_pass "static library send API execution returns successfully"

    wait "$pid_tcp" || {
        kill "$pid_udp" 2>/dev/null
        kc_test_fail "static library TCP receive: expected server command exit code 0, got non-zero"
        return 1
    }

    sleep 0.2
    kill "$pid_udp" 2>/dev/null
    wait "$pid_udp" 2>/dev/null || true

    actual_tcp=$(cat "$out_tcp")
    if [ "$actual_tcp" != "hello api tcp" ]; then
        kc_test_fail "static library TCP payload: expected 'hello api tcp', got '$actual_tcp'"
        return 1
    fi
    kc_test_pass "static library TCP payload delivers matches expected string"

    actual_udp=$(cat "$out_udp")
    if [ "$actual_udp" != "hello api udp" ]; then
        kc_test_fail "static library UDP payload: expected 'hello api udp', got '$actual_udp'"
        return 1
    fi
    kc_test_pass "static library UDP payload delivers matches expected string"
    return 0
}

# Tests multi-context stop isolation.
# @return 0 on success, 1 on failure.
kc_test_multictx_stop() {
    LIB_PATH="$ROOT/bin/$(kc_test_arch)/$(kc_test_platform)/libnets.a"

    port_tcp=$((30000 + ($$ % 10000)))
    port_udp=$((40000 + ($$ % 10000)))

    out="$TMPDIR/multictx.out"
    rm -f "$out"

    timeout 5s nc -l -p "$port_tcp" > "$out" 2>/dev/null &
    pid_tcp=$!

    sleep 0.2

    {
        printf '%s\n' '#include "nets.h"'
        printf '%s\n' '#include <stdio.h>'
        printf '%s\n' '#include <stdlib.h>'
        printf '%s\n' '#include <signal.h>'
        printf '%s\n' 'int main(void) {'
        printf '%s\n' '    kc_nets_options_t opts = kc_nets_options_default();'
        printf '%s\n' '    kc_nets_t *a = NULL, *b = NULL;'
        printf '%s\n' '    if (kc_nets_open(&a, &opts) != KC_NETS_OK) return 1;'
        printf '%s\n' '    if (kc_nets_open(&b, &opts) != KC_NETS_OK) return 2;'
        printf '%s\n' '    if (kc_nets_stop(NULL) != KC_NETS_EINVAL) { kc_nets_close(a); kc_nets_close(b); return 3; }'
        printf '%s\n' '    if (kc_nets_stop(a) != KC_NETS_OK) { kc_nets_close(a); kc_nets_close(b); return 4; }'
        printf '%s\n' '    if (kc_nets_send(a, "127.0.0.1", '"$port_tcp"', KC_NETS_TCP, "fail", 4) != KC_NETS_ESTOP) { kc_nets_close(a); kc_nets_close(b); return 5; }'
        printf '%s\n' '    if (kc_nets_send(b, "127.0.0.1", '"$port_tcp"', KC_NETS_TCP, "hello multi", 11) != KC_NETS_OK) { kc_nets_close(a); kc_nets_close(b); return 6; }'
        printf '%s\n' '    if (kc_nets_close(a) != KC_NETS_OK) return 7;'
        printf '%s\n' '    if (kc_nets_stop(b) != KC_NETS_OK) return 8;'
        printf '%s\n' '    if (kc_nets_close(b) != KC_NETS_OK) return 9;'
        printf '%s\n' '    return 0;'
        printf '%s\n' '}'
    } > "$TMPDIR/multictx.c"

    if ! cc -I "$ROOT/src" "$TMPDIR/multictx.c" "$LIB_PATH" -o "$TMPDIR/multictx"; then
        kill "$pid_tcp" 2>/dev/null
        kc_test_fail "multi-context compile: expected multictx.c to compile, but compilation failed"
        return 1
    fi
    kc_test_pass "multi-context: test program compiles successfully"

    if ! "$TMPDIR/multictx"; then
        kill "$pid_tcp" 2>/dev/null
        kc_test_fail "multi-context execution: expected test program to return 0, got non-zero"
        return 1
    fi
    kc_test_pass "multi-context stop: two contexts coexist, stop is isolated"

    wait "$pid_tcp" || true
    actual=$(cat "$out")
    if [ "$actual" != "hello multi" ]; then
        kc_test_fail "multi-context payload: expected 'hello multi', got '$actual'"
        return 1
    fi
    kc_test_pass "multi-context: outstanding context delivers payload"
    return 0
}

# Runs the full validation suite.
# @return 0 on success, 1 on failure.
kc_test_main() {
    failed=0
    ROOT=$(CDPATH='' cd -- "$(dirname "$0")" && pwd)
    BIN="$ROOT/$(kc_test_binary_path)"
    TMPDIR=$(mktemp -d)
    trap 'rm -rf "$TMPDIR"' EXIT INT HUP TERM
    kc_test_check_binary || exit 1
    kc_test_cli || failed=$((failed + 1))
    kc_test_tcp || failed=$((failed + 1))
    kc_test_udp || failed=$((failed + 1))
    kc_test_api || failed=$((failed + 1))
    kc_test_multictx_stop || failed=$((failed + 1))

    return "$failed"
}

kc_test_main
