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
#define SC1777Y_SENSOR_AUTH_CHALLENGE_LEN 4U
#define SC1777Y_SENSOR_AUTH_ENCRYPTED_LEN 8U
#define SC1777Y_SENSOR_AUTH_RAND_LEN 4U
#define SC1777Y_SENSOR_BLOCK_LEN 8U
#define SC1777Y_SENSOR_ID_LEN 8U
#define SC1777Y_UPDATE_AUTH_ENCRYPTED_LEN 8U
#define SC1777Y_PLATFORM_TYPE_LEN 1U

static int sc1777y_command_expect_len(const struct device *dev, const struct sc1777y_command *cmd,
				      uint8_t *out, size_t len)
{
	size_t out_len;
	int ret;

	ret = sc1777y_command(dev, cmd, out, len, &out_len, NULL);
	if (ret != 0) {
		return ret;
	}

	return out_len == len ? 0 : -EIO;
}

static int sc1777y_command_expect_empty(const struct device *dev, const struct sc1777y_command *cmd)
{
	size_t out_len;
	int ret;

	ret = sc1777y_command(dev, cmd, NULL, 0U, &out_len, NULL);
	if (ret != 0) {
		return ret;
	}

	return out_len == 0U ? 0 : -EIO;
}

static int sc1777y_sensor_auth_p2(enum sc1777y_sensor_type type, uint8_t *p2)
{
	if (p2 == NULL) {
		return -EINVAL;
	}

	if (type == SC1777Y_SENSOR_LEGACY) {
		*p2 = 0x01;
		return 0;
	}

	if (type == SC1777Y_SENSOR_NEW) {
		*p2 = 0x04;
		return 0;
	}

	return -EINVAL;
}

static int sc1777y_sensor_data_p2(enum sc1777y_sensor_type type, uint8_t *p2)
{
	if (p2 == NULL) {
		return -EINVAL;
	}

	if (type == SC1777Y_SENSOR_LEGACY) {
		*p2 = 0x02;
		return 0;
	}

	if (type == SC1777Y_SENSOR_NEW) {
		*p2 = 0x05;
		return 0;
	}

	return -EINVAL;
}

static int sc1777y_validate_session_blocks(const uint8_t *in, size_t in_len)
{
	if (in == NULL || in_len < SC1777Y_BLOCK16_MIN_LEN || in_len > SC1777Y_MAX_DATA_LEN ||
	    (in_len % SC1777Y_BLOCK16_MIN_LEN) != 0U) {
		return -EINVAL;
	}

	return 0;
}

static int sc1777y_session_crypto(const struct device *dev, uint8_t p1, const uint8_t *in,
				  size_t in_len, uint8_t *out, size_t out_size,
				  size_t *out_len)
{
	const struct sc1777y_command cmd = {
		.cla = 0x80,
		.ins = 0x28,
		.p1 = p1,
		.p2 = 0x00,
		.data = in,
		.data_len = in_len,
	};
	size_t actual_out_len;
	int ret;

	if (out_len == NULL) {
		return -EINVAL;
	}

	ret = sc1777y_validate_session_blocks(in, in_len);
	if (ret != 0) {
		return ret;
	}

	if (out_size < in_len) {
		*out_len = in_len;
		return -ENOMEM;
	}

	ret = sc1777y_command(dev, &cmd, out, out_size, &actual_out_len, NULL);
	if (ret != 0) {
		return ret;
	}

	if (actual_out_len != in_len) {
		return -EIO;
	}

	*out_len = actual_out_len;

	return 0;
}

static int sc1777y_validate_sensor_blocks(const uint8_t *in, size_t in_len, size_t max_len)
{
	if (in == NULL || in_len == 0U || in_len > max_len || (in_len % SC1777Y_SENSOR_BLOCK_LEN) != 0U) {
		return -EINVAL;
	}

	return 0;
}

