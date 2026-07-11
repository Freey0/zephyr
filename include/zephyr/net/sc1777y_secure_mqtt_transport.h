/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_NET_SC1777Y_SECURE_MQTT_TRANSPORT_H_
#define ZEPHYR_INCLUDE_NET_SC1777Y_SECURE_MQTT_TRANSPORT_H_

#ifdef __cplusplus
extern "C" {
#endif

struct mqtt_client;
struct sc1777y_secure_channel;

int sc1777y_secure_mqtt_transport_bind(
	struct mqtt_client *client,
	struct sc1777y_secure_channel *channel);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_NET_SC1777Y_SECURE_MQTT_TRANSPORT_H_ */
