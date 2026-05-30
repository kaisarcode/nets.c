/**
 * nets.h - Network sender.
 * Summary: Public API for sending byte buffers over TCP or UDP.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef KC_NETS_H
#define KC_NETS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KC_NETS_OK        0
#define KC_NETS_EINVAL   -1
#define KC_NETS_ENET     -2

#define KC_NETS_TCP       1
#define KC_NETS_UDP       2

typedef struct {
    int reserved;
} kc_nets_options_t;

typedef void (*kc_nets_signal_callback_t)(void);

/**
 * Returns default-initialized options.
 * @return Default-initialized options.
 */
kc_nets_options_t kc_nets_options_default(void);

/**
 * Loads environment variables into options.
 * @param opts Options to update.
 * @return None.
 */
void kc_nets_options_load_env(kc_nets_options_t *opts);

/**
 * Frees options resources.
 * @param opts Options to free.
 * @return None.
 */
void kc_nets_options_free(kc_nets_options_t *opts);

/**
 * Registers a signal callback.
 * @param sig Signal number.
 * @param cb Callback function, or NULL to unregister.
 * @return KC_NETS_OK on success, or a negative error code.
 */
int kc_nets_on_signal(int sig, kc_nets_signal_callback_t cb);

/**
 * Raises a signal to registered callbacks.
 * @param sig Signal number.
 * @return KC_NETS_OK on success, or a negative error code.
 */
int kc_nets_raise_signal(int sig);

/**
 * Listens for registered signals.
 * @return KC_NETS_OK on success, or a negative error code.
 */
int kc_nets_listen_signals(void);

/**
 * Listens for a specific signal.
 * @param sig_id Signal number.
 * @return KC_NETS_OK on success, or a negative error code.
 */
int kc_nets_listen_signal(int sig_id);

/**
 * Default signal listener.
 * @param sig Signal number.
 * @return None.
 */
void kc_nets_signal_listener(int sig);

/**
 * Sends bytes to one network address.
 * @param host Destination host or IP address.
 * @param port Destination port.
 * @param proto KC_NETS_TCP or KC_NETS_UDP.
 * @param data Buffer to send.
 * @param size Buffer size in bytes.
 * @return KC_NETS_OK on success, or a negative error code.
 */
int kc_nets_send(
const char *host,
unsigned short port,
int proto,
const void *data,
size_t size
);

/**
 * Returns a static message for a nets status code.
 * @param code Status code.
 * @return Static message.
 */
const char *kc_nets_strerror(int code);

#ifdef __cplusplus
}
#endif

#endif
