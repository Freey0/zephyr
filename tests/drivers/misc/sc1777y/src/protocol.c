/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/drivers/misc/sc1777y.h>
#include <zephyr/drivers/misc/sc1777y_emul.h>
#include <zephyr/ztest.h>

#include "fixture.h"

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
