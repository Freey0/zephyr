/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/net/sc1777y_secure_channel.h>
#include <zephyr/ztest.h>

#define SC1777Y_NODE DT_ALIAS(sc1777y_0)

static const uint8_t certificate[] = {0x01};
static const uint8_t platform_public_key[SC1777Y_PLATFORM_PUBLIC_KEY_LEN] = {0};

static struct sc1777y_secure_channel_config valid_config(struct sockaddr_in *gateway)
{
	*gateway = (struct sockaddr_in){
		.sin_family = AF_INET,
		.sin_port = htons(443),
		.sin_addr.s_addr = htonl(0x7f000001U),
	};

	return (struct sc1777y_secure_channel_config){
		.sc1777y = DEVICE_DT_GET(SC1777Y_NODE),
		.gateway = (const struct sockaddr *)gateway,
		.gateway_len = sizeof(*gateway),
		.connect_timeout_ms = 1000,
		.io_timeout_ms = 1000,
		.certificate = certificate,
		.certificate_len = sizeof(certificate),
		.platform_public_key = platform_public_key,
		.platform_type = SC1777Y_PLATFORM_NANRUI,
	};
}

ZTEST(sc1777y_secure_channel, test_init_rejects_null_arguments)
{
	struct sc1777y_secure_channel channel;
	struct sc1777y_secure_channel_config config = {0};

	zassert_equal(-EINVAL, sc1777y_secure_channel_init(NULL, &config));
	zassert_equal(-EINVAL, sc1777y_secure_channel_init(&channel, NULL));
}

ZTEST(sc1777y_secure_channel, test_init_rejects_invalid_device_and_gateway)
{
	struct sc1777y_secure_channel channel;
	struct sockaddr_in gateway;
	struct sc1777y_secure_channel_config config = valid_config(&gateway);

	config.sc1777y = NULL;
	zassert_equal(-EINVAL, sc1777y_secure_channel_init(&channel, &config));

	config = valid_config(&gateway);
	config.gateway = NULL;
	zassert_equal(-EINVAL, sc1777y_secure_channel_init(&channel, &config));

	config = valid_config(&gateway);
	gateway.sin_family = AF_UNSPEC;
	zassert_equal(-EAFNOSUPPORT, sc1777y_secure_channel_init(&channel, &config));

	config = valid_config(&gateway);
	config.gateway_len = sizeof(gateway) - 1U;
	zassert_equal(-EINVAL, sc1777y_secure_channel_init(&channel, &config));
}

ZTEST(sc1777y_secure_channel, test_init_rejects_invalid_timeouts_and_credentials)
{
	struct sc1777y_secure_channel channel;
	struct sockaddr_in gateway;
	struct sc1777y_secure_channel_config config = valid_config(&gateway);

	config.connect_timeout_ms = 0;
	zassert_equal(-EINVAL, sc1777y_secure_channel_init(&channel, &config));

	config = valid_config(&gateway);
	config.io_timeout_ms = -1;
	zassert_equal(-EINVAL, sc1777y_secure_channel_init(&channel, &config));

	config = valid_config(&gateway);
	config.certificate = NULL;
	zassert_equal(-EINVAL, sc1777y_secure_channel_init(&channel, &config));

	config = valid_config(&gateway);
	config.certificate_len = 0;
	zassert_equal(-EINVAL, sc1777y_secure_channel_init(&channel, &config));

	config = valid_config(&gateway);
	config.certificate_len = SC1777Y_SECURE_MAX_CERTIFICATE_LEN + 1U;
	zassert_equal(-EINVAL, sc1777y_secure_channel_init(&channel, &config));

	config = valid_config(&gateway);
	config.platform_public_key = NULL;
	zassert_equal(-EINVAL, sc1777y_secure_channel_init(&channel, &config));
}

ZTEST(sc1777y_secure_channel, test_init_rejects_unsupported_platform_types)
{
	struct sc1777y_secure_channel channel;
	struct sockaddr_in gateway;
	struct sc1777y_secure_channel_config config = valid_config(&gateway);

	config.platform_type = SC1777Y_PLATFORM_UNSET;
	zassert_equal(-EINVAL, sc1777y_secure_channel_init(&channel, &config));

	config = valid_config(&gateway);
	config.platform_type = (enum sc1777y_platform_type)3;
	zassert_equal(-EINVAL, sc1777y_secure_channel_init(&channel, &config));
}

ZTEST(sc1777y_secure_channel, test_init_copies_config_and_initializes_context)
{
	struct sc1777y_secure_channel channel;
	struct sockaddr_in gateway;
	struct sc1777y_secure_channel_config config = valid_config(&gateway);

	zassert_true(device_is_ready(config.sc1777y));
	zassert_ok(sc1777y_secure_channel_init(&channel, &config));
	zassert_not_equal(channel.config.gateway, config.gateway);
	zassert_equal(channel.config.gateway, (const struct sockaddr *)&channel.gateway_storage);
	zassert_mem_equal(channel.config.gateway, &gateway, sizeof(gateway));
	zassert_equal(channel.config.certificate, certificate);
	zassert_equal(channel.config.platform_public_key, platform_public_key);
	zassert_equal(channel.socket_fd, -1);
	zassert_equal(sc1777y_secure_channel_get_state(&channel),
		      SC1777Y_SECURE_CHANNEL_DISCONNECTED);
	zassert_equal(channel.header_used, 0);
	zassert_equal(channel.record_expected, 0);
	zassert_equal(channel.record_used, 0);
	zassert_equal(channel.plain_offset, 0);
	zassert_equal(channel.plain_len, 0);
	zassert_ok(k_mutex_lock(&channel.tx_lock, K_NO_WAIT));
	zassert_ok(k_mutex_unlock(&channel.tx_lock));
	zassert_ok(k_mutex_lock(&channel.rx_lock, K_NO_WAIT));
	zassert_ok(k_mutex_unlock(&channel.rx_lock));
	zassert_ok(k_mutex_lock(&channel.crypto_lock, K_NO_WAIT));
	zassert_ok(k_mutex_unlock(&channel.crypto_lock));
}

ZTEST(sc1777y_secure_channel, test_init_accepts_and_copies_ipv6_gateway)
{
	struct sc1777y_secure_channel channel;
	struct sockaddr_in gateway;
	struct sockaddr_in6 gateway6 = {
		.sin6_family = AF_INET6,
		.sin6_port = htons(443),
		.sin6_addr.s6_addr = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
	};
	struct sc1777y_secure_channel_config config = valid_config(&gateway);

	config.gateway = (const struct sockaddr *)&gateway6;
	config.gateway_len = sizeof(gateway6);

	zassert_ok(sc1777y_secure_channel_init(&channel, &config));
	zassert_equal(channel.config.gateway, (const struct sockaddr *)&channel.gateway_storage);
	zassert_mem_equal(channel.config.gateway, &gateway6, sizeof(gateway6));
}

ZTEST_SUITE(sc1777y_secure_channel, NULL, NULL, NULL, NULL, NULL);
