/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>

#include <zephyr/drivers/misc/sc1777y.h>
#include <zephyr/drivers/misc/sc1777y_emul.h>
#include <zephyr/ztest.h>

#include "fixture.h"

static uint8_t expected_lrc(const uint8_t *buf, size_t len)
{
	uint8_t x = 0U;

	for (size_t i = 0; i < len; i++) {
		x ^= buf[i];
	}

	return (uint8_t)~x;
}

static void fill_incrementing(uint8_t *buf, size_t len, uint8_t base)
{
	for (size_t i = 0; i < len; i++) {
		buf[i] = base + i;
	}
}

static void expect_xor_a5(uint8_t *expected, const uint8_t *input, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		expected[i] = input[i] ^ 0xA5;
	}
}

ZTEST_F(sc1777y, test_import_platform_public_key_sends_803001010040)
{
	uint8_t key64[64];
	uint8_t expected_frame[72];
	uint8_t frame[72];
	size_t frame_len;

	fill_incrementing(key64, sizeof(key64), 0x10);
	expected_frame[0] = 0x55;
	expected_frame[1] = 0x80;
	expected_frame[2] = 0x30;
	expected_frame[3] = 0x01;
	expected_frame[4] = 0x01;
	expected_frame[5] = 0x00;
	expected_frame[6] = 0x40;
	memcpy(&expected_frame[7], key64, sizeof(key64));
	expected_frame[sizeof(expected_frame) - 1U] =
		expected_lrc(&expected_frame[1], sizeof(expected_frame) - 2U);

	zassert_ok(sc1777y_import_platform_public_key(fixture->dev, key64));
	zassert_ok(sc1777y_emul_get_last_command(fixture->emul, frame, sizeof(frame), &frame_len));
	zassert_mem_equal(expected_frame, frame, sizeof(expected_frame));
	zassert_equal(sizeof(expected_frame), frame_len);
}

ZTEST_F(sc1777y, test_import_ak_sends_802602000010)
{
	uint8_t ak16[16];
	uint8_t expected_frame[24];
	uint8_t frame[24];
	size_t frame_len;

	fill_incrementing(ak16, sizeof(ak16), 0x20);
	expected_frame[0] = 0x55;
	expected_frame[1] = 0x80;
	expected_frame[2] = 0x26;
	expected_frame[3] = 0x02;
	expected_frame[4] = 0x00;
	expected_frame[5] = 0x00;
	expected_frame[6] = 0x10;
	memcpy(&expected_frame[7], ak16, sizeof(ak16));
	expected_frame[sizeof(expected_frame) - 1U] =
		expected_lrc(&expected_frame[1], sizeof(expected_frame) - 2U);

	zassert_ok(sc1777y_import_ak(fixture->dev, ak16));
	zassert_ok(sc1777y_emul_get_last_command(fixture->emul, frame, sizeof(frame), &frame_len));
	zassert_mem_equal(expected_frame, frame, sizeof(expected_frame));
	zassert_equal(sizeof(expected_frame), frame_len);
}

ZTEST_F(sc1777y, test_import_iv_sends_802604000010)
{
	uint8_t iv16[16];
	uint8_t expected_frame[24];
	uint8_t frame[24];
	size_t frame_len;

	fill_incrementing(iv16, sizeof(iv16), 0x40);
	expected_frame[0] = 0x55;
	expected_frame[1] = 0x80;
	expected_frame[2] = 0x26;
	expected_frame[3] = 0x04;
	expected_frame[4] = 0x00;
	expected_frame[5] = 0x00;
	expected_frame[6] = 0x10;
	memcpy(&expected_frame[7], iv16, sizeof(iv16));
	expected_frame[sizeof(expected_frame) - 1U] =
		expected_lrc(&expected_frame[1], sizeof(expected_frame) - 2U);

	zassert_ok(sc1777y_import_iv(fixture->dev, iv16));
	zassert_ok(sc1777y_emul_get_last_command(fixture->emul, frame, sizeof(frame), &frame_len));
	zassert_mem_equal(expected_frame, frame, sizeof(expected_frame));
	zassert_equal(sizeof(expected_frame), frame_len);
}

