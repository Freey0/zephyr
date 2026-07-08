/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>

#include <zephyr/drivers/misc/sc1777y.h>
#include <zephyr/ztest.h>

#include "fixture.h"

ZTEST_F(sc1777y, test_command_rejects_small_output_buffer)
{
	const struct sc1777y_command cmd = {.cla = 0x00, .ins = 0x84, .p1 = 0x00, .p2 = 0x04};
	uint8_t out[3];
	size_t out_len = 0U;

	zassert_equal(-ENOMEM,
		      sc1777y_command(fixture->dev, &cmd, out, sizeof(out), &out_len, NULL));
}
