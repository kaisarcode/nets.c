#!/bin/sh
# test.sh
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
        kc_test_fail "binary not found: $BIN"
        return 1
    fi
    if ! command -v nc > /dev/null 2>&1; then
        kc_test_fail "nc not found"
        return 1
    fi
    if ! command -v timeout > /dev/null 2>&1; then
        kc_test_fail "timeout not found"
        return 1
    fi
    return 0
}

# Tests help, version, and fail-fast CLI behavior.
# @return 0 on success, 1 on failure.
kc_test_cli() {
    if ! "$BIN" --help > /dev/null 2>&1; then
        kc_test_fail "cli: --help failed"
        return 1
    fi
    if ! "$BIN" -v > /dev/null 2>&1; then
        kc_test_fail "cli: -v failed"
        return 1
    fi
    if "$BIN" --bad > /dev/null 2>&1; then
        kc_test_fail "cli: unknown option should fail"
        return 1
    fi
    if "$BIN" 127.0.0.1:bad > /dev/null 2>&1; then
        kc_test_fail "cli: invalid port should fail"
        return 1
    fi
    kc_test_pass "cli"
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
        kc_test_fail "tcp: send failed"
        return 1
    fi
    wait "$pid" || {
        kc_test_fail "tcp: server failed"
        return 1
    }
    if [ "$(cat "$out")" != "hello tcp" ]; then
        kc_test_fail "tcp: payload mismatch"
        return 1
    fi
    kc_test_pass "tcp"
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
        kc_test_fail "udp: send failed"
        return 1
    fi
    sleep 0.2
    kill "$pid" 2>/dev/null
    wait "$pid" 2>/dev/null || true
    if [ "$(cat "$out")" != "hello udp" ]; then
        kc_test_fail "udp: payload mismatch"
        return 1
    fi
    kc_test_pass "udp"
}

# Runs the full validation suite.
# @return 0 on success, 1 on failure.
kc_test_main() {
    failed=0
    TMPDIR=$(mktemp -d)
    BIN=$(kc_test_binary_path)
    trap 'rm -rf "$TMPDIR"' EXIT INT HUP TERM
    kc_test_check_binary || exit 1
    kc_test_cli || failed=$((failed + 1))
    kc_test_tcp || failed=$((failed + 1))
    kc_test_udp || failed=$((failed + 1))
    return "$failed"
}

kc_test_main
