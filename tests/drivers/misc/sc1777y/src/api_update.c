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

ZTEST_F(sc1777y, test_update_identity_sends_001000000000)
{
	struct sc1777y_identity identity;
	uint8_t frame[16];
	size_t frame_len;
	const uint8_t expected_frame[] = {0x55, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0xEF};
	const uint8_t expected_serial[] = {0x53, 0x43, 0x17, 0x77, 0x00, 0x00, 0x00, 0x01};
	const uint8_t expected_key_version[] = {0x01, 0x02, 0x03, 0x00};

	zassert_ok(sc1777y_get_update_identity(fixture->dev, &identity));
	zassert_mem_equal(expected_serial, identity.serial, sizeof(identity.serial));
	zassert_mem_equal(expected_key_version, identity.key_version,
			  sizeof(identity.key_version));
	zassert_ok(sc1777y_emul_get_last_command(fixture->emul, frame, sizeof(frame), &frame_len));
	zassert_mem_equal(expected_frame, frame, sizeof(expected_frame));
	zassert_equal(sizeof(expected_frame), frame_len);
}

ZTEST_F(sc1777y, test_get_random8_sends_008400080000)
{
	uint8_t rand8[8];
	uint8_t frame[16];
	size_t frame_len;
	const uint8_t expected_frame[] = {0x55, 0x00, 0x84, 0x00, 0x08, 0x00, 0x00, 0x73};
	const uint8_t expected_rand[] = {0xA0, 0xA1, 0xA2, 0xA3,
					 0xA4, 0xA5, 0xA6, 0xA7};

	zassert_ok(sc1777y_get_random8(fixture->dev, rand8));
	zassert_mem_equal(expected_rand, rand8, sizeof(expected_rand));
	zassert_ok(sc1777y_emul_get_last_command(fixture->emul, frame, sizeof(frame), &frame_len));
	zassert_mem_equal(expected_frame, frame, sizeof(expected_frame));
	zassert_equal(sizeof(expected_frame), frame_len);
}

ZTEST_F(sc1777y, test_get_random8_rejects_null_output)
{
	zassert_equal(-EINVAL, sc1777y_get_random8(fixture->dev, NULL));
	zassert_equal(0, sc1777y_emul_get_command_count(fixture->emul));
}

ZTEST_F(sc1777y, test_verify_update_auth_sends_008200020008)
{
	const uint8_t encrypted8[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
	const uint8_t expected_frame[] = {0x55, 0x00, 0x82, 0x00, 0x02, 0x00, 0x08,
					  0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
					  0x88, 0xFF};
	uint8_t frame[32];
	size_t frame_len;

	zassert_ok(sc1777y_verify_update_auth(fixture->dev, encrypted8));
	zassert_ok(sc1777y_emul_get_last_command(fixture->emul, frame, sizeof(frame), &frame_len));
	zassert_mem_equal(expected_frame, frame, sizeof(expected_frame));
	zassert_equal(sizeof(expected_frame), frame_len);
}

ZTEST_F(sc1777y, test_verify_update_auth_rejects_invalid_input_before_spi)
{
	zassert_equal(-EINVAL, sc1777y_verify_update_auth(fixture->dev, NULL));
	zassert_equal(0, sc1777y_emul_get_command_count(fixture->emul));
}

ZTEST_F(sc1777y, test_apply_key_update_sends_802202010004)
{
	const uint8_t key_data[] = {0x10, 0x20, 0x30, 0x40};
	const uint8_t expected_frame[] = {0x55, 0x80, 0x22, 0x02, 0x01, 0x00,
					  0x04, 0x10, 0x20, 0x30, 0x40, 0x1A};
	uint8_t frame[16];
	size_t frame_len;

	zassert_ok(sc1777y_apply_key_update(fixture->dev, key_data, sizeof(key_data)));
	zassert_ok(sc1777y_emul_get_last_command(fixture->emul, frame, sizeof(frame), &frame_len));
	zassert_mem_equal(expected_frame, frame, sizeof(expected_frame));
	zassert_equal(sizeof(expected_frame), frame_len);
}

ZTEST_F(sc1777y, test_apply_key_update_accepts_max_payload)
{
	uint8_t key_data[SC1777Y_MAX_DATA_LEN];
	uint8_t expected_frame[SC1777Y_MAX_FRAME_LEN];
	uint8_t frame[SC1777Y_MAX_FRAME_LEN];
	size_t frame_len;

	for (size_t i = 0; i < sizeof(key_data); i++) {
		key_data[i] = (uint8_t)i;
	}

	expected_frame[0] = 0x55;
	expected_frame[1] = 0x80;
	expected_frame[2] = 0x22;
	expected_frame[3] = 0x02;
	expected_frame[4] = 0x01;
	expected_frame[5] = 0x08;
	expected_frame[6] = 0x00;
	memcpy(&expected_frame[7], key_data, sizeof(key_data));
	expected_frame[sizeof(expected_frame) - 1U] =
		expected_lrc(&expected_frame[1], sizeof(expected_frame) - 2U);

	zassert_ok(sc1777y_apply_key_update(fixture->dev, key_data, sizeof(key_data)));
	zassert_ok(sc1777y_emul_get_last_command(fixture->emul, frame, sizeof(frame), &frame_len));
	zassert_mem_equal(expected_frame, frame, sizeof(expected_frame));
	zassert_equal(sizeof(expected_frame), frame_len);
}

ZTEST_F(sc1777y, test_apply_key_update_rejects_invalid_input_before_spi)
{
	uint8_t key_data[SC1777Y_MAX_DATA_LEN + 1U] = {0};

	zassert_equal(-EINVAL, sc1777y_apply_key_update(fixture->dev, NULL, 1U));
	zassert_equal(-EINVAL, sc1777y_apply_key_update(fixture->dev, key_data, 0U));
	zassert_equal(-EINVAL, sc1777y_apply_key_update(fixture->dev, key_data,
							sizeof(key_data)));
	zassert_equal(0, sc1777y_emul_get_command_count(fixture->emul));
}
