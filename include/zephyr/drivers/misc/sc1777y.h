/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_MISC_SC1777Y_H_
#define ZEPHYR_INCLUDE_DRIVERS_MISC_SC1777Y_H_

#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SC1777Y_MAX_DATA_LEN 2048U
#define SC1777Y_MAX_FRAME_LEN (SC1777Y_MAX_DATA_LEN + 8U)

struct sc1777y_command {
	uint8_t cla;
	uint8_t ins;
	uint8_t p1;
	uint8_t p2;
	const uint8_t *data;
	size_t data_len;
};

struct sc1777y_status {
	uint8_t sw1;
	uint8_t sw2;
};

int sc1777y_command(const struct device *dev, const struct sc1777y_command *cmd, uint8_t *out,
		    size_t out_size, size_t *out_len, struct sc1777y_status *status);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_MISC_SC1777Y_H_ */