static int sc1777y_sensor_crypto(const struct device *dev, uint8_t ins, uint8_t p1, uint8_t p2,
				 const uint8_t *in, size_t in_len, uint8_t *out, size_t out_size,
				 size_t *out_len)
{
	const struct sc1777y_command cmd = {
		.cla = 0x80,
		.ins = ins,
		.p1 = p1,
		.p2 = p2,
		.data = in,
		.data_len = in_len,
	};
	size_t actual_out_len;
	int ret;

	if (out_len == NULL) {
		return -EINVAL;
	}

	ret = sc1777y_validate_sensor_blocks(in, in_len, SC1777Y_MAX_DATA_LEN);
	if (ret != 0) {
		return ret;
	}

	if (out_size < in_len) {
		*out_len = in_len;
		return -ENOMEM;
	}

	ret = sc1777y_command(dev, &cmd, out, out_size, &actual_out_len, NULL);
	if (ret != 0) {
		return ret;
	}

	if (actual_out_len != in_len) {
		return -EIO;
	}

	*out_len = actual_out_len;

	return 0;
}

static int sc1777y_terminal_sensor_crypto(const struct device *dev, uint8_t ins,
					  enum sc1777y_sensor_type type,
					  const uint8_t sensor_id[SC1777Y_SENSOR_ID_LEN],
					  const uint8_t *in, size_t in_len, uint8_t *out,
					  size_t out_size, size_t *out_len)
{
	uint8_t p2;
	uint8_t payload[SC1777Y_MAX_DATA_LEN];
	struct sc1777y_command cmd = {
		.cla = 0x80,
		.ins = ins,
		.p1 = 0x81,
	};
	size_t actual_out_len;
	int ret;

	if (out_len == NULL) {
		return -EINVAL;
	}

	if (sensor_id == NULL) {
		return -EINVAL;
	}

	ret = sc1777y_sensor_data_p2(type, &p2);
	if (ret != 0) {
		return ret;
	}

	ret = sc1777y_validate_sensor_blocks(in, in_len,
					     SC1777Y_MAX_DATA_LEN - SC1777Y_SENSOR_ID_LEN);
	if (ret != 0) {
		return ret;
	}

	if (out_size < in_len) {
		*out_len = in_len;
		return -ENOMEM;
	}

	memcpy(payload, sensor_id, SC1777Y_SENSOR_ID_LEN);
	memcpy(&payload[SC1777Y_SENSOR_ID_LEN], in, in_len);

	cmd.p2 = p2;
	cmd.data = payload;
	cmd.data_len = SC1777Y_SENSOR_ID_LEN + in_len;

	ret = sc1777y_command(dev, &cmd, out, out_size, &actual_out_len, NULL);
	if (ret != 0) {
		return ret;
	}

	if (actual_out_len != in_len) {
		return -EIO;
	}

	*out_len = actual_out_len;

	return 0;
}

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
				*out_len = payload_len;
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

/***** Sensor user operations *****/

int sc1777y_get_sensor_identity(const struct device *dev, struct sc1777y_identity *identity)
{
	const struct sc1777y_command cmd = {
		.cla = 0x00,
		.ins = 0x36,
		.p1 = 0x00,
		.p2 = 0x00,
	};

	if (identity == NULL) {
		return -EINVAL;
	}

	return sc1777y_command_expect_len(dev, &cmd, (uint8_t *)identity, sizeof(*identity));
}

int sc1777y_encrypt_sensor_challenge(const struct device *dev, const uint8_t rand4[4],
				     uint8_t encrypted8[8])
{
	uint8_t payload[SC1777Y_SENSOR_AUTH_ENCRYPTED_LEN] = {0x00, 0x04};
	const struct sc1777y_command cmd = {
		.cla = 0x00,
		.ins = 0x88,
		.p1 = 0x00,
		.p2 = 0x01,
		.data = payload,
		.data_len = sizeof(payload),
	};

	if (rand4 == NULL || encrypted8 == NULL) {
		return -EINVAL;
	}

	memcpy(&payload[2], rand4, SC1777Y_SENSOR_AUTH_CHALLENGE_LEN);
	payload[6] = 0x80;
	payload[7] = 0x00;

	return sc1777y_command_expect_len(dev, &cmd, encrypted8, SC1777Y_SENSOR_AUTH_ENCRYPTED_LEN);
}

