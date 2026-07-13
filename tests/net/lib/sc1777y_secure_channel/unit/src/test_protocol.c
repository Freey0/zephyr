/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include "secure_channel_internal.h"
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#define REQUEST_BODY_FIXED_LEN 170U
#define REQUEST_FIXED_LEN 234U
#define RESPONSE_LEN 230U
#define CONFIRM_LEN 184U
#define RECORD_FIXED_LEN 20U

static void fill_sequence(uint8_t *buf, size_t len, uint8_t first)
{
	for (size_t i = 0U; i < len; ++i) {
		buf[i] = first + i;
	}
}

ZTEST(sc1777y_secure_protocol, test_pad_full_block_adds_another_block)
{
	uint8_t buf[32] = {0};
	size_t padded_len = 0;

	zassert_ok(sc1777y_secure_pad(buf, 16, sizeof(buf), &padded_len));
	zassert_equal(32, padded_len);
	zassert_equal(0x80, buf[16]);
	zassert_mem_equal((uint8_t[15]) { 0 }, &buf[17], 15);
}

ZTEST(sc1777y_secure_protocol, test_pad_and_unpad_boundary_lengths)
{
	static const size_t lengths[] = {1U, 15U, 16U, 17U, 2047U};
	static const uint8_t zeros[16];
	uint8_t buf[SC1777Y_SECURE_MAX_CIPHERTEXT_LEN];

	for (size_t i = 0U; i < ARRAY_SIZE(lengths); ++i) {
		size_t padded_len = 0U;
		size_t plain_len = 0U;
		size_t expected_len = ROUND_UP(lengths[i] + 1U, SC1777Y_IV_LEN);

		memset(buf, 0x5a, sizeof(buf));
		zassert_ok(sc1777y_secure_pad(buf, lengths[i], sizeof(buf), &padded_len));
		zassert_equal(expected_len, padded_len);
		zassert_equal(0x80, buf[lengths[i]]);
		zassert_mem_equal(zeros, &buf[lengths[i] + 1U],
				  padded_len - lengths[i] - 1U);
		zassert_ok(sc1777y_secure_unpad(buf, padded_len, &plain_len));
		zassert_equal(lengths[i], plain_len);
	}
}

ZTEST(sc1777y_secure_protocol, test_padding_rejects_truncation_and_malformed_data)
{
	uint8_t short_buf[16] = {0};
	uint8_t all_zero[16] = {0};
	uint8_t wrong_marker[16] = {0x55};
	uint8_t excessive_padding[32] = {[0] = 0x80};
	size_t len = 0U;

	zassert_equal(-EMSGSIZE,
		      sc1777y_secure_pad(short_buf, sizeof(short_buf), sizeof(short_buf), &len));
	zassert_equal(-EBADMSG, sc1777y_secure_unpad(all_zero, sizeof(all_zero), &len));
	zassert_equal(-EBADMSG, sc1777y_secure_unpad(wrong_marker, sizeof(wrong_marker), &len));
	zassert_equal(-EBADMSG, sc1777y_secure_unpad(wrong_marker, 15U, &len));
	zassert_equal(-EBADMSG,
		      sc1777y_secure_unpad(excessive_padding, sizeof(excessive_padding), &len));
}

ZTEST(sc1777y_secure_protocol, test_request_golden_layout_and_network_byte_order)
{
	static const uint8_t certificate[] = {0xc0, 0xc1, 0xc2};
	static const uint8_t expected_header[] = {
		0x01, 0x01, 0x00, 0xed, 0x01, 0x00, 0x12, 0x34,
	};
	static const uint8_t signature[SC1777Y_SIGNATURE_LEN] = {[0] = 0xa5, [63] = 0x5a};
	struct sc1777y_secure_channel_config config = {
		.certificate = certificate,
		.certificate_len = sizeof(certificate),
	};
	uint8_t en_r1[SC1777Y_SESSION_RANDOM_LEN];
	uint8_t request[REQUEST_FIXED_LEN + sizeof(certificate)];
	size_t body_len = 0U;
	size_t request_len = 0U;

	fill_sequence(config.sim, sizeof(config.sim), 0x10);
	fill_sequence(config.device_id, sizeof(config.device_id), 0x30);
	fill_sequence(en_r1, sizeof(en_r1), 0x80);
	zassert_ok(sc1777y_secure_encode_request_body(&config, 0x1234, en_r1, request,
						       sizeof(request), &body_len));
	zassert_equal(REQUEST_BODY_FIXED_LEN + sizeof(certificate), body_len);
	zassert_mem_equal(expected_header, request, sizeof(expected_header));
	zassert_mem_equal(config.sim, &request[8], sizeof(config.sim));
	zassert_mem_equal(config.device_id, &request[24], sizeof(config.device_id));
	zassert_mem_equal(certificate, &request[42], sizeof(certificate));
	zassert_mem_equal(en_r1, &request[45], sizeof(en_r1));

	zassert_ok(sc1777y_secure_append_signature(request, sizeof(request), body_len,
						    signature, &request_len));
	zassert_equal(REQUEST_FIXED_LEN + sizeof(certificate), request_len);
	zassert_mem_equal(signature, &request[body_len], sizeof(signature));
}

