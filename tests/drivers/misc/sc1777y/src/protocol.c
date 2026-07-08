/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/drivers/misc/sc1777y.h>
#include <zephyr/drivers/misc/sc1777y_emul.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/ztest.h>

#include "fixture.h"

static uint8_t test_lrc(const uint8_t *buf, size_t len)
{
	uint8_t x = 0U;

	for (size_t i = 0; i < len; i++) {
		x ^= buf[i];
	}

	return (uint8_t)~x;
}

#define TEST_MAX_RAW_PAYLOAD_LEN 2048U

ZTEST_F(sc1777y, test_emulator_starts_empty)
{
	zassert_equal(0, sc1777y_emul_get_command_count(fixture->emul));
}

ZTEST_F(sc1777y, test_raw_command_records_full_sc1777y_frame)
{
	const uint8_t payload[] = {0xAA, 0x55};
	const struct sc1777y_command cmd = {
		.cla = 0x12,
		.ins = 0x34,
		.p1 = 0x56,
		.p2 = 0x78,
		.data = payload,
		.data_len = sizeof(payload),
	};
	uint8_t out[8];
	size_t out_len;
	struct sc1777y_status status;
	uint8_t frame[16];
	size_t frame_len;
	const uint8_t expected[] = {0x55, 0x12, 0x34, 0x56, 0x78, 0x00, 0x02, 0xAA, 0x55, 0x0A};

	zassert_equal(-ENOTSUP,
		      sc1777y_command(fixture->dev, &cmd, out, sizeof(out), &out_len, &status));
	zassert_equal(0x6D, status.sw1);
	zassert_equal(0x00, status.sw2);
	zassert_ok(sc1777y_emul_get_last_command(fixture->emul, frame, sizeof(frame), &frame_len));
	zassert_mem_equal(expected, frame, sizeof(expected));
	zassert_equal(sizeof(expected), frame_len);
}

ZTEST_F(sc1777y, test_raw_command_accepts_max_payload_and_records_boundary_frame)
{
	static uint8_t payload[TEST_MAX_RAW_PAYLOAD_LEN];
	uint8_t out[8];
	size_t out_len;
	struct sc1777y_status status;
	static uint8_t frame[TEST_MAX_RAW_PAYLOAD_LEN + 8U];
	size_t frame_len;
	struct sc1777y_command cmd = {
		.cla = 0x80,
		.ins = 0xCA,
		.p1 = 0x00,
		.p2 = 0x01,
		.data = payload,
		.data_len = sizeof(payload),
	};

	for (size_t i = 0; i < sizeof(payload); i++) {
		payload[i] = (uint8_t)i;
	}

	zassert_equal(-ENOTSUP,
		      sc1777y_command(fixture->dev, &cmd, out, sizeof(out), &out_len, &status));
	zassert_equal(0x6D, status.sw1);
	zassert_equal(0x00, status.sw2);
	zassert_ok(sc1777y_emul_get_last_command(fixture->emul, frame, sizeof(frame), &frame_len));
	zassert_equal(TEST_MAX_RAW_PAYLOAD_LEN + 8U, frame_len);
	zassert_equal(0x55, frame[0]);
	zassert_equal(0x80, frame[1]);
	zassert_equal(0xCA, frame[2]);
	zassert_equal(0x08, frame[5]);
	zassert_equal(0x00, frame[6]);
	zassert_equal(payload[0], frame[7]);
	zassert_equal(payload[sizeof(payload) - 1], frame[frame_len - 2]);
	zassert_equal(test_lrc(&frame[1], frame_len - 2), frame[frame_len - 1]);
}

ZTEST_F(sc1777y, test_command_polls_until_ready_byte)
{
	const struct sc1777y_command cmd = {.cla = 0x00, .ins = 0x84, .p1 = 0x00, .p2 = 0x04};
	uint8_t out[4];
	size_t out_len;

	sc1777y_emul_set_ready_delay(fixture->emul, 3);
	zassert_ok(sc1777y_command(fixture->dev, &cmd, out, sizeof(out), &out_len, NULL));
	zassert_equal(4, out_len);
	zassert_equal(1, sc1777y_emul_get_command_count(fixture->emul));
}

