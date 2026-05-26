# nets.c - Network Sender

`nets.c` is a small C library and CLI for sending standard input to one TCP or UDP address.

---

## CLI

Send bytes from standard input to a network address.

### Examples

Send to a TCP endpoint:

```bash
echo 'hola' | nets 127.0.0.1:8080
```

Send to port 80 by default:

```bash
echo 'hola' | nets 127.0.0.1
```

Send a UDP datagram:

```bash
echo 'hola' | nets 127.0.0.1:8080 --udp
```

---

### Parameters

| Parameter | Description |
| :--- | :--- |
| `<addr>[:port]` | Destination host or IP address. Port defaults to `80`. |
| `--tcp` | Use TCP. This is the default. |
| `--udp` | Use UDP. |
| `-h`, `--help` | Show help and usage. |
| `-v`, `--version` | Show version. |

---

## Public API

```c
#include "nets.h"

const char msg[] = "hello\n";
kc_nets_send("127.0.0.1", 8080, KC_NETS_TCP, msg, sizeof(msg) - 1);
```

---

## Lifecycle

- `kc_nets_send()` opens a socket, sends the provided bytes, and closes the socket before returning.
- TCP sends all bytes over one connection.
- UDP sends the provided bytes as one datagram.
- The caller owns the input buffer before and after the call.

---

## Build

Compiled artifacts are generated under `bin/{arch}/{platform}/` for the host architecture running the build.

```bash
make clean && make
```

### Multiarch Builds

The project is prepared to build artifacts for multiple architectures under `bin/{arch}/{platform}/`. A plain `make` builds only the current host architecture, while the targets below build the full matrix or a specific target.

```bash
make all
make x86_64/linux
make x86_64/windows
make i686/linux
make i686/windows
make aarch64/linux
make aarch64/android
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

## Beta Notice

This is a beta project tested only on Debian x86_64. It was created out of a personal need for these libraries, but no guarantees are provided regarding its stability or future support. You are free to test it, use it, and modify it as you please.

If you'd like to reach out, you can send an email to kaisar@kaisarcode.com. Please note that I do not accept pull requests; the goal is to avoid long-term dependency on platforms like GitHub, and I do not maintain fixed infrastructure to guarantee long-term stability for these projects.

---

## License

[![GPLv3](https://www.gnu.org/graphics/gplv3-127x51.png)](https://www.gnu.org/licenses/gpl-3.0.html)

This project is distributed under the **GNU General Public License version 3 (GPLv3)**. 