int sc1777y_sensor_encrypt(const struct device *dev, const uint8_t *in, size_t in_len,
			   uint8_t *out, size_t out_size, size_t *out_len)
{
	return sc1777y_sensor_crypto(dev, 0x06, 0x80, 0x01, in, in_len, out, out_size, out_len);
}

int sc1777y_sensor_decrypt_from_terminal(const struct device *dev, const uint8_t *in,
					 size_t in_len, uint8_t *out, size_t out_size,
					 size_t *out_len)
{
	return sc1777y_sensor_crypto(dev, 0x08, 0x80, 0x01, in, in_len, out, out_size, out_len);
}

/***** Terminal user operations *****/

/***** 5.1.1 Identity authentication flow *****/

int sc1777y_get_random(const struct device *dev, uint8_t *out, size_t len)
{
	const struct sc1777y_command cmd = {
		.cla = 0x00,
		.ins = 0x84,
		.p1 = 0x00,
		.p2 = (uint8_t)len,
	};

	if (out == NULL || len == 0U || len > UINT8_MAX) {
		return -EINVAL;
	}

	return sc1777y_command_expect_len(dev, &cmd, out, len);
}

int sc1777y_get_random4(const struct device *dev, uint8_t rand4[4])
{
	return sc1777y_get_random(dev, rand4, 4U);
}

int sc1777y_verify_sensor_auth(const struct device *dev, enum sc1777y_sensor_type type,
			       const uint8_t sensor_id[8], const uint8_t encrypted8[8],
			       uint8_t rand4[4])
{
	uint8_t p2;
	uint8_t payload[SC1777Y_SENSOR_ID_LEN + SC1777Y_SENSOR_AUTH_ENCRYPTED_LEN];
	struct sc1777y_command cmd = {
		.cla = 0x80,
		.ins = 0x08,
		.p1 = 0x01,
	};
	int ret;

	if (sensor_id == NULL || encrypted8 == NULL || rand4 == NULL) {
		return -EINVAL;
	}

	ret = sc1777y_sensor_auth_p2(type, &p2);
	if (ret != 0) {
		return ret;
	}

	memcpy(payload, sensor_id, SC1777Y_SENSOR_ID_LEN);
	memcpy(&payload[SC1777Y_SENSOR_ID_LEN], encrypted8, SC1777Y_SENSOR_AUTH_ENCRYPTED_LEN);
	cmd.p2 = p2;
	cmd.data = payload;
	cmd.data_len = sizeof(payload);

	return sc1777y_command_expect_len(dev, &cmd, rand4, SC1777Y_SENSOR_AUTH_RAND_LEN);
}

/***** 5.1.2 Business data flow *****/

int sc1777y_terminal_decrypt_sensor(const struct device *dev, enum sc1777y_sensor_type type,
				    const uint8_t sensor_id[8], const uint8_t *in,
				    size_t in_len, uint8_t *out, size_t out_size,
				    size_t *out_len)
{
	return sc1777y_terminal_sensor_crypto(dev, 0x08, type, sensor_id, in, in_len, out, out_size,
					       out_len);
}

int sc1777y_terminal_encrypt_sensor(const struct device *dev, enum sc1777y_sensor_type type,
				    const uint8_t sensor_id[8], const uint8_t *in,
				    size_t in_len, uint8_t *out, size_t out_size,
				    size_t *out_len)
{
	return sc1777y_terminal_sensor_crypto(dev, 0x06, type, sensor_id, in, in_len, out, out_size,
					       out_len);
}

/***** 5.2.1 Key update/recovery flow *****/

int sc1777y_get_update_identity(const struct device *dev, struct sc1777y_identity *identity)
{
	const struct sc1777y_command cmd = {
		.cla = 0x00,
		.ins = 0x10,
		.p1 = 0x00,
		.p2 = 0x00,
	};

	if (identity == NULL) {
		return -EINVAL;
	}

	return sc1777y_command_expect_len(dev, &cmd, (uint8_t *)identity, sizeof(*identity));
}

