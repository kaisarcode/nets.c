# nets.c Design

## Purpose

`nets.c` sends one caller-provided byte buffer to one network endpoint. Its CLI
reads stdin, selects TCP, UDP, or optional TLS, and delegates one send operation
to the library.

The project is intended as a small Unix-style composition primitive for local
scripts, personal infrastructure, small servers, workshops, and modest systems.
It is not a general network client platform.

## Architecture

The CLI parses the target and flags, buffers stdin, opens one context, and calls
`kc_nets_send()`. The library resolves the host, tries resolved addresses in
order, creates one socket per attempt, performs the selected transport operation,
and releases all per-call network state before returning.

The four source files have fixed responsibilities:

- `src/nets.c` owns CLI parsing and stdin buffering;
- `src/libnets.c` owns reusable transport, TLS, and stop behavior;
- `src/libnets.h` defines the public contract;
- `src/test.c` contains all tests.

## Target Syntax

Targets may be a host, `host:port`, bracketed IPv6 address, or URL-shaped value
using `http`, `https`, `tcp`, or `udp`.

The schemes mean only:

- `http` selects TCP and port 80;
- `https` selects TLS and port 443;
- `tcp` selects TCP and port 80;
- `udp` selects UDP and port 80.

An explicit port replaces the default. CLI flags may replace the scheme-selected
transport. Any path, query, or fragment is ignored after authority parsing.
User-information syntax is rejected.

This syntax is endpoint convenience, not URL fetching or application protocol
support.

## TCP Operation

TCP resolves the target, connects one stream socket, and loops until every input
byte is sent. On POSIX it shuts down the write side and copies response bytes to
stdout until peer EOF, then closes the socket.

The sender does not frame messages, infer request completion, parse responses,
retry requests, or keep connections alive. The remote protocol determines when
it closes the response stream.

Current plain TCP response reading is absent from the Windows implementation.
That platform difference must remain visible until implemented and runtime
tested.

## UDP Operation

UDP sends the complete buffer through one `sendto()` call and closes the socket.
Success requires the reported sent length to equal the input length.

The library does not split oversized input, wait for a reply, retransmit, order,
deduplicate, or establish a session. Datagram size limits are supplied by the
local network stack and path.

## TLS Operation

TLS is compiled only when OpenSSL headers and libraries are found. It uses a
shared client `SSL_CTX`, sets SNI to the supplied host, completes one handshake,
sends all input bytes, shuts down its write direction, reads response bytes to
stdout, and releases the SSL and socket objects.

The current implementation does not configure certificate-chain or hostname
verification. It provides encryption without authenticated peer identity and
must not be represented as equivalent to a verified HTTPS client.

TLS adds stream protection only. It does not add HTTP framing, URL retrieval,
cookies, redirects, credentials, or application semantics.

## Context and Ownership

An opened context owns copied options and a stop flag. The caller owns the input
buffer throughout `kc_nets_send()`. Host and payload pointers are borrowed for
the duration of the call and are not retained.

stdout is borrowed process state. The library writes response bytes and flushes
it but never closes it.

Closing a context releases its owned memory. There is no persistent connection
or endpoint state.

## Stop Behavior

A stop request sets context-local state. Sends check it before network work and
during plain TCP or TLS write loops.

Stop is cooperative, not a universal cancellation mechanism. Current blocking
DNS resolution, connect calls, response reads, and some transport operations may
not observe it promptly.

## Resource and Failure Model

The CLI currently grows one memory buffer until stdin reaches EOF. Stream
responses are copied in fixed 65,536-byte chunks, but response duration and total
size are controlled by peer EOF. No default connection or read timeout is set.

These are explicit limitations. Future bounds or timeouts must have clear CLI
and API semantics and preserve exact binary behavior. Hidden spooling, background
workers, automatic retries, and resident state are outside the default model.

Invalid arguments return `KC_NETS_EINVAL`; resolution, socket, connect, send,
TLS, and unavailable-TLS failures return `KC_NETS_ENET`; observed stop state
returns `KC_NETS_ESTOP`. Address attempts continue until one succeeds or all
fail.

## Composition

`nets` carries bytes. A protocol builder such as `http` can produce those bytes,
and a separate parser can consume the returned stdout stream. Files, templates,
compression, authentication material, and application rules remain separate.

This keeps network transport inspectable without embedding a protocol ecosystem.

## Portability

The socket layer targets POSIX and Windows, with OpenSSL enabled only where found
at build time. Platform behavior must be documented and runtime tested rather
than inferred from cross-compilation.

Portability supports locally available hardware. It does not justify platform
frameworks or mandatory external services.

## Non-Goals

The project does not provide an HTTP client, browser, downloader, API SDK, proxy
manager, connection pool, retry engine, transfer queue, daemon, service
discovery client, credential manager, certificate service, QUIC stack,
application parser, telemetry system, hosted control plane, or plugin platform.

These exclusions define the tool rather than an unfinished roadmap.

## Change Criteria

A change must solve a concrete one-shot transport problem, preserve exact bytes
and one-target operation, define blocking and stop behavior, retain explicit
socket ownership, account for platform differences, avoid hidden persistence or
network dependencies, and keep protocol policy outside the library.

Changes justified mainly by enterprise scale, generalized client features,
managed operation, or ecosystem expectations should be rejected.

## Core Invariants

The project is defined by one input buffer, one resolved endpoint, explicit TCP,
UDP, or optional TLS operation, one-datagram UDP behavior, raw stdout responses,
caller-owned data, no retained connections, and no application protocol layer.
