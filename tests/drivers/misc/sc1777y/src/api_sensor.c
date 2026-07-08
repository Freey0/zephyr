/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>

#include <zephyr/drivers/misc/sc1777y.h>
#include <zephyr/drivers/misc/sc1777y_emul.h>
#include <zephyr/ztest.h>

#include "fixture.h"

static void expect_xor_5a(uint8_t *expected, const uint8_t *input, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		expected[i] = input[i] ^ 0x5A;
	}
}

ZTEST_F(sc1777y, test_encrypt_sensor_challenge_sends_0088000100080004_rand4_8000)
{
	const uint8_t rand4[4] = {0x11, 0x22, 0x33, 0x44};
	const uint8_t expected_frame[] = {0x55, 0x00, 0x88, 0x00, 0x01, 0x00, 0x08,
					  0x00, 0x04, 0x11, 0x22, 0x33, 0x44, 0x80, 0x00,
					  0xBE};
	const uint8_t expected_encrypted[8] = {0xC0, 0xC1, 0xC2, 0xC3,
					       0xC4, 0xC5, 0xC6, 0xC7};
	uint8_t encrypted8[8];
	uint8_t frame[32];
	size_t frame_len;

	zassert_ok(sc1777y_encrypt_sensor_challenge(fixture->dev, rand4, encrypted8));
	zassert_mem_equal(expected_encrypted, encrypted8, sizeof(expected_encrypted));
	zassert_ok(sc1777y_emul_get_last_command(fixture->emul, frame, sizeof(frame), &frame_len));
	zassert_mem_equal(expected_frame, frame, sizeof(expected_frame));
	zassert_equal(sizeof(expected_frame), frame_len);
}

ZTEST_F(sc1777y, test_verify_sensor_auth_new_sends_800801040010)
{
	const uint8_t id[8] = {1, 2, 3, 4, 5, 6, 7, 8};
	const uint8_t encrypted[8] = {9, 10, 11, 12, 13, 14, 15, 16};
	const uint8_t expected_rand4[4] = {0xD0, 0xD1, 0xD2, 0xD3};
	uint8_t rand4[4];
	uint8_t frame[32];
	size_t frame_len;

	zassert_ok(sc1777y_verify_sensor_auth(fixture->dev, SC1777Y_SENSOR_NEW, id, encrypted,
					      rand4));
	zassert_mem_equal(expected_rand4, rand4, sizeof(expected_rand4));
	zassert_ok(sc1777y_emul_get_last_command(fixture->emul, frame, sizeof(frame), &frame_len));
	zassert_equal(0x80, frame[1]);
	zassert_equal(0x08, frame[2]);
	zassert_equal(0x01, frame[3]);
	zassert_equal(0x04, frame[4]);
	zassert_equal(0x00, frame[5]);
	zassert_equal(0x10, frame[6]);
	zassert_mem_equal(id, &frame[7], 8);
	zassert_mem_equal(encrypted, &frame[15], 8);
	zassert_equal(24, frame_len);
}

ZTEST_F(sc1777y, test_sensor_encrypt_sends_800680010008_and_xors_output)
{
	const uint8_t input[8] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80};
	const uint8_t expected_frame[] = {0x55, 0x80, 0x06, 0x80, 0x01, 0x00, 0x08,
					  0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80,
					  0x70};
	uint8_t expected_out[8];
	uint8_t out[8];
	uint8_t frame[32];
	size_t out_len;
	size_t frame_len;

	expect_xor_5a(expected_out, input, sizeof(input));
	zassert_ok(sc1777y_sensor_encrypt(fixture->dev, input, sizeof(input), out, sizeof(out),
					  &out_len));
	zassert_mem_equal(expected_out, out, sizeof(out));
	zassert_equal(sizeof(out), out_len);
	zassert_ok(sc1777y_emul_get_last_command(fixture->emul, frame, sizeof(frame), &frame_len));
	zassert_mem_equal(expected_frame, frame, sizeof(expected_frame));
	zassert_equal(sizeof(expected_frame), frame_len);
}

