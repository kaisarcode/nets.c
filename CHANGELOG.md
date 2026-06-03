# CHANGELOG

## v1.1.1

- Fixed `nets_signal_listener` to restore default signal behavior (SIG_DFL)
  when no `on_signal` handler is registered. Added missing `<signal.h>` include.

## v1.1.0

- Added data-driven configuration lifecycle through `kc_nets_options_t`.
- Added `kc_nets_options_default()`, `kc_nets_options_load_env()`, and `kc_nets_options_free()` to the public API.
- Added signal listener lifecycle: `kc_nets_on_signal()`, `kc_nets_raise_signal()`, `kc_nets_listen_signals()`, `kc_nets_listen_signal()`, and `kc_nets_signal_listener()`.

## v1.0.0

- Published the stable baseline release.
- Provided TCP and UDP byte buffer transmission to a single endpoint.
- Supported cross-platform POSIX and Windows socket abstraction.

---

**Author:** KaisarCode

**Email:** <kaisar@kaisarcode.com>

**Website:** [https://kaisarcode.com](https://kaisarcode.com)

**License:** [GNU GPL v3.0](https://www.gnu.org/licenses/gpl-3.0.html)

© 2026 KaisarCode
