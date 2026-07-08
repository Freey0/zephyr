# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources_ifdef(CONFIG_SC1777Y sc1777y.c)
zephyr_library_sources_ifdef(CONFIG_SC1777Y_EMUL sc1777y_emul.c)