ZTEST_F(sc1777y, test_set_platform_type_sends_803e00010000)
{
	const uint8_t expected_frame[] = {0x55, 0x80, 0x3E, 0x00, 0x01, 0x00, 0x00, 0x40};
	uint8_t frame[16];
	size_t frame_len;

	zassert_ok(sc1777y_set_platform_type(fixture->dev, SC1777Y_PLATFORM_NANRUI));
	zassert_ok(sc1777y_emul_get_last_command(fixture->emul, frame, sizeof(frame), &frame_len));
	zassert_mem_equal(expected_frame, frame, sizeof(expected_frame));
	zassert_equal(sizeof(expected_frame), frame_len);
}

ZTEST_F(sc1777y, test_get_platform_type_sends_803e01000000)
{
	const uint8_t expected_frame[] = {0x55, 0x80, 0x3E, 0x01, 0x00, 0x00, 0x00, 0x40};
	uint8_t frame[16];
	size_t frame_len;
	enum sc1777y_platform_type type = SC1777Y_PLATFORM_UNSET;

	zassert_ok(sc1777y_get_platform_type(fixture->dev, &type));
	zassert_equal(SC1777Y_PLATFORM_NANRUI, type);
	zassert_ok(sc1777y_emul_get_last_command(fixture->emul, frame, sizeof(frame), &frame_len));
	zassert_mem_equal(expected_frame, frame, sizeof(expected_frame));
	zassert_equal(sizeof(expected_frame), frame_len);
}

ZTEST_F(sc1777y, test_get_platform_type_accepts_unset_value)
{
	const uint8_t injected_type = SC1777Y_PLATFORM_UNSET;
	enum sc1777y_platform_type type = SC1777Y_PLATFORM_NANRUI;

	zassert_ok(sc1777y_emul_set_fixed_response(fixture->emul, &injected_type,
						 sizeof(injected_type)));
	zassert_ok(sc1777y_get_platform_type(fixture->dev, &type));
	zassert_equal(SC1777Y_PLATFORM_UNSET, type);
}

ZTEST_F(sc1777y, test_generate_sm2_keypair_sends_802c00000000)
{
	const uint8_t expected_frame[] = {0x55, 0x80, 0x2C, 0x00, 0x00, 0x00, 0x00, 0x53};
	uint8_t frame[16];
	size_t frame_len;

	zassert_ok(sc1777y_generate_sm2_keypair(fixture->dev));
	zassert_ok(sc1777y_emul_get_last_command(fixture->emul, frame, sizeof(frame), &frame_len));
	zassert_mem_equal(expected_frame, frame, sizeof(expected_frame));
	zassert_equal(sizeof(expected_frame), frame_len);
}

ZTEST_F(sc1777y, test_generate_cert_request_format_2_sends_803801000003_subject)
{
	const uint8_t subject[] = {'C', 'N', '='};
	const uint8_t expected_prefix[] = "SC1777Y-CERT-REQUEST:";
	uint8_t expected_frame[11];
	uint8_t out[64];
	uint8_t frame[16];
	size_t out_len;
	size_t frame_len;

	expected_frame[0] = 0x55;
	expected_frame[1] = 0x80;
	expected_frame[2] = 0x38;
	expected_frame[3] = 0x01;
	expected_frame[4] = 0x00;
	expected_frame[5] = 0x00;
	expected_frame[6] = sizeof(subject);
	memcpy(&expected_frame[7], subject, sizeof(subject));
	expected_frame[sizeof(expected_frame) - 1U] =
		expected_lrc(&expected_frame[1], sizeof(expected_frame) - 2U);

	zassert_ok(sc1777y_generate_cert_request(fixture->dev, SC1777Y_CERT_REQUEST_FORMAT_2,
						 subject, sizeof(subject), out, sizeof(out),
						 &out_len));
	zassert_true(out_len > sizeof(expected_prefix) - 1U);
	zassert_mem_equal(expected_prefix, out, sizeof(expected_prefix) - 1U);
	zassert_mem_equal(subject, &out[sizeof(expected_prefix) - 1U], sizeof(subject));
	zassert_ok(sc1777y_emul_get_last_command(fixture->emul, frame, sizeof(frame), &frame_len));
	zassert_mem_equal(expected_frame, frame, sizeof(expected_frame));
	zassert_equal(sizeof(expected_frame), frame_len);
}

