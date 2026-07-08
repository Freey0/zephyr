/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>

#include <zephyr/drivers/misc/sc1777y.h>
#include <zephyr/drivers/misc/sc1777y_emul.h>
#include <zephyr/ztest.h>

#include "fixture.h"

ZTEST_F(sc1777y, test_null_device_returns_einval)
{
	uint8_t out[4];

	zassert_equal(-EINVAL, sc1777y_get_random(NULL, out, sizeof(out)));
}

ZTEST_F(sc1777y, test_required_output_pointer_rejected_before_spi)
{
	zassert_equal(-EINVAL, sc1777y_get_sensor_identity(fixture->dev, NULL));
	zassert_equal(0, sc1777y_emul_get_command_count(fixture->emul));
}

ZTEST_F(sc1777y, test_output_buffer_too_small_reports_enomem_before_spi)
{
	uint8_t input[SC1777Y_BLOCK16_MIN_LEN] = {0};
	uint8_t out[SC1777Y_BLOCK16_MIN_LEN - 1U];
	size_t out_len = 0U;

	zassert_equal(-ENOMEM, sc1777y_session_encrypt(fixture->dev, input, sizeof(input), out,
						       sizeof(out), &out_len));
	zassert_equal(sizeof(input), out_len);
	zassert_equal(0, sc1777y_emul_get_command_count(fixture->emul));
}

ZTEST_F(sc1777y, test_invalid_sensor_type_rejected_before_spi)
{
	uint8_t sensor_id[8] = {0};
	uint8_t encrypted[8] = {0};
	uint8_t rand4[4];

	zassert_equal(-EINVAL, sc1777y_verify_sensor_auth(fixture->dev,
							  (enum sc1777y_sensor_type)99,
							  sensor_id, encrypted, rand4));
	zassert_equal(0, sc1777y_emul_get_command_count(fixture->emul));
}

ZTEST_F(sc1777y, test_invalid_platform_type_rejected_before_spi)
{
	zassert_equal(-EINVAL, sc1777y_set_platform_type(fixture->dev,
							 (enum sc1777y_platform_type)99));
	zassert_equal(0, sc1777y_emul_get_command_count(fixture->emul));
}

ZTEST_F(sc1777y, test_sensor_block_length_rejected_before_spi)
{
	uint8_t input[7] = {0};
	uint8_t out[8];
	size_t out_len = 0U;

	zassert_equal(-EINVAL, sc1777y_sensor_encrypt(fixture->dev, input, sizeof(input), out,
						      sizeof(out), &out_len));
	zassert_equal(0, sc1777y_emul_get_command_count(fixture->emul));
}

ZTEST_F(sc1777y, test_session_block_length_rejected_before_spi)
{
	uint8_t input[SC1777Y_BLOCK16_MIN_LEN - 1U] = {0};
	uint8_t out[SC1777Y_BLOCK16_MIN_LEN];
	size_t out_len = 0U;

	zassert_equal(-EINVAL, sc1777y_session_encrypt(fixture->dev, input, sizeof(input), out,
						       sizeof(out), &out_len));
	zassert_equal(0, sc1777y_emul_get_command_count(fixture->emul));
}

ZTEST_F(sc1777y, test_ready_timeout_returns_etimedout)
{
	uint8_t out[4];

	sc1777y_emul_set_ready_delay(fixture->emul, 100000U);
	zassert_equal(-ETIMEDOUT, sc1777y_get_random(fixture->dev, out, sizeof(out)));
	zassert_equal(1, sc1777y_emul_get_command_count(fixture->emul));
}

ZTEST_F(sc1777y, test_6d00_maps_to_enotsup)
{
	const struct sc1777y_command cmd = {.cla = 0x00, .ins = 0x84, .p1 = 0x00, .p2 = 0x04};
	uint8_t out[4];
	size_t out_len = 0U;
	struct sc1777y_status status;

	sc1777y_emul_set_next_status(fixture->emul, 0x6D, 0x00);
	zassert_equal(-ENOTSUP,
		      sc1777y_command(fixture->dev, &cmd, out, sizeof(out), &out_len, &status));
	zassert_equal(0x6D, status.sw1);
	zassert_equal(0x00, status.sw2);
	zassert_equal(1, sc1777y_emul_get_command_count(fixture->emul));
}

ZTEST_F(sc1777y, test_repeated_6a90_returns_eio_after_three_attempts)
{
	const struct sc1777y_command cmd = {.cla = 0x00, .ins = 0x84, .p1 = 0x00, .p2 = 0x04};
	uint8_t out[4];
	size_t out_len = 0U;
	struct sc1777y_status status;

	sc1777y_emul_set_status_repeat(fixture->emul, 0x6A, 0x90, 3U);
	zassert_equal(-EIO,
		      sc1777y_command(fixture->dev, &cmd, out, sizeof(out), &out_len, &status));
	zassert_equal(0x6A, status.sw1);
	zassert_equal(0x90, status.sw2);
	zassert_equal(3, sc1777y_emul_get_command_count(fixture->emul));
}
