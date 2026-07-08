/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/misc/sc1777y.h>
#include <zephyr/drivers/misc/sc1777y_emul.h>
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
	uint8_t frame[16];
	size_t frame_len;
	const uint8_t expected[] = {0x55, 0x12, 0x34, 0x56, 0x78, 0x00, 0x02, 0xAA, 0x55, 0x0A};

	zassert_ok(sc1777y_command(fixture->dev, &cmd, out, sizeof(out), &out_len, NULL));
	zassert_ok(sc1777y_emul_get_last_command(fixture->emul, frame, sizeof(frame), &frame_len));
	zassert_mem_equal(expected, frame, sizeof(expected));
	zassert_equal(sizeof(expected), frame_len);
}

ZTEST_F(sc1777y, test_raw_command_accepts_max_payload_and_records_boundary_frame)
{
	static uint8_t payload[TEST_MAX_RAW_PAYLOAD_LEN];
	uint8_t out[8];
	size_t out_len;
	uint16_t status;
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

	zassert_ok(sc1777y_command(fixture->dev, &cmd, out, sizeof(out), &out_len, &status));
	zassert_equal(0x9000, status);
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

ZTEST_F(sc1777y, test_emulator_rejects_invalid_single_transfer_frame)
{
	const struct device *bus = DEVICE_DT_GET(DT_BUS(DT_ALIAS(sc1777y_0)));
	uint8_t frame[] = {0x55, 0x01, 0x02, 0x03, 0x04, 0x00, 0x02, 0xAA, 0x55, 0x00};
	uint8_t response[sizeof(frame)];
	size_t frame_len;
	struct spi_buf tx_buf = {
		.buf = frame,
		.len = sizeof(frame),
	};
	struct spi_buf rx_buf = {
		.buf = response,
		.len = sizeof(response),
	};
	const struct spi_buf_set tx_bufs = {
		.buffers = &tx_buf,
		.count = 1U,
	};
	const struct spi_buf_set rx_bufs = {
		.buffers = &rx_buf,
		.count = 1U,
	};
	const struct spi_config config = {
		.frequency = DT_PROP(DT_ALIAS(sc1777y_0), spi_max_frequency),
		.operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_MODE_CPOL | SPI_MODE_CPHA |
			     DT_PROP(DT_ALIAS(sc1777y_0), duplex) |
			     DT_PROP(DT_ALIAS(sc1777y_0), frame_format),
		.slave = DT_REG_ADDR(DT_ALIAS(sc1777y_0)),
		.word_delay = DT_PROP(DT_ALIAS(sc1777y_0), spi_interframe_delay_ns),
	};

	memset(response, 0, sizeof(response));

	zassert_ok(spi_transceive(bus, &config, &tx_bufs, &rx_bufs));
	zassert_ok(sc1777y_emul_get_last_command(fixture->emul, frame, sizeof(frame), &frame_len));
	zassert_equal(sizeof(frame), frame_len);
	zassert_equal(0x6A, response[0]);
	zassert_equal(0x90, response[1]);
	zassert_equal(0x00, response[2]);
	zassert_equal(0x00, response[3]);
	zassert_equal(0x95, response[4]);
}
