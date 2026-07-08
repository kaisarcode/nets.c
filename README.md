# nets.c - Network Sender

`nets.c` is a small C library and CLI for sending standard input to one TCP or UDP address and printing the response to standard output.

---

## CLI

Send bytes from standard input to a network address and print the response to standard output.

### Examples

Send to a TCP endpoint — stdin is sent, then the response is printed to stdout:

```bash
echo 'hello' | nets 127.0.0.1:8080
```

Send to port 80 by default:

```bash
echo 'hello' | nets 127.0.0.1
```

Send a UDP datagram:

```bash
echo 'hello' | nets 127.0.0.1:8080 --udp
```

Open a temporary control socket while one CLI send runs:

```bash
echo 'hello' | nets 127.0.0.1:8080 --ctrl /tmp/nets.sock
```

---

### Parameters

| Parameter | Description |
| :--- | :--- |
| `<addr>[:port]` | Destination host or IP address. Port defaults to `80`. |
| `--tcp` | Use TCP. This is the default. |
| `--udp` | Use UDP. |
| `--ctrl PATH` | Open a Unix domain control socket for the current CLI run. |
| `-h`, `--help` | Show help and usage. |
| `-v`, `--version` | Show version. |

---

## Public API

```c
#include "nets.h"

const char msg[] = "hello\n";
kc_nets_options_t opts = kc_nets_options_default();
kc_nets_t *ctx = NULL;

kc_nets_open(&ctx, &opts);
kc_nets_send(ctx, "127.0.0.1", 8080, KC_NETS_TCP, msg, sizeof(msg) - 1);
kc_nets_close(ctx);
kc_nets_options_free(&opts);
```

---

## Lifecycle

- `kc_nets_options_default()` creates a default options struct.
- `kc_nets_options_load_env()` overlays options from environment variables such as `KC_NETS_CTRL`.
- `kc_nets_options_free()` releases resources owned by options.
- `kc_nets_open()` allocates a sender context.
- `kc_nets_ctrl_open()` opens a Unix domain control socket on POSIX systems.
- The built-in control commands are `HELP`, `STOP`, `GET`, and `SET`.
- `GET` exposes only `ctrl_path` and `reserved`.
- `SET` currently accepts only `ctrl_path`; unknown keys are rejected.
- The CLI is still one-shot: the control socket exists only while one invocation is alive, around stdin read and one send attempt.
- `kc_nets_send()` opens a socket, sends the provided bytes, reads the response to stdout, and closes the socket before returning.
- TCP sends all bytes over one connection, then reads the response until EOF.
- UDP sends the provided bytes as one datagram.
- The caller owns the input buffer before and after the call.
- `kc_nets_stop()` requests stop for one context.
- `kc_nets_on_signal()` registers or removes application-defined signal handlers.
- `kc_nets_raise_signal()` raises an application-defined signal on a context.
- `kc_nets_listen_signals()` registers a context with the global signal listener.
- `kc_nets_listen_signal()` wires an OS signal to the global listener.
- `kc_nets_signal_listener()` dispatches a signal to registered contexts.
- `kc_nets_strerror()` returns a static status message.
- `kc_nets_version()` returns the build version.
- `kc_nets_close()` releases the context.

---

## Build

Compiled artifacts are generated under `bin/{arch}/{platform}/` for the host architecture running the build.

```bash
make clean && make
```

### Tests

The portable test entry point is `make test`. Build project artifacts first, then run tests. Tests compile only test executables, link dynamically against the generated shared library, and run through CTest.

```bash
make
make test
```

To run the common `test` target in Windows-through-Wine mode:

```bash
make x86_64/windows
make test wine
```

The portable C test source is `src/test.c`. Test binaries and runtime outputs are build artifacts and are not stored in the project tree.

Build targets such as `make x86_64/windows` compile project artifacts. Tests are run only through `make test` or `make test wine`.

### Multiarch Builds

The project is prepared to build artifacts for multiple architectures under `bin/{arch}/{platform}/`. A plain `make` builds only the current host architecture.

```bash
make all
make x86_64/linux
make x86_64/windows
make x86_64/macos
make x86_64/iossim
make i686/linux
make i686/windows
make aarch64/linux
make aarch64/android
make aarch64/macos
make aarch64/ios
make aarch64/iossim
make armv7/linux
make armv7/android
make armv7hf/linux
make riscv64/linux
make powerpc64le/linux
make mips/linux
make mipsel/linux
make mips64el/linux
make s390x/linux
make loongarch64/linux
```

---

## Development Requirements

### Build Tools

- `make` (GNU Make)
- `cmake` >= 3.14
- `ninja`
- `gcc` or `clang` (C11 compatible)

### System Libraries

Linux:
- `libpthread`
- `libm`

Windows (MSVC or MinGW):
- `ws2_32`

macOS / iOS:
- No additional system libraries required.

### Optional Cross-Compilation SDKs

Required only for multiarch builds:

- MinGW (`x86_64-w64-mingw32-gcc`) for Windows cross-compilation from Linux.
- `wine` for running Windows tests on Linux.
- `osxcross` with macOS and iOS SDKs for macOS and iOS targets.
- Android NDK (version 27.2.12479018) for Android targets.

### Test Dependencies

- `ctest` (included with cmake)

---

## Beta Notice

This is a beta project tested only on Debian x86_64. It was created out of a personal need for these libraries, but no guarantees are provided regarding its stability or future support. You are free to test it, use it, and modify it as you please.

If you'd like to reach out, you can send an email to kaisar@kaisarcode.com. Please note that I do not accept pull requests; the goal is to avoid long-term dependency on platforms like GitHub, and I do not maintain fixed infrastructure to guarantee long-term stability for these projects.

---

## License

[![GPLv3](https://www.gnu.org/graphics/gplv3-127x51.png)](https://www.gnu.org/licenses/gpl-3.0.html)

This project is distributed under the **GNU General Public License version 3 (GPLv3)**.
