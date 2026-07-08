/* SPDX-License-Identifier: Apache-2.0 */

#define DT_DRV_COMPAT senscomm_sc1777y

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/misc/sc1777y.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>

struct sc1777y_config {
	struct spi_dt_spec bus;
};

#define SC1777Y_CMD_HEADER 0x55
#define SC1777Y_READY_BYTE 0x55
#define SC1777Y_MAX_RETRIES 3
#define SC1777Y_POLL_INTERVAL_US 20
#define SC1777Y_POLL_TIMEOUT_US 2000000
#define SC1777Y_RESPONSE_HEADER_LEN 4U

static uint8_t sc1777y_lrc(const uint8_t *buf, size_t len)
{
	uint8_t x = 0U;

	for (size_t i = 0; i < len; i++) {
		x ^= buf[i];
	}

	return (uint8_t)~x;
}

static int sc1777y_status_to_errno(uint8_t sw1, uint8_t sw2)
{
	if (sw1 == 0x90 && sw2 == 0x00) {
		return 0;
	}
	if (sw1 == 0x63 || sw1 == 0x69) {
		return -EACCES;
	}
	if (sw1 == 0x6A && sw2 == 0x90) {
		return -EIO;
	}
	if (sw1 == 0x6D || sw1 == 0x6E || (sw1 == 0x6A && sw2 == 0x81)) {
		return -ENOTSUP;
	}

	return -EIO;
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

	frame[0] = SC1777Y_CMD_HEADER;
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

static int sc1777y_write_frame(const struct spi_dt_spec *bus, uint8_t *frame, size_t frame_len)
{
	struct spi_buf tx_buf = {
		.buf = frame,
		.len = frame_len,
	};
	const struct spi_buf_set tx_bufs = {
		.buffers = &tx_buf,
		.count = 1U,
	};

	return spi_write_dt(bus, &tx_bufs);
}

static int sc1777y_poll_ready(const struct spi_dt_spec *bus)
{
	uint8_t ready = 0U;
	struct spi_buf rx_buf = {
		.buf = &ready,
		.len = sizeof(ready),
	};
	const struct spi_buf_set rx_bufs = {
		.buffers = &rx_buf,
		.count = 1U,
	};

	for (uint32_t waited = 0U; waited < SC1777Y_POLL_TIMEOUT_US;
	     waited += SC1777Y_POLL_INTERVAL_US) {
		int ret = spi_read_dt(bus, &rx_bufs);

		if (ret != 0) {
			return ret;
		}

		if (ready == SC1777Y_READY_BYTE) {
			return 0;
		}

		k_busy_wait(SC1777Y_POLL_INTERVAL_US);
	}

	return -ETIMEDOUT;
}

static int sc1777y_read_bytes(const struct spi_dt_spec *bus, uint8_t *buf, size_t len)
{
	struct spi_buf rx_buf = {
		.buf = buf,
		.len = len,
	};
	const struct spi_buf_set rx_bufs = {
		.buffers = &rx_buf,
		.count = 1U,
	};

	return spi_read_dt(bus, &rx_bufs);
}

static int sc1777y_read_response(const struct spi_dt_spec *bus, uint8_t *response, size_t response_size,
				 size_t *response_len, struct sc1777y_status *status)
{
	uint16_t payload_len;
	size_t total_len;
	int ret;

	if (response_size < SC1777Y_RESPONSE_HEADER_LEN + 1U) {
		return -ENOMEM;
	}

	ret = sc1777y_read_bytes(bus, response, SC1777Y_RESPONSE_HEADER_LEN);
	if (ret != 0) {
		return ret;
	}

	if (status != NULL) {
		status->sw1 = response[0];
		status->sw2 = response[1];
	}

	payload_len = ((uint16_t)response[2] << 8) | response[3];
	if (payload_len > SC1777Y_MAX_DATA_LEN) {
		return -EIO;
	}

	total_len = SC1777Y_RESPONSE_HEADER_LEN + payload_len + 1U;
	if (total_len > response_size) {
		return -ENOMEM;
	}

	ret = sc1777y_read_bytes(bus, &response[SC1777Y_RESPONSE_HEADER_LEN], payload_len + 1U);
	if (ret != 0) {
		return ret;
	}

	if (sc1777y_lrc(response, SC1777Y_RESPONSE_HEADER_LEN + payload_len) !=
	    response[total_len - 1U]) {
		return -EBADMSG;
	}

	*response_len = total_len;

	return 0;
}

int sc1777y_command(const struct device *dev, const struct sc1777y_command *cmd, uint8_t *out,
		    size_t out_size, size_t *out_len, struct sc1777y_status *status)
{
	const struct sc1777y_config *cfg;
	uint8_t frame[SC1777Y_MAX_FRAME_LEN];
	uint8_t response[SC1777Y_MAX_FRAME_LEN];
	size_t frame_len;
	size_t response_len;
	size_t payload_len;
	struct sc1777y_status local_status = {0};
	int ret;

	if (dev == NULL || out_len == NULL || (out_size > 0U && out == NULL)) {
		return -EINVAL;
	}

	ret = sc1777y_build_frame(cmd, frame, sizeof(frame), &frame_len);
	if (ret != 0) {
		return ret;
	}

	cfg = dev->config;
	*out_len = 0U;

	for (int attempt = 0; attempt < SC1777Y_MAX_RETRIES; attempt++) {
		ret = sc1777y_write_frame(&cfg->bus, frame, frame_len);
		if (ret != 0) {
			return ret;
		}

		ret = sc1777y_poll_ready(&cfg->bus);
		if (ret != 0) {
			return ret;
		}

		ret = sc1777y_read_response(&cfg->bus, response, sizeof(response), &response_len,
					    &local_status);
		if (ret == -EBADMSG) {
			continue;
		}
		if (ret != 0) {
			return ret;
		}

		ret = sc1777y_status_to_errno(local_status.sw1, local_status.sw2);
		if (ret == 0) {
			payload_len = response_len - SC1777Y_RESPONSE_HEADER_LEN - 1U;
			if (payload_len > out_size) {
				return -ENOMEM;
			}
			if (payload_len > 0U) {
				memcpy(out, &response[SC1777Y_RESPONSE_HEADER_LEN], payload_len);
			}
			*out_len = payload_len;
			if (status != NULL) {
				*status = local_status;
			}
			return 0;
		}

		if (local_status.sw1 == 0x6A && local_status.sw2 == 0x90 &&
		    attempt + 1 < SC1777Y_MAX_RETRIES) {
			continue;
		}

		if (status != NULL) {
			*status = local_status;
		}
		return ret;
	}

	if (status != NULL) {
		*status = local_status;
	}

	return -EIO;
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
