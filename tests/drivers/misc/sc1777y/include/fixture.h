/* SPDX-License-Identifier: Apache-2.0 */

#ifndef TESTS_DRIVERS_MISC_SC1777Y_INCLUDE_FIXTURE_H_
#define TESTS_DRIVERS_MISC_SC1777Y_INCLUDE_FIXTURE_H_

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>

struct sc1777y_fixture {
	const struct device *dev;
	const struct emul *emul;
};

#endif /* TESTS_DRIVERS_MISC_SC1777Y_INCLUDE_FIXTURE_H_ */
