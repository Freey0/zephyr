/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/misc/sc1777y_emul.h>
#include <zephyr/ztest.h>

#include "fixture.h"

static void *sc1777y_setup(void)
{
	static struct sc1777y_fixture fixture = {
		.dev = DEVICE_DT_GET(DT_ALIAS(sc1777y_0)),
		.emul = EMUL_DT_GET(DT_ALIAS(sc1777y_0)),
	};

	return &fixture;
}

static void sc1777y_before(void *f)
{
	struct sc1777y_fixture *fixture = f;

	zassert_true(device_is_ready(fixture->dev));
	sc1777y_emul_reset(fixture->emul);
}

ZTEST_SUITE(sc1777y, NULL, sc1777y_setup, sc1777y_before, NULL, NULL);
