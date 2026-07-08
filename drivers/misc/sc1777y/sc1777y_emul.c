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
	uint32_t ready_delay;
	uint32_t ready_polls_remaining;
	uint8_t last_command[SC1777Y_MAX_FRAME_LEN];
	size_t last_command_len;
	uint8_t response[SC1777Y_MAX_FRAME_LEN];
	size_t response_len;
	size_t response_offset;
	bool response_ready;
	bool corrupt_next_response_lrc;
	bool next_status_valid;
	uint8_t next_status_sw1;
	uint8_t next_status_sw2;
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

int sc1777y_emul_get_last_response(const struct emul *target, uint8_t *buf, size_t buf_size,
				   size_t *response_len)
{
	struct sc1777y_emul_data *data;

	if (target == NULL || buf == NULL || response_len == NULL) {
		return -EINVAL;
	}

	data = target->data;
	if (buf_size < data->response_len) {
		return -ENOMEM;
	}

	memcpy(buf, data->response, data->response_len);
	*response_len = data->response_len;

	return 0;
}

void sc1777y_emul_set_ready_delay(const struct emul *target, uint32_t polls_before_ready)
{
	struct sc1777y_emul_data *data = target->data;

	data->ready_delay = polls_before_ready;
}

void sc1777y_emul_corrupt_next_response_lrc(const struct emul *target)
{
	struct sc1777y_emul_data *data = target->data;

	data->corrupt_next_response_lrc = true;
}

