# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources(secure_channel.c protocol.c socket_io.c handshake.c record.c)
zephyr_library_sources_ifdef(CONFIG_SC1777Y_SECURE_CHANNEL_MQTT mqtt_transport.c)
