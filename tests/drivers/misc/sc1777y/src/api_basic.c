/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/drivers/misc/sc1777y.h>
#include <zephyr/drivers/misc/sc1777y_emul.h>
#include <zephyr/ztest.h>

#include "fixture.h"

ZTEST_F(sc1777y, test_get_random_sends_008400040000)
{
	uint8_t rand4[4];
	uint8_t frame[16];
	size_t frame_len;
	const uint8_t expected_frame[] = {0x55, 0x00, 0x84, 0x00, 0x04, 0x00, 0x00, 0x7F};
	const uint8_t expected_rand[] = {0xA0, 0xA1, 0xA2, 0xA3};

	zassert_ok(sc1777y_get_random(fixture->dev, rand4, sizeof(rand4)));
	zassert_mem_equal(expected_rand, rand4, sizeof(expected_rand));
	zassert_ok(sc1777y_emul_get_last_command(fixture->emul, frame, sizeof(frame), &frame_len));
	zassert_mem_equal(expected_frame, frame, sizeof(expected_frame));
	zassert_equal(sizeof(expected_frame), frame_len);
}

ZTEST_F(sc1777y, test_get_random_rejects_invalid_lengths)
{
	uint8_t rand4[4];

	zassert_equal(-EINVAL, sc1777y_get_random(fixture->dev, NULL, sizeof(rand4)));
	zassert_equal(-EINVAL, sc1777y_get_random(fixture->dev, rand4, 0));
}

ZTEST_F(sc1777y, test_get_sensor_identity_sends_003600000000)
{
	struct sc1777y_identity identity;
	uint8_t frame[16];
	size_t frame_len;
	const uint8_t expected_frame[] = {0x55, 0x00, 0x36, 0x00, 0x00, 0x00, 0x00, 0xC9};
	const uint8_t expected_serial[] = {0x53, 0x43, 0x17, 0x77, 0x00, 0x00, 0x00, 0x01};
	const uint8_t expected_key_version[] = {0x01, 0x02, 0x03, 0x00};

	zassert_ok(sc1777y_get_sensor_identity(fixture->dev, &identity));
	zassert_mem_equal(expected_serial, identity.serial, sizeof(identity.serial));
	zassert_mem_equal(expected_key_version, identity.key_version,
			  sizeof(identity.key_version));
	zassert_ok(sc1777y_emul_get_last_command(fixture->emul, frame, sizeof(frame), &frame_len));
	zassert_mem_equal(expected_frame, frame, sizeof(expected_frame));
	zassert_equal(sizeof(expected_frame), frame_len);
}

ZTEST_F(sc1777y, test_get_sensor_identity_rejects_null_output)
{
	zassert_equal(-EINVAL, sc1777y_get_sensor_identity(fixture->dev, NULL));
	zassert_equal(0, sc1777y_emul_get_command_count(fixture->emul));
}

ZTEST_F(sc1777y, test_get_update_identity_sends_001000000000)
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

ZTEST_F(sc1777y, test_get_update_identity_rejects_null_output)
{
	zassert_equal(-EINVAL, sc1777y_get_update_identity(fixture->dev, NULL));
	zassert_equal(0, sc1777y_emul_get_command_count(fixture->emul));
}

ZTEST_F(sc1777y, test_get_version_info_sends_005b00400000)
{
	struct sc1777y_version_info version;
	uint8_t frame[16];
	size_t frame_len;
	uint8_t expected_version[sizeof(version.bytes)];
	const uint8_t expected_frame[] = {0x55, 0x00, 0x5B, 0x00, 0x40, 0x00, 0x00, 0xE4};

	for (size_t i = 0; i < sizeof(expected_version); i++) {
		expected_version[i] = 0x30 + i;
	}

	zassert_ok(sc1777y_get_version_info(fixture->dev, &version));
	zassert_mem_equal(expected_version, version.bytes, sizeof(version.bytes));
	zassert_ok(sc1777y_emul_get_last_command(fixture->emul, frame, sizeof(frame), &frame_len));
	zassert_mem_equal(expected_frame, frame, sizeof(expected_frame));
	zassert_equal(sizeof(expected_frame), frame_len);
}

ZTEST_F(sc1777y, test_get_version_info_rejects_null_output)
{
	zassert_equal(-EINVAL, sc1777y_get_version_info(fixture->dev, NULL));
	zassert_equal(0, sc1777y_emul_get_command_count(fixture->emul));
}

ZTEST_F(sc1777y, test_get_serial_sends_00b0990000020008)
{
	uint8_t serial[SC1777Y_SERIAL_LEN];
	uint8_t frame[16];
	size_t frame_len;
	const uint8_t expected_frame[] = {0x55, 0x00, 0xB0, 0x99, 0x00,
					  0x00, 0x02, 0x00, 0x08, 0xDC};
	const uint8_t expected_serial[] = {0x53, 0x43, 0x17, 0x77, 0x00, 0x00, 0x00, 0x01};

	zassert_ok(sc1777y_get_serial(fixture->dev, serial));
	zassert_mem_equal(expected_serial, serial, sizeof(expected_serial));
	zassert_ok(sc1777y_emul_get_last_command(fixture->emul, frame, sizeof(frame), &frame_len));
	zassert_mem_equal(expected_frame, frame, sizeof(expected_frame));
	zassert_equal(sizeof(expected_frame), frame_len);
}

ZTEST_F(sc1777y, test_get_serial_rejects_null_output)
{
	zassert_equal(-EINVAL, sc1777y_get_serial(fixture->dev, NULL));
	zassert_equal(0, sc1777y_emul_get_command_count(fixture->emul));
}
