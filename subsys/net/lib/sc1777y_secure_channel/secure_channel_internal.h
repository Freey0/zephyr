/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SUBSYS_NET_LIB_SC1777Y_SECURE_CHANNEL_INTERNAL_H_
#define ZEPHYR_SUBSYS_NET_LIB_SC1777Y_SECURE_CHANNEL_INTERNAL_H_

#include <stddef.h>
#include <stdint.h>

#include <zephyr/net/sc1777y_secure_channel.h>

struct sc1777y_secure_handshake_response {
	uint16_t sn;
	uint8_t auth_factor[SC1777Y_AUTH_FACTOR_LEN];
	uint8_t en_r2[SC1777Y_SESSION_RANDOM_LEN];
	uint8_t signature[SC1777Y_SIGNATURE_LEN];
};

int sc1777y_secure_socket_connect(struct sc1777y_secure_channel *channel);
int sc1777y_secure_socket_send_all(struct sc1777y_secure_channel *channel,
				   const uint8_t *data, size_t len);
int sc1777y_secure_socket_recv_exact(struct sc1777y_secure_channel *channel,
				     uint8_t *data, size_t len);
void sc1777y_secure_socket_close(struct sc1777y_secure_channel *channel);
void sc1777y_secure_channel_clear_rx(struct sc1777y_secure_channel *channel);
int sc1777y_secure_channel_fail(struct sc1777y_secure_channel *channel, int ret);
int sc1777y_secure_handshake(struct sc1777y_secure_channel *channel);

int sc1777y_secure_pad(uint8_t *buf, size_t plain_len, size_t capacity,
		       size_t *padded_len);
int sc1777y_secure_unpad(uint8_t *buf, size_t padded_len, size_t *plain_len);
int sc1777y_secure_encode_request_body(
	const struct sc1777y_secure_channel_config *config, uint16_t sn,
	const uint8_t en_r1[SC1777Y_SESSION_RANDOM_LEN],
	uint8_t *out, size_t out_size, size_t *out_len);
int sc1777y_secure_append_signature(
	uint8_t *request, size_t request_size, size_t body_len,
	const uint8_t signature[SC1777Y_SIGNATURE_LEN], size_t *request_len);
int sc1777y_secure_decode_response(
	const uint8_t *frame, size_t frame_len, uint16_t request_sn,
	struct sc1777y_secure_handshake_response *response);
int sc1777y_secure_encode_confirm(
	uint16_t request_sn,
	const uint8_t auth_result[SC1777Y_AUTH_RESPONSE_LEN],
	const uint8_t dk_hash[SC1777Y_SESSION_DKHASH_LEN],
	uint8_t *out, size_t out_size, size_t *out_len);
int sc1777y_secure_encode_record(
	const uint8_t iv[SC1777Y_IV_LEN], const uint8_t *ciphertext,
	size_t ciphertext_len, uint8_t *out, size_t out_size, size_t *out_len);
int sc1777y_secure_decode_record_header(
	const uint8_t header[4], size_t *record_len, size_t *ciphertext_len);

#endif /* ZEPHYR_SUBSYS_NET_LIB_SC1777Y_SECURE_CHANNEL_INTERNAL_H_ */