ZTEST_F(sc1777y, test_session_begin_sends_803a01000000_and_returns_128_bytes)
{
	const uint8_t expected_frame[] = {0x55, 0x80, 0x3A, 0x01, 0x00, 0x00, 0x00, 0x44};
	uint8_t expected_out[SC1777Y_SESSION_RANDOM_LEN];
	uint8_t out[SC1777Y_SESSION_RANDOM_LEN];
	uint8_t frame[16];
	size_t frame_len;

	fill_incrementing(expected_out, sizeof(expected_out), 0xC0);
	zassert_ok(sc1777y_session_begin(fixture->dev, out));
	zassert_mem_equal(expected_out, out, sizeof(out));
	zassert_ok(sc1777y_emul_get_last_command(fixture->emul, frame, sizeof(frame), &frame_len));
	zassert_mem_equal(expected_frame, frame, sizeof(expected_frame));
	zassert_equal(sizeof(expected_frame), frame_len);
}

ZTEST_F(sc1777y, test_hash_response_sends_803201000003_and_returns_32_bytes)
{
	const uint8_t input[] = {0x01, 0x02, 0x03};
	uint8_t expected_frame[11];
	uint8_t expected_hash[SC1777Y_HASH_LEN];
	uint8_t hash[SC1777Y_HASH_LEN];
	uint8_t frame[16];
	size_t frame_len;

	expected_frame[0] = 0x55;
	expected_frame[1] = 0x80;
	expected_frame[2] = 0x32;
	expected_frame[3] = 0x01;
	expected_frame[4] = 0x00;
	expected_frame[5] = 0x00;
	expected_frame[6] = sizeof(input);
	memcpy(&expected_frame[7], input, sizeof(input));
	expected_frame[sizeof(expected_frame) - 1U] =
		expected_lrc(&expected_frame[1], sizeof(expected_frame) - 2U);
	fill_incrementing(expected_hash, sizeof(expected_hash), 0xD0);

	zassert_ok(sc1777y_hash(fixture->dev, SC1777Y_HASH_RESPONSE, input, sizeof(input), hash));
	zassert_mem_equal(expected_hash, hash, sizeof(hash));
	zassert_ok(sc1777y_emul_get_last_command(fixture->emul, frame, sizeof(frame), &frame_len));
	zassert_mem_equal(expected_frame, frame, sizeof(expected_frame));
	zassert_equal(sizeof(expected_frame), frame_len);
}

ZTEST_F(sc1777y, test_sign_hash_sends_803400000020_and_returns_64_bytes)
{
	uint8_t hash[SC1777Y_HASH_LEN];
	uint8_t signature[SC1777Y_SIGNATURE_LEN];
	uint8_t expected_signature[SC1777Y_SIGNATURE_LEN];
	uint8_t expected_frame[40];
	uint8_t frame[48];
	size_t frame_len;

	fill_incrementing(hash, sizeof(hash), 0x31);
	expected_frame[0] = 0x55;
	expected_frame[1] = 0x80;
	expected_frame[2] = 0x34;
	expected_frame[3] = 0x00;
	expected_frame[4] = 0x00;
	expected_frame[5] = 0x00;
	expected_frame[6] = SC1777Y_HASH_LEN;
	memcpy(&expected_frame[7], hash, sizeof(hash));
	expected_frame[sizeof(expected_frame) - 1U] =
		expected_lrc(&expected_frame[1], sizeof(expected_frame) - 2U);
	fill_incrementing(expected_signature, sizeof(expected_signature), 0xE0);

	zassert_ok(sc1777y_sign_hash(fixture->dev, hash, signature));
	zassert_mem_equal(expected_signature, signature, sizeof(signature));
	zassert_ok(sc1777y_emul_get_last_command(fixture->emul, frame, sizeof(frame), &frame_len));
	zassert_mem_equal(expected_frame, frame, sizeof(expected_frame));
	zassert_equal(sizeof(expected_frame), frame_len);
}

ZTEST_F(sc1777y, test_verify_signature_sends_803600010060)
{
	uint8_t hash[SC1777Y_HASH_LEN];
	uint8_t signature[SC1777Y_SIGNATURE_LEN];
	uint8_t expected_frame[104];
	uint8_t frame[112];
	size_t frame_len;

	fill_incrementing(hash, sizeof(hash), 0x41);
	fill_incrementing(signature, sizeof(signature), 0x61);
	expected_frame[0] = 0x55;
	expected_frame[1] = 0x80;
	expected_frame[2] = 0x36;
	expected_frame[3] = 0x00;
	expected_frame[4] = 0x01;
	expected_frame[5] = 0x00;
	expected_frame[6] = 0x60;
	memcpy(&expected_frame[7], hash, sizeof(hash));
	memcpy(&expected_frame[7 + sizeof(hash)], signature, sizeof(signature));
	expected_frame[sizeof(expected_frame) - 1U] =
		expected_lrc(&expected_frame[1], sizeof(expected_frame) - 2U);

	zassert_ok(sc1777y_verify_signature(fixture->dev, hash, signature));
	zassert_ok(sc1777y_emul_get_last_command(fixture->emul, frame, sizeof(frame), &frame_len));
	zassert_mem_equal(expected_frame, frame, sizeof(expected_frame));
	zassert_equal(sizeof(expected_frame), frame_len);
}