ZTEST(sc1777y_secure_protocol, test_request_rejects_truncation)
{
	static const uint8_t certificate[] = {1U};
	struct sc1777y_secure_channel_config config = {
		.certificate = certificate,
		.certificate_len = sizeof(certificate),
	};
	uint8_t en_r1[SC1777Y_SESSION_RANDOM_LEN] = {0};
	uint8_t request[REQUEST_BODY_FIXED_LEN];
	uint8_t signature[SC1777Y_SIGNATURE_LEN] = {0};
	size_t len = 0U;

	zassert_equal(-EMSGSIZE,
		      sc1777y_secure_encode_request_body(&config, 1U, en_r1, request,
							 sizeof(request), &len));
	config.certificate_len = SC1777Y_SECURE_MAX_CERTIFICATE_LEN + 1U;
	zassert_equal(-EMSGSIZE,
		      sc1777y_secure_encode_request_body(&config, 1U, en_r1, request,
							 sizeof(request), &len));
	zassert_equal(-EMSGSIZE,
		      sc1777y_secure_append_signature(request, sizeof(request), sizeof(request),
						      signature, &len));
}

ZTEST(sc1777y_secure_protocol, test_decode_response_golden_layout)
{
	uint8_t response[RESPONSE_LEN] = {0x01, 0x02, 0x00, 0xe6, 0x12, 0x35};
	struct sc1777y_secure_handshake_response parsed;

	fill_sequence(&response[6], SC1777Y_AUTH_FACTOR_LEN, 0x10);
	fill_sequence(&response[38], SC1777Y_SESSION_RANDOM_LEN, 0x40);
	fill_sequence(&response[166], SC1777Y_SIGNATURE_LEN, 0xc0);
	zassert_ok(sc1777y_secure_decode_response(response, sizeof(response), 0x1234,
						    &parsed));
	zassert_equal(0x1235, parsed.sn);
	zassert_mem_equal(&response[6], parsed.auth_factor, sizeof(parsed.auth_factor));
	zassert_mem_equal(&response[38], parsed.en_r2, sizeof(parsed.en_r2));
	zassert_mem_equal(&response[166], parsed.signature, sizeof(parsed.signature));
}

ZTEST(sc1777y_secure_protocol, test_decode_response_rejects_wrong_sn)
{
	uint8_t response[230] = {0x01, 0x02, 0x00, 0xE6, 0x12, 0x35};
	struct sc1777y_secure_handshake_response parsed;

	zassert_equal(-EPROTO,
		sc1777y_secure_decode_response(response, sizeof(response), 0x1233, &parsed));
}

ZTEST(sc1777y_secure_protocol, test_decode_response_rejects_wrong_header_or_size)
{
	uint8_t response[RESPONSE_LEN] = {0x01, 0x02, 0x00, 0xe6, 0x00, 0x02};
	struct sc1777y_secure_handshake_response parsed;

	zassert_equal(-EPROTO,
		      sc1777y_secure_decode_response(response, sizeof(response) - 1U, 1U,
						      &parsed));
	response[0] = 2U;
	zassert_equal(-EPROTO,
		      sc1777y_secure_decode_response(response, sizeof(response), 1U, &parsed));
	response[0] = 1U;
	response[1] = 3U;
	zassert_equal(-EPROTO,
		      sc1777y_secure_decode_response(response, sizeof(response), 1U, &parsed));
	response[1] = 2U;
	response[3] = 0xe5;
	zassert_equal(-EPROTO,
		      sc1777y_secure_decode_response(response, sizeof(response), 1U, &parsed));
}