int sc1777y_get_random8(const struct device *dev, uint8_t rand8[8])
{
	return sc1777y_get_random(dev, rand8, 8U);
}

int sc1777y_verify_update_auth(const struct device *dev, const uint8_t encrypted8[8])
{
	const struct sc1777y_command cmd = {
		.cla = 0x00,
		.ins = 0x82,
		.p1 = 0x00,
		.p2 = 0x02,
		.data = encrypted8,
		.data_len = SC1777Y_UPDATE_AUTH_ENCRYPTED_LEN,
	};

	if (encrypted8 == NULL) {
		return -EINVAL;
	}

	return sc1777y_command_expect_empty(dev, &cmd);
}

int sc1777y_apply_key_update(const struct device *dev, const uint8_t *key_data, size_t key_data_len)
{
	const struct sc1777y_command cmd = {
		.cla = 0x80,
		.ins = 0x22,
		.p1 = 0x02,
		.p2 = 0x01,
		.data = key_data,
		.data_len = key_data_len,
	};

	if (key_data == NULL || key_data_len == 0U || key_data_len > SC1777Y_MAX_DATA_LEN) {
		return -EINVAL;
	}

	return sc1777y_command_expect_empty(dev, &cmd);
}

/***** 5.3.1 Platform basic instructions *****/

int sc1777y_get_version_info(const struct device *dev, struct sc1777y_version_info *version)
{
	const struct sc1777y_command cmd = {
		.cla = 0x00,
		.ins = 0x5B,
		.p1 = 0x00,
		.p2 = 0x40,
	};

	if (version == NULL) {
		return -EINVAL;
	}

	return sc1777y_command_expect_len(dev, &cmd, version->bytes, sizeof(version->bytes));
}

int sc1777y_get_serial(const struct device *dev, uint8_t serial[SC1777Y_SERIAL_LEN])
{
	static const uint8_t serial_req[] = {0x00, SC1777Y_SERIAL_LEN};
	const struct sc1777y_command cmd = {
		.cla = 0x00,
		.ins = 0xB0,
		.p1 = 0x99,
		.p2 = 0x00,
		.data = serial_req,
		.data_len = sizeof(serial_req),
	};

	if (serial == NULL) {
		return -EINVAL;
	}

	return sc1777y_command_expect_len(dev, &cmd, serial, SC1777Y_SERIAL_LEN);
}

int sc1777y_import_platform_public_key(const struct device *dev,
				       const uint8_t key64[SC1777Y_PLATFORM_PUBLIC_KEY_LEN])
{
	const struct sc1777y_command cmd = {
		.cla = 0x80,
		.ins = 0x30,
		.p1 = 0x01,
		.p2 = 0x01,
		.data = key64,
		.data_len = SC1777Y_PLATFORM_PUBLIC_KEY_LEN,
	};

	if (key64 == NULL) {
		return -EINVAL;
	}

	return sc1777y_command_expect_empty(dev, &cmd);
}

int sc1777y_import_ak(const struct device *dev, const uint8_t ak16[SC1777Y_AK_LEN])
{
	const struct sc1777y_command cmd = {
		.cla = 0x80,
		.ins = 0x26,
		.p1 = 0x02,
		.p2 = 0x00,
		.data = ak16,
		.data_len = SC1777Y_AK_LEN,
	};

	if (ak16 == NULL) {
		return -EINVAL;
	}

	return sc1777y_command_expect_empty(dev, &cmd);
}

int sc1777y_import_iv(const struct device *dev, const uint8_t iv16[SC1777Y_IV_LEN])
{
	const struct sc1777y_command cmd = {
		.cla = 0x80,
		.ins = 0x26,
		.p1 = 0x04,
		.p2 = 0x00,
		.data = iv16,
		.data_len = SC1777Y_IV_LEN,
	};

	if (iv16 == NULL) {
		return -EINVAL;
	}

	return sc1777y_command_expect_empty(dev, &cmd);
}

/***** 5.3.2 Certificate request flow *****/

