# nets.c — Network Sender

## Overview
Minimalist C library and CLI for sending byte buffers to a single TCP or UDP endpoint. Reads payload from stdin, resolves the target host via `getaddrinfo` (AF_UNSPEC, tries all addresses until one succeeds), opens a socket, sends all bytes, and closes. Designed as a composable native primitive for the KaisarCode ecosystem — zero dependencies beyond POSIX/Winsock sockets.

## Architecture
Three-file split: `nets.h` exposes `kc_nets_send` and `kc_nets_strerror`. `libnets.c` implements send logic with platform abstraction (`kc_nets_socket_t`, `kc_nets_close`, `kc_nets_platform_init/cleanup`). `nets.c` is the CLI — parses `<addr>[:port]` and `--tcp`/`--udp` flags, reads stdin into a growing buffer, delegates to `kc_nets_send`, reports errors to stderr. The send function is self-contained: it initializes the platform socket layer, resolves the host, iterates `addrinfo` results, and cleans up before returning.

### TCP vs UDP Connection Logic
Both protocols share the same resolution path: `getaddrinfo` with `AF_UNSPEC` and the appropriate `ai_socktype` (`SOCK_STREAM` for TCP, `SOCK_DGRAM` for UDP). For each resolved address (`ai_next`), a socket is created with the matching `ai_family` and `ai_protocol`. TCP opens a `SOCK_STREAM` socket, calls `connect()`, then sends all bytes in a loop via `send()` (`kc_nets_send_all`) and closes. UDP opens a `SOCK_DGRAM` socket, sends the entire buffer as a single datagram via `sendto()`, and closes immediately — no connect call, one-shot delivery. If the first resolved address fails, the loop advances to the next `addrinfo` result. Platform differences are abstracted: Windows uses `WSAStartup`/`WSACleanup` and `closesocket`; POSIX uses `close`.

## Directory Layout
| Path | Contents |
|------|----------|
| `src/nets.h` | Public API — send function, status codes, protocol constants |
| `src/libnets.c` | Library implementation — socket abstraction, resolution, TCP/UDP send |
| `src/nets.c` | CLI entry point — address parsing, stdin read, flag handling |
| `Makefile` | Cross-compilation builder (17 targets) via CMake + Ninja |
| `CMakeLists.txt` | CMake project definition |
| `test.sh` | Shell test suite — help, TCP send, UDP send |
| `README.md` | Project documentation and usage examples |
| `LICENSE` | GPL v3.0 |
| `.kcsignore` | KCS exclusion list |

## Data Model
### Internal Structures
| Symbol | Type | Role |
|--------|------|------|
| `kc_nets_socket_t` | `typedef` | `SOCKET` on Windows, `int` on POSIX |
| `KC_NETS_BAD_SOCKET` | `#define` | `INVALID_SOCKET` on Windows, `-1` on POSIX |

### Hard Limits
| Limit | Value | Symbol |
|-------|-------|--------|
| Stdin read chunk | 8192 bytes | `buf` in `kc_nets_read_stdin` |
| Stdin initial capacity | 8192 bytes | implicit growth logic |
| Stdin capacity growth | doubles | `cap * 2` |
| Host buffer | 512 bytes | `host` in `main` |
| Port text buffer | 16 bytes | `port_text` in `kc_nets_send` |
| Default port | 80 | `*port = 80` |

## Public API
| Function | Returns | Description |
|----------|---------|-------------|
| `kc_nets_send(host, port, proto, data, size)` | `int` | Send bytes to host:port via TCP or UDP; returns `KC_NETS_OK` (0) or negative error |
| `kc_nets_strerror(int code)` | `const char *` | Return static message for status code |

### Status Codes
| Symbol | Value | Message |
|--------|-------|---------|
| `KC_NETS_OK` | 0 | `"ok"` |
| `KC_NETS_EINVAL` | -1 | `"invalid argument"` |
| `KC_NETS_ENET` | -2 | `"network error"` |

### Protocol Constants
| Symbol | Value |
|--------|-------|
| `KC_NETS_TCP` | 1 |
| `KC_NETS_UDP` | 2 |

## CLI
| Argument | Description |
|----------|-------------|
| `<addr>[:port]` | Destination host or IP; port defaults to 80 |
| `--tcp` | Use TCP (default) |
| `--udp` | Use UDP |
| `-h`, `--help` | Show help and exit 0 |
| `-v`, `--version` | Show version and exit 0 |

Payload is read from stdin. Output is none on success.

### Exit Codes
| Code | Meaning |
|------|---------|
| 0 | Success |
| 1 | Error (bad option, invalid address, stdin read failure, send failure) |

## Build
| Target | Description |
|--------|-------------|
| `make` (default: `native`) | Build for host arch/platform |
| `make all` | Build full 17-target cross-compilation matrix |
| `make <arch>/<platform>` | Build specific target |
| `make test` | Run `sh test.sh` |
| `make clean` | Remove `.build/` |

## Error Handling
| Condition | Stderr | Exit Code |
|-----------|--------|-----------|
| Unknown option | `"nets: unknown option '<opt>'"` | 1 |
| Invalid address/port | `"nets: invalid address"` | 1 |
| Stdin read failure | `"nets: failed to read stdin"` | 1 |
| Send failure | `"nets: <strerror message>"` | 1 |
| Invalid args to API (NULL host, bad proto) | (none, returns `KC_NETS_EINVAL`) | N/A |
| DNS resolution failure | (none, returns `KC_NETS_ENET`) | N/A |
| Socket creation/connect/send failure | (none, returns `KC_NETS_ENET`) | N/A |

## Constraints
- Reads entire stdin into memory before sending — no streaming mode.
- UDP sends the entire buffer as one datagram; if the payload exceeds path MTU, fragmentation is left to the IP layer.
- No TLS, no encryption, no connection pooling, no listener/server mode.
- Port range: 1–65535; validated via `strtoul`.
- Address format: `host:port`; the last colon splits host and port. IPv6 addresses without brackets will be misparsed.
- POSIX and Windows only; no other platforms.
- `kc_nets_send` is not reentrant on Windows (calls `WSAStartup`/`WSACleanup` every invocation).