ZTEST_F(sc1777y, test_emulator_records_response_without_extra_ready_header_byte)
{
	const struct sc1777y_command cmd = {.cla = 0x00, .ins = 0x84, .p1 = 0x00, .p2 = 0x04};
	uint8_t out[4];
	size_t out_len;
	uint8_t response[9];
	size_t response_len;
	const uint8_t expected_payload[] = {0xA0, 0xA1, 0xA2, 0xA3};

	zassert_ok(sc1777y_command(fixture->dev, &cmd, out, sizeof(out), &out_len, NULL));
	zassert_ok(sc1777y_emul_get_last_response(fixture->emul, response, sizeof(response),
						 &response_len));
	zassert_equal(sizeof(response), response_len);
	zassert_equal(0x90, response[0]);
	zassert_equal(0x00, response[1]);
	zassert_equal(0x00, response[2]);
	zassert_equal(0x04, response[3]);
	zassert_mem_equal(expected_payload, &response[4], sizeof(expected_payload));
	zassert_equal(test_lrc(response, response_len - 1U), response[response_len - 1U]);
}

ZTEST_F(sc1777y, test_command_retries_after_response_lrc_error)
{
	const struct sc1777y_command cmd = {.cla = 0x00, .ins = 0x84, .p1 = 0x00, .p2 = 0x04};
	uint8_t out[4];
	size_t out_len;

	sc1777y_emul_corrupt_next_response_lrc(fixture->emul);
	zassert_ok(sc1777y_command(fixture->dev, &cmd, out, sizeof(out), &out_len, NULL));
	zassert_equal(2, sc1777y_emul_get_command_count(fixture->emul));
}

ZTEST_F(sc1777y, test_command_retries_after_6a90_status)
{
	const struct sc1777y_command cmd = {.cla = 0x00, .ins = 0x84, .p1 = 0x00, .p2 = 0x04};
	uint8_t out[4];
	size_t out_len;
	struct sc1777y_status status;

	sc1777y_emul_set_next_status(fixture->emul, 0x6A, 0x90);
	zassert_ok(sc1777y_command(fixture->dev, &cmd, out, sizeof(out), &out_len, &status));
	zassert_equal(2, sc1777y_emul_get_command_count(fixture->emul));
	zassert_equal(0x90, status.sw1);
	zassert_equal(0x00, status.sw2);
}

ZTEST_F(sc1777y, test_raw_command_reports_required_length_on_short_buffer)
{
	const struct sc1777y_command cmd = {.cla = 0x00, .ins = 0x84, .p1 = 0x00, .p2 = 0x04};
	const uint8_t fixed_response[] = {0x11, 0x22, 0x33, 0x44};
	uint8_t out[3];
	size_t out_len = 0U;

	zassert_ok(sc1777y_emul_set_fixed_response(fixture->emul, fixed_response,
						 sizeof(fixed_response)));
	zassert_equal(-ENOMEM,
		      sc1777y_command(fixture->dev, &cmd, out, sizeof(out), &out_len, NULL));
	zassert_equal(sizeof(fixed_response), out_len);
}

ZTEST_F(sc1777y, test_semantic_api_uses_mode_3_msb_and_8_bit_spi)
{
	uint8_t rand4[4];
	spi_operation_t operation = 0U;

	zassert_ok(sc1777y_get_random(fixture->dev, rand4, sizeof(rand4)));
	zassert_ok(sc1777y_emul_get_last_operation(fixture->emul, &operation));
	zassert_true((operation & SPI_MODE_CPOL) != 0U);
	zassert_true((operation & SPI_MODE_CPHA) != 0U);
	zassert_equal(8U, SPI_WORD_SIZE_GET(operation));
	zassert_equal(SPI_TRANSFER_MSB, operation & SPI_TRANSFER_LSB);
}

ZTEST_F(sc1777y, test_command_returns_access_error_for_auth_failure)
{
	const struct sc1777y_command cmd = {.cla = 0x80, .ins = 0x08, .p1 = 0x01, .p2 = 0x04};
	uint8_t out[4];
	size_t out_len;
	struct sc1777y_status status;

	sc1777y_emul_set_next_status(fixture->emul, 0x63, 0x00);
	zassert_equal(-EACCES,
		      sc1777y_command(fixture->dev, &cmd, out, sizeof(out), &out_len, &status));
	zassert_equal(0x63, status.sw1);
	zassert_equal(0x00, status.sw2);
}

ZTEST_F(sc1777y, test_unsupported_byte_valid_command_returns_not_supported_status)
{
	const struct sc1777y_command cmd = {
		.cla = 0x12,
		.ins = 0x34,
		.p1 = 0x56,
		.p2 = 0x78,
	};
	uint8_t out[4];
	size_t out_len;
	struct sc1777y_status status;

	zassert_equal(-ENOTSUP,
		      sc1777y_command(fixture->dev, &cmd, out, sizeof(out), &out_len, &status));
	zassert_equal(0x6D, status.sw1);
	zassert_equal(0x00, status.sw2);
	zassert_equal(1, sc1777y_emul_get_command_count(fixture->emul));
}
