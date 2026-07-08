/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_MISC_SC1777Y_EMUL_H_
#define ZEPHYR_INCLUDE_DRIVERS_MISC_SC1777Y_EMUL_H_

#include <stddef.h>
#include <stdint.h>

struct emul;

#ifdef __cplusplus
extern "C" {
#endif

void sc1777y_emul_reset(const struct emul *target);
uint32_t sc1777y_emul_get_command_count(const struct emul *target);
int sc1777y_emul_get_last_command(const struct emul *target, uint8_t *buf, size_t buf_size,
				  size_t *command_len);
int sc1777y_emul_get_last_response(const struct emul *target, uint8_t *buf, size_t buf_size,
				   size_t *response_len);
void sc1777y_emul_set_ready_delay(const struct emul *target, uint32_t polls_before_ready);
void sc1777y_emul_corrupt_next_response_lrc(const struct emul *target);
void sc1777y_emul_set_next_status(const struct emul *target, uint8_t sw1, uint8_t sw2);
void sc1777y_emul_set_status_repeat(const struct emul *target, uint8_t sw1, uint8_t sw2,
				      uint32_t repeat_count);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_MISC_SC1777Y_EMUL_H_ */