ZTEST_F(sc1777y, test_generate_auth_response_sends_802a01040020_and_returns_146_bytes)
{
	uint8_t factor[SC1777Y_AUTH_FACTOR_LEN];
	uint8_t response[SC1777Y_AUTH_RESPONSE_LEN];
	uint8_t expected_response[SC1777Y_AUTH_RESPONSE_LEN];
	uint8_t expected_frame[40];
	uint8_t frame[48];
	size_t frame_len;

	fill_incrementing(factor, sizeof(factor), 0x51);
	expected_frame[0] = 0x55;
	expected_frame[1] = 0x80;
	expected_frame[2] = 0x2A;
	expected_frame[3] = 0x01;
	expected_frame[4] = 0x04;
	expected_frame[5] = 0x00;
	expected_frame[6] = SC1777Y_AUTH_FACTOR_LEN;
	memcpy(&expected_frame[7], factor, sizeof(factor));
	expected_frame[sizeof(expected_frame) - 1U] =
		expected_lrc(&expected_frame[1], sizeof(expected_frame) - 2U);
	fill_incrementing(expected_response, sizeof(expected_response), 0x70);

	zassert_ok(sc1777y_generate_auth_response(fixture->dev, factor, response));
	zassert_mem_equal(expected_response, response, sizeof(response));
	zassert_ok(sc1777y_emul_get_last_command(fixture->emul, frame, sizeof(frame), &frame_len));
	zassert_mem_equal(expected_frame, frame, sizeof(expected_frame));
	zassert_equal(sizeof(expected_frame), frame_len);
}

ZTEST_F(sc1777y, test_session_confirm_sends_803c00000080_and_returns_32_bytes)
{
	uint8_t en_r2[SC1777Y_SESSION_RANDOM_LEN];
	uint8_t dkhash[SC1777Y_SESSION_DKHASH_LEN];
	uint8_t expected_dkhash[SC1777Y_SESSION_DKHASH_LEN];
	uint8_t expected_frame[136];
	uint8_t frame[144];
	size_t frame_len;

	fill_incrementing(en_r2, sizeof(en_r2), 0x71);
	expected_frame[0] = 0x55;
	expected_frame[1] = 0x80;
	expected_frame[2] = 0x3C;
	expected_frame[3] = 0x00;
	expected_frame[4] = 0x00;
	expected_frame[5] = 0x00;
	expected_frame[6] = 0x80;
	memcpy(&expected_frame[7], en_r2, sizeof(en_r2));
	expected_frame[sizeof(expected_frame) - 1U] =
		expected_lrc(&expected_frame[1], sizeof(expected_frame) - 2U);
	fill_incrementing(expected_dkhash, sizeof(expected_dkhash), 0x90);

	zassert_ok(sc1777y_session_confirm(fixture->dev, en_r2, dkhash));
	zassert_mem_equal(expected_dkhash, dkhash, sizeof(dkhash));
	zassert_ok(sc1777y_emul_get_last_command(fixture->emul, frame, sizeof(frame), &frame_len));
	zassert_mem_equal(expected_frame, frame, sizeof(expected_frame));
	zassert_equal(sizeof(expected_frame), frame_len);
}

