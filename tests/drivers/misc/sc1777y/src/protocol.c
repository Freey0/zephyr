/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/drivers/misc/sc1777y_emul.h>
#include <zephyr/ztest.h>

#include "fixture.h"

ZTEST_F(sc1777y, test_emulator_starts_empty)
{
	zassert_equal(0, sc1777y_emul_get_command_count(fixture->emul));
}