ZTEST(sc1777y_secure_protocol, test_confirm_golden_layout_and_network_byte_order)
{
	static const uint8_t expected_header[] = {0x01, 0x03, 0x00, 0xb8, 0x12, 0x36};
	uint8_t auth_result[SC1777Y_AUTH_RESPONSE_LEN];
	uint8_t dk_hash[SC1777Y_SESSION_DKHASH_LEN];
	uint8_t confirm[CONFIRM_LEN];
	size_t confirm_len = 0U;

	fill_sequence(auth_result, sizeof(auth_result), 0x10);
	fill_sequence(dk_hash, sizeof(dk_hash), 0xb0);
	zassert_ok(sc1777y_secure_encode_confirm(0x1234, auth_result, dk_hash, confirm,
						  sizeof(confirm), &confirm_len));
	zassert_equal(CONFIRM_LEN, confirm_len);
	zassert_mem_equal(expected_header, confirm, sizeof(expected_header));
	zassert_mem_equal(auth_result, &confirm[6], sizeof(auth_result));
	zassert_mem_equal(dk_hash, &confirm[152], sizeof(dk_hash));

	zassert_equal(-EMSGSIZE,
		      sc1777y_secure_encode_confirm(0x1234, auth_result, dk_hash, confirm,
						     sizeof(confirm) - 1U, &confirm_len));
}

ZTEST(sc1777y_secure_protocol, test_record_golden_layout_and_network_byte_order)
{
	static const uint8_t expected_header[] = {0x02, 0x00, 0x00, 0x34};
	uint8_t iv[SC1777Y_IV_LEN];
	uint8_t ciphertext[32];
	uint8_t record[RECORD_FIXED_LEN + sizeof(ciphertext)];
	size_t record_len = 0U;
	size_t decoded_record_len = 0U;
	size_t decoded_ciphertext_len = 0U;

	fill_sequence(iv, sizeof(iv), 0x20);
	fill_sequence(ciphertext, sizeof(ciphertext), 0x60);
	zassert_ok(sc1777y_secure_encode_record(iv, ciphertext, sizeof(ciphertext), record,
						 sizeof(record), &record_len));
	zassert_equal(RECORD_FIXED_LEN + sizeof(ciphertext), record_len);
	zassert_mem_equal(expected_header, record, sizeof(expected_header));
	zassert_mem_equal(iv, &record[4], sizeof(iv));
	zassert_mem_equal(ciphertext, &record[20], sizeof(ciphertext));
	zassert_ok(sc1777y_secure_decode_record_header(record, &decoded_record_len,
							 &decoded_ciphertext_len));
	zassert_equal(record_len, decoded_record_len);
	zassert_equal(sizeof(ciphertext), decoded_ciphertext_len);
}

ZTEST(sc1777y_secure_protocol, test_record_rejects_invalid_headers_and_lengths)
{
	uint8_t header[4] = {0x02, 0x00, 0x00, 0x24};
	uint8_t iv[SC1777Y_IV_LEN] = {0};
	uint8_t ciphertext[17] = {0};
	uint8_t record[RECORD_FIXED_LEN + sizeof(ciphertext)];
	size_t record_len = 0U;
	size_t ciphertext_len = 0U;

	header[0] = 1U;
	zassert_equal(-EPROTO,
		      sc1777y_secure_decode_record_header(header, &record_len, &ciphertext_len));
	header[0] = 2U;
	header[1] = 1U;
	zassert_equal(-EPROTO,
		      sc1777y_secure_decode_record_header(header, &record_len, &ciphertext_len));
	header[1] = 0U;
	sys_put_be16(RECORD_FIXED_LEN + 15U, &header[2]);
	zassert_equal(-EBADMSG,
		      sc1777y_secure_decode_record_header(header, &record_len, &ciphertext_len));
	sys_put_be16(RECORD_FIXED_LEN - 1U, &header[2]);
	zassert_equal(-EPROTO,
		      sc1777y_secure_decode_record_header(header, &record_len, &ciphertext_len));
	zassert_equal(-EINVAL,
		      sc1777y_secure_encode_record(iv, ciphertext, sizeof(ciphertext), record,
						    sizeof(record), &record_len));
	zassert_equal(-EMSGSIZE,
		      sc1777y_secure_encode_record(iv, ciphertext, 16U, record,
						    RECORD_FIXED_LEN + 15U, &record_len));
}

ZTEST_SUITE(sc1777y_secure_protocol, NULL, NULL, NULL, NULL, NULL);