ZTEST_F(sc1777y, test_session_encrypt_sends_802880000010_and_xors_output)
{
	const uint8_t input[SC1777Y_BLOCK16_MIN_LEN] = {
		0x01, 0x11, 0x21, 0x31, 0x41, 0x51, 0x61, 0x71,
		0x81, 0x91, 0xA1, 0xB1, 0xC1, 0xD1, 0xE1, 0xF1,
	};
	uint8_t expected_out[sizeof(input)];
	uint8_t out[sizeof(input)];
	uint8_t expected_frame[24];
	uint8_t frame[32];
	size_t out_len;
	size_t frame_len;

	expected_frame[0] = 0x55;
	expected_frame[1] = 0x80;
	expected_frame[2] = 0x28;
	expected_frame[3] = 0x80;
	expected_frame[4] = 0x00;
	expected_frame[5] = 0x00;
	expected_frame[6] = sizeof(input);
	memcpy(&expected_frame[7], input, sizeof(input));
	expected_frame[sizeof(expected_frame) - 1U] =
		expected_lrc(&expected_frame[1], sizeof(expected_frame) - 2U);
	expect_xor_a5(expected_out, input, sizeof(input));

	zassert_ok(sc1777y_session_encrypt(fixture->dev, input, sizeof(input), out, sizeof(out),
					   &out_len));
	zassert_mem_equal(expected_out, out, sizeof(out));
	zassert_equal(sizeof(out), out_len);
	zassert_ok(sc1777y_emul_get_last_command(fixture->emul, frame, sizeof(frame), &frame_len));
	zassert_mem_equal(expected_frame, frame, sizeof(expected_frame));
	zassert_equal(sizeof(expected_frame), frame_len);
}

ZTEST_F(sc1777y, test_session_decrypt_sends_802881000010_and_xors_output)
{
	const uint8_t input[SC1777Y_BLOCK16_MIN_LEN] = {
		0xF1, 0xE1, 0xD1, 0xC1, 0xB1, 0xA1, 0x91, 0x81,
		0x71, 0x61, 0x51, 0x41, 0x31, 0x21, 0x11, 0x01,
	};
	uint8_t expected_out[sizeof(input)];
	uint8_t out[sizeof(input)];
	uint8_t expected_frame[24];
	uint8_t frame[32];
	size_t out_len;
	size_t frame_len;

	expected_frame[0] = 0x55;
	expected_frame[1] = 0x80;
	expected_frame[2] = 0x28;
	expected_frame[3] = 0x81;
	expected_frame[4] = 0x00;
	expected_frame[5] = 0x00;
	expected_frame[6] = sizeof(input);
	memcpy(&expected_frame[7], input, sizeof(input));
	expected_frame[sizeof(expected_frame) - 1U] =
		expected_lrc(&expected_frame[1], sizeof(expected_frame) - 2U);
	expect_xor_a5(expected_out, input, sizeof(input));

	zassert_ok(sc1777y_session_decrypt(fixture->dev, input, sizeof(input), out, sizeof(out),
					   &out_len));
	zassert_mem_equal(expected_out, out, sizeof(out));
	zassert_equal(sizeof(out), out_len);
	zassert_ok(sc1777y_emul_get_last_command(fixture->emul, frame, sizeof(frame), &frame_len));
	zassert_mem_equal(expected_frame, frame, sizeof(expected_frame));
	zassert_equal(sizeof(expected_frame), frame_len);
}

ZTEST_F(sc1777y, test_platform_apis_reject_fixed_length_invalid_input_before_spi)
{
	uint8_t hash[SC1777Y_HASH_LEN] = {0};
	uint8_t signature[SC1777Y_SIGNATURE_LEN] = {0};
	uint8_t factor[SC1777Y_AUTH_FACTOR_LEN] = {0};
	uint8_t en_r2[SC1777Y_SESSION_RANDOM_LEN] = {0};
	uint8_t response[SC1777Y_AUTH_RESPONSE_LEN];
	uint8_t dkhash[SC1777Y_SESSION_DKHASH_LEN];

	zassert_equal(-EINVAL, sc1777y_import_platform_public_key(fixture->dev, NULL));
	zassert_equal(-EINVAL, sc1777y_import_ak(fixture->dev, NULL));
	zassert_equal(-EINVAL, sc1777y_import_iv(fixture->dev, NULL));
	zassert_equal(-EINVAL, sc1777y_session_begin(fixture->dev, NULL));
	zassert_equal(-EINVAL, sc1777y_sign_hash(fixture->dev, NULL, signature));
	zassert_equal(-EINVAL, sc1777y_sign_hash(fixture->dev, hash, NULL));
	zassert_equal(-EINVAL, sc1777y_verify_signature(fixture->dev, NULL, signature));
	zassert_equal(-EINVAL, sc1777y_verify_signature(fixture->dev, hash, NULL));
	zassert_equal(-EINVAL, sc1777y_generate_auth_response(fixture->dev, NULL, response));
	zassert_equal(-EINVAL, sc1777y_generate_auth_response(fixture->dev, factor, NULL));
	zassert_equal(-EINVAL, sc1777y_session_confirm(fixture->dev, NULL, dkhash));
	zassert_equal(-EINVAL, sc1777y_session_confirm(fixture->dev, en_r2, NULL));
	zassert_equal(0, sc1777y_emul_get_command_count(fixture->emul));
}