ZTEST_F(sc1777y, test_sensor_decrypt_from_terminal_sends_800880010008_and_xors_output)
{
	const uint8_t input[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
	const uint8_t expected_frame[] = {0x55, 0x80, 0x08, 0x80, 0x01, 0x00, 0x08,
					  0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
					  0xFE};
	uint8_t expected_out[8];
	uint8_t out[8];
	uint8_t frame[32];
	size_t out_len;
	size_t frame_len;

	expect_xor_5a(expected_out, input, sizeof(input));
	zassert_ok(sc1777y_sensor_decrypt_from_terminal(fixture->dev, input, sizeof(input), out,
							sizeof(out), &out_len));
	zassert_mem_equal(expected_out, out, sizeof(out));
	zassert_equal(sizeof(out), out_len);
	zassert_ok(sc1777y_emul_get_last_command(fixture->emul, frame, sizeof(frame), &frame_len));
	zassert_mem_equal(expected_frame, frame, sizeof(expected_frame));
	zassert_equal(sizeof(expected_frame), frame_len);
}

ZTEST_F(sc1777y, test_terminal_decrypt_sensor_legacy_sends_p2_02)
{
	const uint8_t id[8] = {0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8};
	const uint8_t input[8] = {0x11, 0x21, 0x31, 0x41, 0x51, 0x61, 0x71, 0x81};
	uint8_t expected_out[8];
	uint8_t out[8];
	uint8_t frame[32];
	size_t out_len;
	size_t frame_len;

	expect_xor_5a(expected_out, input, sizeof(input));
	zassert_ok(sc1777y_terminal_decrypt_sensor(fixture->dev, SC1777Y_SENSOR_LEGACY, id,
						   input, sizeof(input), out, sizeof(out),
						   &out_len));
	zassert_mem_equal(expected_out, out, sizeof(out));
	zassert_equal(sizeof(out), out_len);
	zassert_ok(sc1777y_emul_get_last_command(fixture->emul, frame, sizeof(frame), &frame_len));
	zassert_equal(0x80, frame[1]);
	zassert_equal(0x08, frame[2]);
	zassert_equal(0x81, frame[3]);
	zassert_equal(0x02, frame[4]);
	zassert_equal(0x00, frame[5]);
	zassert_equal(0x10, frame[6]);
	zassert_mem_equal(id, &frame[7], 8);
	zassert_mem_equal(input, &frame[15], 8);
	zassert_equal(24, frame_len);
}

ZTEST_F(sc1777y, test_terminal_decrypt_sensor_new_sends_p2_05)
{
	const uint8_t id[8] = {0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8};
	const uint8_t input[8] = {0x12, 0x22, 0x32, 0x42, 0x52, 0x62, 0x72, 0x82};
	uint8_t out[8];
	uint8_t frame[32];
	size_t out_len;
	size_t frame_len;

	zassert_ok(sc1777y_terminal_decrypt_sensor(fixture->dev, SC1777Y_SENSOR_NEW, id, input,
						   sizeof(input), out, sizeof(out), &out_len));
	zassert_ok(sc1777y_emul_get_last_command(fixture->emul, frame, sizeof(frame), &frame_len));
	zassert_equal(0x05, frame[4]);
	zassert_equal(24, frame_len);
}

ZTEST_F(sc1777y, test_terminal_encrypt_sensor_legacy_sends_p2_02)
{
	const uint8_t id[8] = {0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8};
	const uint8_t input[8] = {0x13, 0x23, 0x33, 0x43, 0x53, 0x63, 0x73, 0x83};
	uint8_t out[8];
	uint8_t frame[32];
	size_t out_len;
	size_t frame_len;

	zassert_ok(sc1777y_terminal_encrypt_sensor(fixture->dev, SC1777Y_SENSOR_LEGACY, id, input,
						   sizeof(input), out, sizeof(out), &out_len));
	zassert_ok(sc1777y_emul_get_last_command(fixture->emul, frame, sizeof(frame), &frame_len));
	zassert_equal(0x80, frame[1]);
	zassert_equal(0x06, frame[2]);
	zassert_equal(0x81, frame[3]);
	zassert_equal(0x02, frame[4]);
	zassert_equal(24, frame_len);
}

ZTEST_F(sc1777y, test_terminal_encrypt_sensor_new_sends_p2_05_and_xors_output)
{
	const uint8_t id[8] = {0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8};
	const uint8_t input[8] = {0x14, 0x24, 0x34, 0x44, 0x54, 0x64, 0x74, 0x84};
	uint8_t expected_out[8];
	uint8_t out[8];
	uint8_t frame[32];
	size_t out_len;
	size_t frame_len;

	expect_xor_5a(expected_out, input, sizeof(input));
	zassert_ok(sc1777y_terminal_encrypt_sensor(fixture->dev, SC1777Y_SENSOR_NEW, id, input,
						   sizeof(input), out, sizeof(out), &out_len));
	zassert_mem_equal(expected_out, out, sizeof(out));
	zassert_equal(sizeof(out), out_len);
	zassert_ok(sc1777y_emul_get_last_command(fixture->emul, frame, sizeof(frame), &frame_len));
	zassert_equal(0x05, frame[4]);
	zassert_mem_equal(id, &frame[7], 8);
	zassert_mem_equal(input, &frame[15], 8);
	zassert_equal(24, frame_len);
}

ZTEST_F(sc1777y, test_sensor_encrypt_rejects_invalid_block_size_before_spi)
{
	const uint8_t input[7] = {0};
	uint8_t out[8];
	size_t out_len;

	zassert_equal(-EINVAL, sc1777y_sensor_encrypt(fixture->dev, input, sizeof(input), out,
						      sizeof(out), &out_len));
	zassert_equal(0, sc1777y_emul_get_command_count(fixture->emul));
}
