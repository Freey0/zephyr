/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/net/mqtt.h>
#include <zephyr/net/sc1777y_secure_channel.h>
#include <zephyr/net/sc1777y_secure_mqtt_transport.h>
#include <zephyr/ztest.h>

#include "mqtt_transport.h"

#define SC1777Y_NODE DT_ALIAS(sc1777y_0)

static const uint8_t certificate[] = {0x01};
static const uint8_t platform_public_key[SC1777Y_PLATFORM_PUBLIC_KEY_LEN];
static struct sc1777y_secure_channel channel;
static struct mqtt_client client;

static void init_channel(void)
{
	struct sockaddr_in gateway = {
		.sin_family = AF_INET,
		.sin_port = htons(18883),
		.sin_addr.s_addr = htonl(0x7f000001U),
	};
	struct sc1777y_secure_channel_config config = {
		.sc1777y = DEVICE_DT_GET(SC1777Y_NODE),
		.gateway = (const struct sockaddr *)&gateway,
		.gateway_len = sizeof(gateway),
		.connect_timeout_ms = 1000,
		.io_timeout_ms = 1000,
		.certificate = certificate,
		.certificate_len = sizeof(certificate),
		.platform_public_key = platform_public_key,
		.platform_type = SC1777Y_PLATFORM_NANRUI,
	};

	mqtt_client_init(&client);
	zassert_ok(sc1777y_secure_channel_init(&channel, &config));
}

static void adapter_before(void *fixture)
{
	ARG_UNUSED(fixture);
	init_channel();
}

ZTEST(sc1777y_secure_mqtt_transport, test_bind_selects_custom_transport_and_gateway)
{
	zassert_ok(sc1777y_secure_mqtt_transport_bind(&client, &channel));
	zassert_equal(MQTT_TRANSPORT_CUSTOM, client.transport.type);
	zassert_equal_ptr(&channel, client.transport.custom_transport_data);
	zassert_equal_ptr(channel.config.gateway, client.broker);
}

ZTEST(sc1777y_secure_mqtt_transport, test_bind_rejects_null_arguments)
{
	zassert_equal(-EINVAL, sc1777y_secure_mqtt_transport_bind(NULL, &channel));
	zassert_equal(-EINVAL, sc1777y_secure_mqtt_transport_bind(&client, NULL));
}

ZTEST(sc1777y_secure_mqtt_transport, test_unbound_client_is_rejected)
{
	uint8_t byte = 0U;
	struct msghdr message = {0};

	zassert_equal(-EINVAL, mqtt_client_custom_transport_connect(&client));
	zassert_equal(-EINVAL, mqtt_client_custom_transport_write(&client, &byte, 1U));
	zassert_equal(-EINVAL, mqtt_client_custom_transport_write_msg(&client, &message));
	zassert_equal(-EINVAL,
		      mqtt_client_custom_transport_read(&client, &byte, 1U, false));
	zassert_equal(-EINVAL, mqtt_client_custom_transport_disconnect(&client));
}

ZTEST(sc1777y_secure_mqtt_transport, test_write_validates_data_and_propagates_error)
{
	static const uint8_t mqtt_bytes[] = {0xc0, 0x00};

	zassert_ok(sc1777y_secure_mqtt_transport_bind(&client, &channel));
	zassert_ok(mqtt_client_custom_transport_write(&client, NULL, 0U));
	zassert_equal(-EINVAL, mqtt_client_custom_transport_write(&client, NULL, 1U));
	zassert_equal(-ENOTCONN, mqtt_client_custom_transport_write(
					 &client, mqtt_bytes, sizeof(mqtt_bytes)));
}

ZTEST(sc1777y_secure_mqtt_transport, test_write_msg_handles_empty_iovecs_and_errors)
{
	static const uint8_t mqtt_bytes[] = {0xc0, 0x00};
	struct iovec iov[] = {
		{.iov_base = NULL, .iov_len = 0U},
		{.iov_base = (void *)mqtt_bytes, .iov_len = sizeof(mqtt_bytes)},
	};
	struct msghdr empty = {0};
	struct msghdr invalid = {.msg_iov = NULL, .msg_iovlen = 1U};
	struct iovec null_iov = {.iov_base = NULL, .iov_len = 1U};
	struct msghdr null_data = {.msg_iov = &null_iov, .msg_iovlen = 1U};
	struct msghdr message = {.msg_iov = iov, .msg_iovlen = ARRAY_SIZE(iov)};
	struct iovec original[ARRAY_SIZE(iov)];

	memcpy(original, iov, sizeof(iov));
	zassert_ok(sc1777y_secure_mqtt_transport_bind(&client, &channel));
	zassert_equal(-EINVAL, mqtt_client_custom_transport_write_msg(&client, NULL));
	zassert_ok(mqtt_client_custom_transport_write_msg(&client, &empty));
	zassert_equal(-EINVAL, mqtt_client_custom_transport_write_msg(&client, &invalid));
	zassert_equal(-EINVAL, mqtt_client_custom_transport_write_msg(&client, &null_data));
	zassert_equal(-ENOTCONN,
		      mqtt_client_custom_transport_write_msg(&client, &message));
	zassert_mem_equal(original, iov, sizeof(iov));
}

ZTEST(sc1777y_secure_mqtt_transport, test_read_propagates_disconnected_error)
{
	uint8_t data[4];

	zassert_ok(sc1777y_secure_mqtt_transport_bind(&client, &channel));
	zassert_equal(-EINVAL,
		      mqtt_client_custom_transport_read(&client, NULL, sizeof(data), false));
	zassert_equal(-ENOTCONN,
		      mqtt_client_custom_transport_read(&client, data, sizeof(data), false));
}

ZTEST(sc1777y_secure_mqtt_transport, test_connect_and_disconnect_delegate_lifecycle)
{
	zassert_ok(sc1777y_secure_mqtt_transport_bind(&client, &channel));
	channel.state = SC1777Y_SECURE_CHANNEL_FAILED;
	zassert_equal(-EALREADY, mqtt_client_custom_transport_connect(&client));
	zassert_ok(mqtt_client_custom_transport_disconnect(&client));
	zassert_equal(SC1777Y_SECURE_CHANNEL_CLOSED,
		      sc1777y_secure_channel_get_state(&channel));
}

ZTEST_SUITE(sc1777y_secure_mqtt_transport, NULL, NULL, adapter_before, NULL, NULL);
