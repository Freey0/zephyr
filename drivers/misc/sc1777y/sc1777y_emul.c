/* SPDX-License-Identifier: Apache-2.0 */

#define DT_DRV_COMPAT senscomm_sc1777y

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/misc/sc1777y_emul.h>
#include <zephyr/drivers/spi_emul.h>

struct sc1777y_emul_data {
	uint32_t command_count;
};

void sc1777y_emul_reset(const struct emul *target)
{
	struct sc1777y_emul_data *data = target->data;

	memset(data, 0, sizeof(*data));
}

uint32_t sc1777y_emul_get_command_count(const struct emul *target)
{
	struct sc1777y_emul_data *data = target->data;

	return data->command_count;
}

static int sc1777y_emul_io(const struct emul *target, const struct spi_config *config,
			   const struct spi_buf_set *tx_bufs, const struct spi_buf_set *rx_bufs)
{
	ARG_UNUSED(target);
	ARG_UNUSED(config);
	ARG_UNUSED(tx_bufs);
	ARG_UNUSED(rx_bufs);

	return 0;
}

static const struct spi_emul_api sc1777y_emul_api = {
	.io = sc1777y_emul_io,
};

static int sc1777y_emul_init(const struct emul *target, const struct device *parent)
{
	ARG_UNUSED(parent);

	sc1777y_emul_reset(target);

	return 0;
}

#define SC1777Y_EMUL_DEFINE(inst)                                                                \
	static struct sc1777y_emul_data sc1777y_emul_data_##inst;                              \
	EMUL_DT_INST_DEFINE(inst, sc1777y_emul_init, &sc1777y_emul_data_##inst, NULL,          \
			    &sc1777y_emul_api, NULL)

DT_INST_FOREACH_STATUS_OKAY(SC1777Y_EMUL_DEFINE)