ZTEST_F(sc1777y, test_set_platform_type_rejects_invalid_enum_before_spi)
{
	zassert_equal(-EINVAL, sc1777y_set_platform_type(fixture->dev,
							 SC1777Y_PLATFORM_UNSET));
	zassert_equal(-EINVAL, sc1777y_set_platform_type(fixture->dev,
							 (enum sc1777y_platform_type)99));
	zassert_equal(0, sc1777y_emul_get_command_count(fixture->emul));
}

ZTEST_F(sc1777y, test_get_platform_type_rejects_null_out_before_spi)
{
	zassert_equal(-EINVAL, sc1777y_get_platform_type(fixture->dev, NULL));
	zassert_equal(0, sc1777y_emul_get_command_count(fixture->emul));
}

ZTEST_F(sc1777y, test_generate_cert_request_rejects_invalid_arguments_before_spi)
{
	uint8_t out[8];
	size_t out_len = 0U;

	zassert_equal(-EINVAL, sc1777y_generate_cert_request(fixture->dev,
							     SC1777Y_CERT_REQUEST_FORMAT_1,
							     NULL, 1U, out, sizeof(out),
							     &out_len));
	zassert_equal(-EINVAL, sc1777y_generate_cert_request(fixture->dev,
							     SC1777Y_CERT_REQUEST_FORMAT_1,
							     out, sizeof(out), NULL,
							     sizeof(out), &out_len));
	zassert_equal(-EINVAL, sc1777y_generate_cert_request(fixture->dev,
							     SC1777Y_CERT_REQUEST_FORMAT_1,
							     out, sizeof(out), out,
							     sizeof(out), NULL));
	zassert_equal(-EINVAL, sc1777y_generate_cert_request(fixture->dev,
							     (enum sc1777y_cert_request_format)2,
							     out, sizeof(out), out,
							     sizeof(out), &out_len));
	zassert_equal(0, sc1777y_emul_get_command_count(fixture->emul));
}

ZTEST_F(sc1777y, test_hash_rejects_invalid_arguments_before_spi)
{
	uint8_t hash[SC1777Y_HASH_LEN];

	zassert_equal(-EINVAL, sc1777y_hash(fixture->dev, SC1777Y_HASH_REQUEST, NULL, 1U, hash));
	zassert_equal(-EINVAL, sc1777y_hash(fixture->dev, SC1777Y_HASH_REQUEST, hash, 1U, NULL));
	zassert_equal(-EINVAL, sc1777y_hash(fixture->dev, (enum sc1777y_hash_target)2, hash, 1U,
					    hash));
	zassert_equal(0, sc1777y_emul_get_command_count(fixture->emul));
}

ZTEST_F(sc1777y, test_session_crypto_rejects_invalid_block_length_before_spi)
{
	const uint8_t short_block[15] = {0};
	const uint8_t odd_block[17] = {0};
	uint8_t out[SC1777Y_BLOCK16_MIN_LEN];
	size_t out_len = 0U;

	zassert_equal(-EINVAL, sc1777y_session_encrypt(fixture->dev, short_block,
						       sizeof(short_block), out, sizeof(out),
						       &out_len));
	zassert_equal(-EINVAL, sc1777y_session_decrypt(fixture->dev, odd_block,
						       sizeof(odd_block), out, sizeof(out),
						       &out_len));
	zassert_equal(0, sc1777y_emul_get_command_count(fixture->emul));
}

ZTEST_F(sc1777y, test_session_crypto_reports_required_length_before_spi)
{
	uint8_t input[SC1777Y_BLOCK16_MIN_LEN] = {0};
	uint8_t out[SC1777Y_BLOCK16_MIN_LEN - 1U];
	size_t out_len = 0U;

	zassert_equal(-ENOMEM, sc1777y_session_encrypt(fixture->dev, input, sizeof(input), out,
						       sizeof(out), &out_len));
	zassert_equal(sizeof(input), out_len);
	zassert_equal(0, sc1777y_emul_get_command_count(fixture->emul));
}
