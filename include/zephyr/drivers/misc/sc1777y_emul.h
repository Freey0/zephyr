/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_MISC_SC1777Y_EMUL_H_
#define ZEPHYR_INCLUDE_DRIVERS_MISC_SC1777Y_EMUL_H_

#include <stdint.h>

struct emul;

#ifdef __cplusplus
extern "C" {
#endif

void sc1777y_emul_reset(const struct emul *target);
uint32_t sc1777y_emul_get_command_count(const struct emul *target);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_MISC_SC1777Y_EMUL_H_ */
