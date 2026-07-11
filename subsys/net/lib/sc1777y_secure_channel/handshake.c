/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/sys/byteorder.h>

#include "secure_channel_internal.h"

#define SC1777Y_SECURE_RESPONSE_LEN 230U
#define SC1777Y_SECURE_RESPONSE_BODY_LEN 166U

int sc1777y_secure_handshake(struct sc1777y_secure_channel *channel)
{
	const struct device *dev = channel->config.sc1777y;
	uint8_t en_r1[SC1777Y_SESSION_RANDOM_LEN] = {0};
	uint8_t sn_bytes[sizeof(uint16_t)] = {0};
	uint8_t request_hash[SC1777Y_HASH_LEN] = {0};
	uint8_t request_signature[SC1777Y_SIGNATURE_LEN] = {0};
	uint8_t response_frame[SC1777Y_SECURE_RESPONSE_LEN] = {0};
	uint8_t response_hash[SC1777Y_HASH_LEN] = {0};
	uint8_t auth_result[SC1777Y_AUTH_RESPONSE_LEN] = {0};
	uint8_t dk_hash[SC1777Y_SESSION_DKHASH_LEN] = {0};
	struct sc1777y_secure_handshake_response response = {0};
	size_t request_body_len;
	size_t request_len;
	size_t confirm_len;
	uint16_t request_sn;
	bool crypto_locked = false;
	int ret;

	channel->state = SC1777Y_SECURE_CHANNEL_NEGOTIATING;
	ret = k_mutex_lock(&channel->crypto_lock, K_FOREVER);
	if (ret < 0) {
		goto out;
	}
	crypto_locked = true;

	ret = sc1777y_set_platform_type(dev, channel->config.platform_type);
	if (ret < 0) {
		goto out;
	}
	ret = sc1777y_import_platform_public_key(dev, channel->config.platform_public_key);
	if (ret < 0) {
		goto out;
	}
	ret = sc1777y_session_begin(dev, en_r1);
	if (ret < 0) {
		goto out;
	}
	ret = sc1777y_get_random(dev, sn_bytes, sizeof(sn_bytes));
	if (ret < 0) {
		goto out;
	}

	request_sn = sys_get_be16(sn_bytes);
	ret = sc1777y_secure_encode_request_body(&channel->config, request_sn, en_r1,
					  channel->tx_work, sizeof(channel->tx_work),
					  &request_body_len);
	if (ret < 0) {
		goto out;
	}
	ret = sc1777y_hash(dev, SC1777Y_HASH_REQUEST, channel->tx_work, request_body_len,
			   request_hash);
	if (ret < 0) {
		goto out;
	}
	ret = sc1777y_sign_hash(dev, request_hash, request_signature);
	if (ret < 0) {
		goto out;
	}

	ret = sc1777y_secure_append_signature(channel->tx_work, sizeof(channel->tx_work),
					       request_body_len, request_signature, &request_len);
	if (ret < 0) {
		goto out;
	}
	ret = sc1777y_secure_socket_send_all(channel, channel->tx_work, request_len);
	if (ret < 0) {
		goto out;
	}
	ret = sc1777y_secure_socket_recv_exact(channel, response_frame, sizeof(response_frame));
	if (ret < 0) {
		goto out;
	}
	ret = sc1777y_secure_decode_response(response_frame, sizeof(response_frame), request_sn,
					      &response);
	if (ret < 0) {
		goto out;
	}

	ret = sc1777y_hash(dev, SC1777Y_HASH_RESPONSE, response_frame,
			   SC1777Y_SECURE_RESPONSE_BODY_LEN, response_hash);
	if (ret < 0) {
		goto out;
	}
	ret = sc1777y_verify_signature(dev, response_hash, response.signature);
	if (ret < 0) {
		goto out;
	}
	ret = sc1777y_generate_auth_response(dev, response.auth_factor, auth_result);
	if (ret < 0) {
		goto out;
	}
	ret = sc1777y_session_confirm(dev, response.en_r2, dk_hash);
	if (ret < 0) {
		goto out;
	}

	ret = sc1777y_secure_encode_confirm(request_sn, auth_result, dk_hash, channel->tx_work,
					    sizeof(channel->tx_work), &confirm_len);
	if (ret < 0) {
		goto out;
	}
	ret = sc1777y_secure_socket_send_all(channel, channel->tx_work, confirm_len);
	if (ret == 0) {
		channel->state = SC1777Y_SECURE_CHANNEL_ESTABLISHED;
	}

out:
	sc1777y_secure_zero(en_r1, sizeof(en_r1));
	sc1777y_secure_zero(sn_bytes, sizeof(sn_bytes));
	sc1777y_secure_zero(request_hash, sizeof(request_hash));
	sc1777y_secure_zero(request_signature, sizeof(request_signature));
	sc1777y_secure_zero(response_frame, sizeof(response_frame));
	sc1777y_secure_zero(response_hash, sizeof(response_hash));
	sc1777y_secure_zero(auth_result, sizeof(auth_result));
	sc1777y_secure_zero(dk_hash, sizeof(dk_hash));
	sc1777y_secure_zero(&response, sizeof(response));
	sc1777y_secure_zero(channel->tx_work, sizeof(channel->tx_work));
	if (crypto_locked) {
		(void)k_mutex_unlock(&channel->crypto_lock);
	}
	return ret < 0 ? sc1777y_secure_channel_fail(channel, ret) : 0;
}
