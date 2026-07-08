/* SPDX-License-Identifier: Apache-2.0 */

#include <stdbool.h>
#include <errno.h>
#define DT_DRV_COMPAT senscomm_sc1777y

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/misc/sc1777y.h>
#include <zephyr/drivers/misc/sc1777y_emul.h>
#include <zephyr/drivers/spi_emul.h>
#include <zephyr/sys/util.h>

struct sc1777y_emul_data {
	uint32_t command_count;
	uint8_t last_command[SC1777Y_MAX_FRAME_LEN];
	size_t last_command_len;
};

static uint8_t sc1777y_emul_lrc(const uint8_t *buf, size_t len)
{
	uint8_t x = 0U;

	for (size_t i = 0; i < len; i++) {
		x ^= buf[i];
	}

	return (uint8_t)~x;
}

void sc1777y_emul_reset(const struct emul *target)
{
	struct sc1777y_emul_data *data = target->data;

	memset(data, 0, sizeof(*data));
}

uint32_t sc1777y_emul_get_command_count(const struct emul *target)
{
	struct sc1777y_emul_data *data = target->data;

	return data->command_count;
}

int sc1777y_emul_get_last_command(const struct emul *target, uint8_t *buf, size_t buf_size,
				  size_t *command_len)
{
	struct sc1777y_emul_data *data;

	if (target == NULL || buf == NULL || command_len == NULL) {
		return -EINVAL;
	}

	data = target->data;
	if (buf_size < data->last_command_len) {
		return -ENOMEM;
	}

	memcpy(buf, data->last_command, data->last_command_len);
	*command_len = data->last_command_len;

	return 0;
}

static size_t sc1777y_emul_copy_tx_bytes(struct sc1777y_emul_data *data,
					 const struct spi_buf_set *tx_bufs)
{
	size_t total = 0U;

	if (tx_bufs == NULL) {
		return 0U;
	}

	for (size_t i = 0; i < tx_bufs->count; i++) {
		const struct spi_buf *buf = &tx_bufs->buffers[i];
		size_t copy_len = buf->len;

		if (copy_len > sizeof(data->last_command) - total) {
			copy_len = sizeof(data->last_command) - total;
		}

		if (copy_len > 0U) {
			memcpy(&data->last_command[total], buf->buf, copy_len);
		}
		total += copy_len;
		if (total == sizeof(data->last_command)) {
			break;
		}
	}

	return total;
}

static void sc1777y_emul_fill_rx_bytes(const struct spi_buf_set *rx_bufs, const uint8_t *response,
				       size_t response_len)
{
	size_t copied = 0U;

	if (rx_bufs == NULL) {
		return;
	}

	for (size_t i = 0; i < rx_bufs->count; i++) {
		const struct spi_buf *buf = &rx_bufs->buffers[i];
		size_t remaining = (copied < response_len) ? (response_len - copied) : 0U;
		size_t copy_len = MIN(buf->len, remaining);

		if (copy_len > 0U) {
			memcpy(buf->buf, &response[copied], copy_len);
			copied += copy_len;
		}

		if (buf->len > copy_len) {
			memset((uint8_t *)buf->buf + copy_len, 0, buf->len - copy_len);
		}
	}
}

static bool sc1777y_emul_is_valid_command(const struct sc1777y_emul_data *data)
{
	uint16_t encoded_data_len;
	uint8_t lrc;

	if (data->last_command_len < 8U) {
		return false;
	}

	if (data->last_command[0] != 0x55U) {
		return false;
	}

	encoded_data_len = ((uint16_t)data->last_command[5] << 8) | data->last_command[6];
	if ((size_t)encoded_data_len + 8U != data->last_command_len) {
		return false;
	}

	lrc = sc1777y_emul_lrc(&data->last_command[1], data->last_command_len - 2U);

	return data->last_command[data->last_command_len - 1U] == lrc;
}

static int sc1777y_emul_io(const struct emul *target, const struct spi_config *config,
			   const struct spi_buf_set *tx_bufs, const struct spi_buf_set *rx_bufs)
{
	static const uint8_t valid_response[] = {0x90, 0x00, 0x00, 0x00, 0x6F};
	static const uint8_t invalid_response[] = {0x6A, 0x90, 0x00, 0x00, 0x95};
	struct sc1777y_emul_data *data = target->data;

	ARG_UNUSED(config);

	data->last_command_len = sc1777y_emul_copy_tx_bytes(data, tx_bufs);
	data->command_count++;
	if (sc1777y_emul_is_valid_command(data)) {
		sc1777y_emul_fill_rx_bytes(rx_bufs, valid_response, sizeof(valid_response));
	} else {
		sc1777y_emul_fill_rx_bytes(rx_bufs, invalid_response, sizeof(invalid_response));
	}

	return 0;
}

static const struct spi_emul_api sc1777y_emul_api = {
	.io = sc1777y_emul_io,
};

static int sc1777y_emul_init(const struct emul *target, const struct device *parent)
{
	ARG_UNUSED(parent);

	sc1777y_emul_reset(target);

	return 0;
}

#define SC1777Y_EMUL_DEFINE(inst)                                                                \
	static struct sc1777y_emul_data sc1777y_emul_data_##inst;                              \
	EMUL_DT_INST_DEFINE(inst, sc1777y_emul_init, &sc1777y_emul_data_##inst, NULL,          \
			    &sc1777y_emul_api, NULL)

DT_INST_FOREACH_STATUS_OKAY(SC1777Y_EMUL_DEFINE)
