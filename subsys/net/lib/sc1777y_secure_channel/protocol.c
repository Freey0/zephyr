/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/sys/byteorder.h>

#include "secure_channel_internal.h"

#define SC1777Y_SECURE_HANDSHAKE_TYPE 1U
#define SC1777Y_SECURE_REQUEST_SUBTYPE 1U
#define SC1777Y_SECURE_RESPONSE_SUBTYPE 2U
#define SC1777Y_SECURE_CONFIRM_SUBTYPE 3U
#define SC1777Y_SECURE_RECORD_TYPE 2U
#define SC1777Y_SECURE_RECORD_SUBTYPE 0U

#define SC1777Y_SECURE_VERSION 0x0100U
#define SC1777Y_SECURE_HEADER_LEN 4U
#define SC1777Y_SECURE_REQUEST_PREFIX_LEN 42U
#define SC1777Y_SECURE_REQUEST_BODY_FIXED_LEN 170U
#define SC1777Y_SECURE_REQUEST_FIXED_LEN 234U
#define SC1777Y_SECURE_RESPONSE_LEN 230U
#define SC1777Y_SECURE_CONFIRM_LEN 184U
#define SC1777Y_SECURE_RECORD_FIXED_LEN 20U

int sc1777y_secure_pad(uint8_t *buf, size_t plain_len, size_t capacity,
		       size_t *padded_len)
{
	size_t padding_len;

	if ((buf == NULL) || (padded_len == NULL)) {
		return -EINVAL;
	}

	if (plain_len > SC1777Y_SECURE_MAX_PLAINTEXT_CHUNK) {
		return -EMSGSIZE;
	}

	padding_len = SC1777Y_IV_LEN - (plain_len % SC1777Y_IV_LEN);
	if ((plain_len > capacity) || (capacity - plain_len < padding_len)) {
		return -EMSGSIZE;
	}

	buf[plain_len] = 0x80U;
	memset(&buf[plain_len + 1U], 0, padding_len - 1U);
	*padded_len = plain_len + padding_len;

	return 0;
}

int sc1777y_secure_unpad(uint8_t *buf, size_t padded_len, size_t *plain_len)
{
	size_t marker;

	if ((buf == NULL) || (plain_len == NULL)) {
		return -EINVAL;
	}

	if ((padded_len == 0U) || (padded_len > SC1777Y_SECURE_MAX_CIPHERTEXT_LEN) ||
	    ((padded_len % SC1777Y_IV_LEN) != 0U)) {
		return -EBADMSG;
	}

	marker = padded_len;
	while ((marker > 0U) && (buf[marker - 1U] == 0U)) {
		--marker;
	}

	if ((marker == 0U) || (buf[marker - 1U] != 0x80U) ||
	    (padded_len - marker >= SC1777Y_IV_LEN)) {
		return -EBADMSG;
	}

	*plain_len = marker - 1U;
	return 0;
}

int sc1777y_secure_encode_request_body(
	const struct sc1777y_secure_channel_config *config, uint16_t sn,
	const uint8_t en_r1[SC1777Y_SESSION_RANDOM_LEN],
	uint8_t *out, size_t out_size, size_t *out_len)
{
	size_t body_len;
	size_t request_len;

	if ((config == NULL) || (en_r1 == NULL) || (out == NULL) || (out_len == NULL) ||
	    (config->certificate == NULL) || (config->certificate_len == 0U)) {
		return -EINVAL;
	}

	if (config->certificate_len > SC1777Y_SECURE_MAX_CERTIFICATE_LEN) {
		return -EMSGSIZE;
	}

	body_len = SC1777Y_SECURE_REQUEST_BODY_FIXED_LEN + config->certificate_len;
	request_len = SC1777Y_SECURE_REQUEST_FIXED_LEN + config->certificate_len;
	if (out_size < body_len) {
		return -EMSGSIZE;
	}

	out[0] = SC1777Y_SECURE_HANDSHAKE_TYPE;
	out[1] = SC1777Y_SECURE_REQUEST_SUBTYPE;
	sys_put_be16((uint16_t)request_len, &out[2]);
	sys_put_be16(SC1777Y_SECURE_VERSION, &out[4]);
	sys_put_be16(sn, &out[6]);
	memcpy(&out[8], config->sim, sizeof(config->sim));
	memcpy(&out[24], config->device_id, sizeof(config->device_id));
	memcpy(&out[SC1777Y_SECURE_REQUEST_PREFIX_LEN], config->certificate,
	       config->certificate_len);
	memcpy(&out[SC1777Y_SECURE_REQUEST_PREFIX_LEN + config->certificate_len], en_r1,
	       SC1777Y_SESSION_RANDOM_LEN);
	*out_len = body_len;

	return 0;
}

int sc1777y_secure_append_signature(
	uint8_t *request, size_t request_size, size_t body_len,
	const uint8_t signature[SC1777Y_SIGNATURE_LEN], size_t *request_len)
{
	if ((request == NULL) || (signature == NULL) || (request_len == NULL)) {
		return -EINVAL;
	}

	if ((body_len > SC1777Y_SECURE_MAX_HANDSHAKE_LEN - SC1777Y_SIGNATURE_LEN) ||
	    (body_len > request_size) ||
	    (request_size - body_len < SC1777Y_SIGNATURE_LEN)) {
		return -EMSGSIZE;
	}

	memcpy(&request[body_len], signature, SC1777Y_SIGNATURE_LEN);
	*request_len = body_len + SC1777Y_SIGNATURE_LEN;

	return 0;
}

