/* SPDX-License-Identifier: Apache-2.0 */

#define DT_DRV_COMPAT senscomm_sc1777y

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/misc/sc1777y.h>
#include <zephyr/drivers/spi.h>

struct sc1777y_config {
	struct spi_dt_spec bus;
};

static uint8_t sc1777y_lrc(const uint8_t *buf, size_t len)
{
	uint8_t x = 0U;

	for (size_t i = 0; i < len; i++) {
		x ^= buf[i];
	}

	return (uint8_t)~x;
}

static int sc1777y_build_frame(const struct sc1777y_command *cmd, uint8_t *frame, size_t frame_size,
			       size_t *frame_len)
{
	if (cmd == NULL || frame == NULL || frame_len == NULL ||
	    cmd->data_len > SC1777Y_MAX_DATA_LEN ||
	    (cmd->data_len != 0U && cmd->data == NULL)) {
		return -EINVAL;
	}

	if (frame_size < cmd->data_len + 8U) {
		return -ENOMEM;
	}

	frame[0] = 0x55;
	frame[1] = cmd->cla;
	frame[2] = cmd->ins;
	frame[3] = cmd->p1;
	frame[4] = cmd->p2;
	frame[5] = (uint8_t)(cmd->data_len >> 8);
	frame[6] = (uint8_t)cmd->data_len;

	if (cmd->data_len > 0U) {
		memcpy(&frame[7], cmd->data, cmd->data_len);
	}

	frame[7 + cmd->data_len] = sc1777y_lrc(&frame[1], 6U + cmd->data_len);
	*frame_len = cmd->data_len + 8U;

	return 0;
}

int sc1777y_command(const struct device *dev, const struct sc1777y_command *cmd, uint8_t *out,
		    size_t out_size, size_t *out_len, uint16_t *status)
{
	const struct sc1777y_config *cfg;
	uint8_t frame[SC1777Y_MAX_FRAME_LEN];
	uint8_t response[SC1777Y_MAX_FRAME_LEN];
	struct spi_buf tx_buf = {
		.buf = frame,
	};
	struct spi_buf rx_buf = {
		.buf = response,
	};
	const struct spi_buf_set tx_bufs = {
		.buffers = &tx_buf,
		.count = 1U,
	};
	const struct spi_buf_set rx_bufs = {
		.buffers = &rx_buf,
		.count = 1U,
	};
	size_t frame_len;
	int ret;

	if (dev == NULL || out_len == NULL || (out_size > 0U && out == NULL)) {
		return -EINVAL;
	}

	ret = sc1777y_build_frame(cmd, frame, sizeof(frame), &frame_len);
	if (ret != 0) {
		return ret;
	}

	cfg = dev->config;
	tx_buf.len = frame_len;
	rx_buf.len = frame_len;

	ret = spi_transceive_dt(&cfg->bus, &tx_bufs, &rx_bufs);
	if (ret != 0) {
		return ret;
	}

	*out_len = 0U;

	if (status != NULL) {
		*status = ((uint16_t)response[0] << 8) | response[1];
	}

	return 0;
}

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
