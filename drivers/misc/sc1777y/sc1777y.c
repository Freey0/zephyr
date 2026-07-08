/* SPDX-License-Identifier: Apache-2.0 */

#define DT_DRV_COMPAT senscomm_sc1777y

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/misc/sc1777y.h>
#include <zephyr/drivers/spi.h>

struct sc1777y_config {
	struct spi_dt_spec bus;
};

static int sc1777y_init(const struct device *dev)
{
	const struct sc1777y_config *cfg = dev->config;

	return spi_is_ready_dt(&cfg->bus) ? 0 : -ENODEV;
}

#define SC1777Y_DEFINE(inst)                                                                     \
	static const struct sc1777y_config sc1777y_config_##inst = {                            \
		.bus = {                                                                       \
			.bus = DEVICE_DT_GET(DT_BUS(DT_DRV_INST(inst))),                      \
			.config = {                                                            \
				.frequency = DT_PROP(DT_DRV_INST(inst), spi_max_frequency),    \
				.operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB |            \
					     SPI_MODE_CPOL | SPI_MODE_CPHA |                \
					     DT_PROP(DT_DRV_INST(inst), duplex) |           \
					     DT_PROP(DT_DRV_INST(inst), frame_format),     \
				.slave = DT_REG_ADDR(DT_DRV_INST(inst)),                     \
				.word_delay =                                              \
					DT_PROP(DT_DRV_INST(inst), spi_interframe_delay_ns), \
			},                                                                   \
		},                                                                           \
	};                                                                                     \
	DEVICE_DT_INST_DEFINE(inst, sc1777y_init, NULL, NULL, &sc1777y_config_##inst,          \
			      POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);

DT_INST_FOREACH_STATUS_OKAY(SC1777Y_DEFINE)