int sc1777y_generate_sm2_keypair(const struct device *dev)
{
	const struct sc1777y_command cmd = {
		.cla = 0x80,
		.ins = 0x2C,
		.p1 = 0x00,
		.p2 = 0x00,
	};

	return sc1777y_command_expect_empty(dev, &cmd);
}

int sc1777y_generate_cert_request(const struct device *dev,
				  enum sc1777y_cert_request_format format,
				  const uint8_t *subject, size_t subject_len,
				  uint8_t *out, size_t out_size, size_t *out_len)
{
	const struct sc1777y_command cmd = {
		.cla = 0x80,
		.ins = 0x38,
		.p1 = (uint8_t)format,
		.p2 = 0x00,
		.data = subject,
		.data_len = subject_len,
	};
	uint8_t response[SC1777Y_MAX_DATA_LEN];
	size_t actual_out_len;
	int ret;

	if (out_len == NULL || (subject_len > 0U && subject == NULL) ||
	    (out_size > 0U && out == NULL)) {
		return -EINVAL;
	}

	if (format != SC1777Y_CERT_REQUEST_FORMAT_1 && format != SC1777Y_CERT_REQUEST_FORMAT_2) {
		return -EINVAL;
	}

	ret = sc1777y_command(dev, &cmd, response, sizeof(response), &actual_out_len, NULL);
	if (ret != 0) {
		return ret;
	}

	*out_len = actual_out_len;
	if (out_size < actual_out_len) {
		return -ENOMEM;
	}

	if (actual_out_len > 0U) {
		memcpy(out, response, actual_out_len);
	}

	return 0;
}

/***** 5.3.3 Session negotiation flow *****/

int sc1777y_session_begin(const struct device *dev, uint8_t en_r1[SC1777Y_SESSION_RANDOM_LEN])
{
	const struct sc1777y_command cmd = {
		.cla = 0x80,
		.ins = 0x3A,
		.p1 = 0x01,
		.p2 = 0x00,
	};

	if (en_r1 == NULL) {
		return -EINVAL;
	}

	return sc1777y_command_expect_len(dev, &cmd, en_r1, SC1777Y_SESSION_RANDOM_LEN);
}

int sc1777y_hash(const struct device *dev, enum sc1777y_hash_target target,
		 const uint8_t *data, size_t len, uint8_t hash32[SC1777Y_HASH_LEN])
{
	const struct sc1777y_command cmd = {
		.cla = 0x80,
		.ins = 0x32,
		.p1 = (uint8_t)target,
		.p2 = 0x00,
		.data = data,
		.data_len = len,
	};

	if (hash32 == NULL || data == NULL || len == 0U || len > SC1777Y_MAX_DATA_LEN) {
		return -EINVAL;
	}

	if (target != SC1777Y_HASH_REQUEST && target != SC1777Y_HASH_RESPONSE) {
		return -EINVAL;
	}

	return sc1777y_command_expect_len(dev, &cmd, hash32, SC1777Y_HASH_LEN);
}

int sc1777y_sign_hash(const struct device *dev, const uint8_t hash32[SC1777Y_HASH_LEN],
		      uint8_t signature64[SC1777Y_SIGNATURE_LEN])
{
	const struct sc1777y_command cmd = {
		.cla = 0x80,
		.ins = 0x34,
		.p1 = 0x00,
		.p2 = 0x00,
		.data = hash32,
		.data_len = SC1777Y_HASH_LEN,
	};

	if (hash32 == NULL || signature64 == NULL) {
		return -EINVAL;
	}

	return sc1777y_command_expect_len(dev, &cmd, signature64, SC1777Y_SIGNATURE_LEN);
}