void sc1777y_emul_set_next_status(const struct emul *target, uint8_t sw1, uint8_t sw2)
{
	struct sc1777y_emul_data *data = target->data;

	data->next_status_sw1 = sw1;
	data->next_status_sw2 = sw2;
	data->next_status_valid = true;
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

static size_t sc1777y_emul_total_rx_len(const struct spi_buf_set *rx_bufs)
{
	size_t total = 0U;

	if (rx_bufs == NULL) {
		return 0U;
	}

	for (size_t i = 0; i < rx_bufs->count; i++) {
		total += rx_bufs->buffers[i].len;
	}

	return total;
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

static size_t sc1777y_emul_set_random_payload(uint8_t *payload, size_t payload_size, uint8_t p2)
{
	size_t len = p2;

	if (len > payload_size) {
		return 0U;
	}

	for (size_t i = 0; i < len; i++) {
		payload[i] = 0xA0 + i;
	}

	return len;
}

static size_t sc1777y_emul_set_identity_payload(uint8_t *payload, size_t payload_size)
{
	static const uint8_t identity[] = {
		0x53, 0x43, 0x17, 0x77, 0x00, 0x00, 0x00, 0x01,
		0x01, 0x02, 0x03, 0x00,
	};

	if (payload_size < sizeof(identity)) {
		return 0U;
	}

	memcpy(payload, identity, sizeof(identity));

	return sizeof(identity);
}

static size_t sc1777y_emul_set_version_payload(uint8_t *payload, size_t payload_size)
{
	if (payload_size < SC1777Y_VERSION_INFO_LEN) {
		return 0U;
	}

	for (size_t i = 0; i < SC1777Y_VERSION_INFO_LEN; i++) {
		payload[i] = 0x30 + i;
	}

	return SC1777Y_VERSION_INFO_LEN;
}

static size_t sc1777y_emul_set_serial_payload(uint8_t *payload, size_t payload_size)
{
	static const uint8_t serial[] = {0x53, 0x43, 0x17, 0x77, 0x00, 0x00, 0x00, 0x01};

	if (payload_size < sizeof(serial)) {
		return 0U;
	}

	memcpy(payload, serial, sizeof(serial));

	return sizeof(serial);
}

static bool sc1777y_emul_prepare_success_payload(const struct sc1777y_emul_data *data, uint8_t *payload,
						 size_t payload_size, size_t *payload_len)
{
	const uint8_t *cmd = data->last_command;
	size_t cmd_data_len;

	if (!sc1777y_emul_is_valid_command(data) || payload_len == NULL) {
		return false;
	}

	cmd_data_len = data->last_command_len - 8U;

	if (cmd[1] == 0x00U && cmd[2] == 0x84U && cmd[3] == 0x00U) {
		*payload_len = sc1777y_emul_set_random_payload(payload, payload_size, cmd[4]);
		return true;
	}

	if (cmd[1] == 0x80U && cmd[2] == 0xCBU && cmd[4] == 0x00U &&
	    (cmd[3] == 0x80U || cmd[3] == 0x81U)) {
		*payload_len = sc1777y_emul_set_identity_payload(payload, payload_size);
		return true;
	}

	if (cmd[1] == 0x80U && cmd[2] == 0xCBU && cmd[3] == 0x00U && cmd[4] == 0x00U) {
		*payload_len = sc1777y_emul_set_version_payload(payload, payload_size);
		return true;
	}

	if (cmd[1] == 0x00U && cmd[2] == 0xB0U && cmd[3] == 0x99U && cmd[4] == 0x00U &&
	    cmd_data_len == 2U && cmd[7] == 0x00U && cmd[8] == SC1777Y_SERIAL_LEN) {
		*payload_len = sc1777y_emul_set_serial_payload(payload, payload_size);
		return true;
	}

	return false;
}

static void sc1777y_emul_prepare_response(struct sc1777y_emul_data *data)
{
	uint8_t sw1;
	uint8_t sw2;
	size_t payload_len = 0U;

	if (data->next_status_valid) {
		sw1 = data->next_status_sw1;
		sw2 = data->next_status_sw2;
		data->next_status_valid = false;
	} else {
		if (!sc1777y_emul_is_valid_command(data)) {
			sw1 = 0x6AU;
			sw2 = 0x90U;
		} else if (sc1777y_emul_prepare_success_payload(data, &data->response[4],
							       sizeof(data->response) - 5U,
							       &payload_len)) {
			sw1 = 0x90U;
			sw2 = 0x00U;
		} else {
			sw1 = 0x6DU;
			sw2 = 0x00U;
		}
	}

	data->response[0] = sw1;
	data->response[1] = sw2;
	data->response[2] = (uint8_t)(payload_len >> 8);
	data->response[3] = (uint8_t)payload_len;

	data->response_len = 4U + payload_len + 1U;
	data->response[data->response_len - 1U] =
		sc1777y_emul_lrc(data->response, 4U + payload_len);
	if (data->corrupt_next_response_lrc) {
		data->response[data->response_len - 1U] ^= 0xFFU;
		data->corrupt_next_response_lrc = false;
	}

	data->response_offset = 0U;
	data->ready_polls_remaining = data->ready_delay;
	data->response_ready = false;
}

static int sc1777y_emul_io(const struct emul *target, const struct spi_config *config,
			   const struct spi_buf_set *tx_bufs, const struct spi_buf_set *rx_bufs)
{
	struct sc1777y_emul_data *data = target->data;
	size_t tx_len;

	ARG_UNUSED(config);

	tx_len = sc1777y_emul_copy_tx_bytes(data, tx_bufs);
	if (tx_len > 0U) {
		data->last_command_len = tx_len;
		data->command_count++;
		sc1777y_emul_prepare_response(data);
		return 0;
	}

	if (rx_bufs != NULL && rx_bufs->count == 1U && rx_bufs->buffers[0].len == 1U &&
	    !data->response_ready) {
		uint8_t ready = 0U;

		if (data->ready_polls_remaining == 0U) {
			ready = 0x55U;
			data->response_ready = true;
		} else {
			data->ready_polls_remaining--;
		}

		sc1777y_emul_fill_rx_bytes(rx_bufs, &ready, sizeof(ready));
		return 0;
	}

	sc1777y_emul_fill_rx_bytes(rx_bufs, &data->response[data->response_offset],
				   data->response_len - data->response_offset);
	data->response_offset += MIN(sc1777y_emul_total_rx_len(rx_bufs),
				     data->response_len - data->response_offset);

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
