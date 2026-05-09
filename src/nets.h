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