int sc1777y_secure_decode_response(
	const uint8_t *frame, size_t frame_len, uint16_t request_sn,
	struct sc1777y_secure_handshake_response *response)
{
	uint16_t response_sn;

	if ((frame == NULL) || (response == NULL)) {
		return -EINVAL;
	}

	if ((frame_len != SC1777Y_SECURE_RESPONSE_LEN) ||
	    (frame[0] != SC1777Y_SECURE_HANDSHAKE_TYPE) ||
	    (frame[1] != SC1777Y_SECURE_RESPONSE_SUBTYPE) ||
	    (sys_get_be16(&frame[2]) != SC1777Y_SECURE_RESPONSE_LEN)) {
		return -EPROTO;
	}

	response_sn = sys_get_be16(&frame[4]);
	if (response_sn != (uint16_t)(request_sn + 1U)) {
		return -EPROTO;
	}

	response->sn = response_sn;
	memcpy(response->auth_factor, &frame[6], sizeof(response->auth_factor));
	memcpy(response->en_r2, &frame[38], sizeof(response->en_r2));
	memcpy(response->signature, &frame[166], sizeof(response->signature));

	return 0;
}

int sc1777y_secure_encode_confirm(
	uint16_t request_sn,
	const uint8_t auth_result[SC1777Y_AUTH_RESPONSE_LEN],
	const uint8_t dk_hash[SC1777Y_SESSION_DKHASH_LEN],
	uint8_t *out, size_t out_size, size_t *out_len)
{
	if ((auth_result == NULL) || (dk_hash == NULL) || (out == NULL) || (out_len == NULL)) {
		return -EINVAL;
	}

	if (out_size < SC1777Y_SECURE_CONFIRM_LEN) {
		return -EMSGSIZE;
	}

	out[0] = SC1777Y_SECURE_HANDSHAKE_TYPE;
	out[1] = SC1777Y_SECURE_CONFIRM_SUBTYPE;
	sys_put_be16(SC1777Y_SECURE_CONFIRM_LEN, &out[2]);
	sys_put_be16((uint16_t)(request_sn + 2U), &out[4]);
	memcpy(&out[6], auth_result, SC1777Y_AUTH_RESPONSE_LEN);
	memcpy(&out[152], dk_hash, SC1777Y_SESSION_DKHASH_LEN);
	*out_len = SC1777Y_SECURE_CONFIRM_LEN;

	return 0;
}

int sc1777y_secure_encode_record(
	const uint8_t iv[SC1777Y_IV_LEN], const uint8_t *ciphertext,
	size_t ciphertext_len, uint8_t *out, size_t out_size, size_t *out_len)
{
	size_t record_len;

	if ((iv == NULL) || (ciphertext == NULL) || (out == NULL) || (out_len == NULL) ||
	    (ciphertext_len == 0U) || ((ciphertext_len % SC1777Y_IV_LEN) != 0U)) {
		return -EINVAL;
	}

	if (ciphertext_len > SC1777Y_SECURE_MAX_CIPHERTEXT_LEN) {
		return -EMSGSIZE;
	}

	record_len = SC1777Y_SECURE_RECORD_FIXED_LEN + ciphertext_len;
	if (out_size < record_len) {
		return -EMSGSIZE;
	}

	out[0] = SC1777Y_SECURE_RECORD_TYPE;
	out[1] = SC1777Y_SECURE_RECORD_SUBTYPE;
	sys_put_be16((uint16_t)record_len, &out[2]);
	memcpy(&out[SC1777Y_SECURE_HEADER_LEN], iv, SC1777Y_IV_LEN);
	memcpy(&out[SC1777Y_SECURE_RECORD_FIXED_LEN], ciphertext, ciphertext_len);
	*out_len = record_len;

	return 0;
}

int sc1777y_secure_decode_record_header(
	const uint8_t header[4], size_t *record_len, size_t *ciphertext_len)
{
	size_t decoded_record_len;
	size_t decoded_ciphertext_len;

	if ((header == NULL) || (record_len == NULL) || (ciphertext_len == NULL)) {
		return -EINVAL;
	}

	if ((header[0] != SC1777Y_SECURE_RECORD_TYPE) ||
	    (header[1] != SC1777Y_SECURE_RECORD_SUBTYPE)) {
		return -EPROTO;
	}

	decoded_record_len = sys_get_be16(&header[2]);
	if ((decoded_record_len < SC1777Y_SECURE_RECORD_FIXED_LEN) ||
	    (decoded_record_len > SC1777Y_SECURE_MAX_RECORD_LEN)) {
		return -EPROTO;
	}

	decoded_ciphertext_len = decoded_record_len - SC1777Y_SECURE_RECORD_FIXED_LEN;
	if ((decoded_ciphertext_len == 0U) ||
	    ((decoded_ciphertext_len % SC1777Y_IV_LEN) != 0U)) {
		return -EBADMSG;
	}

	*record_len = decoded_record_len;
	*ciphertext_len = decoded_ciphertext_len;

	return 0;
}
