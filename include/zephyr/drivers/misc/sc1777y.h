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
#define SC1777Y_SERIAL_LEN 8U
#define SC1777Y_KEY_VERSION_LEN 4U
#define SC1777Y_IDENTITY_LEN (SC1777Y_SERIAL_LEN + SC1777Y_KEY_VERSION_LEN)
#define SC1777Y_VERSION_INFO_LEN 64U

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

struct sc1777y_identity {
	uint8_t serial[SC1777Y_SERIAL_LEN];
	uint8_t key_version[SC1777Y_KEY_VERSION_LEN];
};

struct sc1777y_version_info {
	uint8_t bytes[SC1777Y_VERSION_INFO_LEN];
};

int sc1777y_command(const struct device *dev, const struct sc1777y_command *cmd, uint8_t *out,
		    size_t out_size, size_t *out_len, struct sc1777y_status *status);
int sc1777y_get_random(const struct device *dev, uint8_t *out, size_t len);
int sc1777y_get_sensor_identity(const struct device *dev, struct sc1777y_identity *identity);
int sc1777y_get_update_identity(const struct device *dev, struct sc1777y_identity *identity);
int sc1777y_get_version_info(const struct device *dev, struct sc1777y_version_info *version);
int sc1777y_get_serial(const struct device *dev, uint8_t serial[SC1777Y_SERIAL_LEN]);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_MISC_SC1777Y_H_ */