int sc1777y_verify_signature(const struct device *dev, const uint8_t hash32[SC1777Y_HASH_LEN],
			     const uint8_t signature64[SC1777Y_SIGNATURE_LEN])
{
	uint8_t payload[SC1777Y_HASH_LEN + SC1777Y_SIGNATURE_LEN];
	struct sc1777y_command cmd = {
		.cla = 0x80,
		.ins = 0x36,
		.p1 = 0x00,
		.p2 = 0x01,
		.data = payload,
		.data_len = sizeof(payload),
	};

	if (hash32 == NULL || signature64 == NULL) {
		return -EINVAL;
	}

	memcpy(payload, hash32, SC1777Y_HASH_LEN);
	memcpy(&payload[SC1777Y_HASH_LEN], signature64, SC1777Y_SIGNATURE_LEN);

	return sc1777y_command_expect_empty(dev, &cmd);
}

int sc1777y_generate_auth_response(const struct device *dev,
				   const uint8_t factor32[SC1777Y_AUTH_FACTOR_LEN],
				   uint8_t response146[SC1777Y_AUTH_RESPONSE_LEN])
{
	const struct sc1777y_command cmd = {
		.cla = 0x80,
		.ins = 0x2A,
		.p1 = 0x01,
		.p2 = 0x04,
		.data = factor32,
		.data_len = SC1777Y_AUTH_FACTOR_LEN,
	};

	if (factor32 == NULL || response146 == NULL) {
		return -EINVAL;
	}

	return sc1777y_command_expect_len(dev, &cmd, response146, SC1777Y_AUTH_RESPONSE_LEN);
}

int sc1777y_session_confirm(const struct device *dev,
			    const uint8_t en_r2_128[SC1777Y_SESSION_RANDOM_LEN],
			    uint8_t dkhash32[SC1777Y_SESSION_DKHASH_LEN])
{
	const struct sc1777y_command cmd = {
		.cla = 0x80,
		.ins = 0x3C,
		.p1 = 0x00,
		.p2 = 0x00,
		.data = en_r2_128,
		.data_len = SC1777Y_SESSION_RANDOM_LEN,
	};

	if (en_r2_128 == NULL || dkhash32 == NULL) {
		return -EINVAL;
	}

	return sc1777y_command_expect_len(dev, &cmd, dkhash32, SC1777Y_SESSION_DKHASH_LEN);
}

/***** 5.3.4 Session-key encryption flow *****/

int sc1777y_session_encrypt(const struct device *dev, const uint8_t *in, size_t in_len,
			    uint8_t *out, size_t out_size, size_t *out_len)
{
	return sc1777y_session_crypto(dev, 0x80, in, in_len, out, out_size, out_len);
}

/***** 5.3.5 Session-key decryption flow *****/

int sc1777y_session_decrypt(const struct device *dev, const uint8_t *in, size_t in_len,
			    uint8_t *out, size_t out_size, size_t *out_len)
{
	return sc1777y_session_crypto(dev, 0x81, in, in_len, out, out_size, out_len);
}

/***** 5.3.6 Platform type selection flow *****/

int sc1777y_set_platform_type(const struct device *dev, enum sc1777y_platform_type type)
{
	const struct sc1777y_command cmd = {
		.cla = 0x80,
		.ins = 0x3E,
		.p1 = 0x00,
		.p2 = (uint8_t)type,
	};

	if (type != SC1777Y_PLATFORM_NANRUI && type != SC1777Y_PLATFORM_WANGAN) {
		return -EINVAL;
	}

	return sc1777y_command_expect_empty(dev, &cmd);
}

int sc1777y_get_platform_type(const struct device *dev, enum sc1777y_platform_type *type)
{
	const struct sc1777y_command cmd = {
		.cla = 0x80,
		.ins = 0x3E,
		.p1 = 0x01,
		.p2 = 0x00,
	};
	uint8_t raw_type;
	int ret;

	if (type == NULL) {
		return -EINVAL;
	}

	ret = sc1777y_command_expect_len(dev, &cmd, &raw_type, SC1777Y_PLATFORM_TYPE_LEN);
	if (ret != 0) {
		return ret;
	}

	if (raw_type != SC1777Y_PLATFORM_UNSET && raw_type != SC1777Y_PLATFORM_NANRUI &&
	    raw_type != SC1777Y_PLATFORM_WANGAN) {
		return -EIO;
	}

	*type = (enum sc1777y_platform_type)raw_type;

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
