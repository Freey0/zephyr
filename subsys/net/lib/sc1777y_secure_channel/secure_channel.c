/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/net/sc1777y_secure_channel.h>

#include "secure_channel_internal.h"

static int validate_gateway(const struct sockaddr *gateway, socklen_t gateway_len)
{
	if ((gateway == NULL) || (gateway_len < sizeof(gateway->sa_family))) {
		return -EINVAL;
	}

	switch (gateway->sa_family) {
	case AF_INET:
		return gateway_len == sizeof(struct sockaddr_in) ? 0 : -EINVAL;
	case AF_INET6:
		return gateway_len == sizeof(struct sockaddr_in6) ? 0 : -EINVAL;
	default:
		return -EAFNOSUPPORT;
	}
}

void sc1777y_secure_channel_clear_rx(struct sc1777y_secure_channel *channel)
{
	memset(channel->rx_record, 0, sizeof(channel->rx_record));
	memset(channel->plain_cache, 0, sizeof(channel->plain_cache));
	channel->header_used = 0U;
	channel->record_expected = 0U;
	channel->record_used = 0U;
	channel->plain_offset = 0U;
	channel->plain_len = 0U;
}

static void cleanup_channel(struct sc1777y_secure_channel *channel)
{
	sc1777y_secure_socket_close(channel);
	memset(channel->tx_work, 0, sizeof(channel->tx_work));
	sc1777y_secure_channel_clear_rx(channel);
}

int sc1777y_secure_channel_init(struct sc1777y_secure_channel *channel,
				const struct sc1777y_secure_channel_config *config)
{
	int ret;

	if ((channel == NULL) || (config == NULL)) {
		return -EINVAL;
	}

	if ((config->sc1777y == NULL) || !device_is_ready(config->sc1777y)) {
		return -EINVAL;
	}

	ret = validate_gateway(config->gateway, config->gateway_len);
	if (ret < 0) {
		return ret;
	}

	if ((config->connect_timeout_ms <= 0) || (config->io_timeout_ms <= 0)) {
		return -EINVAL;
	}

	if ((config->certificate == NULL) || (config->certificate_len == 0U) ||
	    (config->certificate_len > SC1777Y_SECURE_MAX_CERTIFICATE_LEN) ||
	    (config->platform_public_key == NULL)) {
		return -EINVAL;
	}

	if ((config->platform_type != SC1777Y_PLATFORM_NANRUI) &&
	    (config->platform_type != SC1777Y_PLATFORM_WANGAN)) {
		return -EINVAL;
	}

	channel->config = *config;
	memset(&channel->gateway_storage, 0, sizeof(channel->gateway_storage));
	memcpy(&channel->gateway_storage, config->gateway, config->gateway_len);
	channel->config.gateway = (const struct sockaddr *)&channel->gateway_storage;
	channel->socket_fd = -1;
	channel->state = SC1777Y_SECURE_CHANNEL_DISCONNECTED;
	k_mutex_init(&channel->tx_lock);
	k_mutex_init(&channel->rx_lock);
	k_mutex_init(&channel->crypto_lock);
	memset(channel->tx_work, 0, sizeof(channel->tx_work));
	sc1777y_secure_channel_clear_rx(channel);

	return 0;
}

enum sc1777y_secure_channel_state
sc1777y_secure_channel_get_state(const struct sc1777y_secure_channel *channel)
{
	return channel->state;
}

int sc1777y_secure_channel_fail(struct sc1777y_secure_channel *channel, int ret)
{
	cleanup_channel(channel);
	channel->state = SC1777Y_SECURE_CHANNEL_FAILED;
	return ret;
}

int sc1777y_secure_channel_connect(struct sc1777y_secure_channel *channel)
{
	int ret;

	if (channel == NULL) {
		return -EINVAL;
	}
	if (((channel->state != SC1777Y_SECURE_CHANNEL_DISCONNECTED) &&
	     (channel->state != SC1777Y_SECURE_CHANNEL_CLOSED)) ||
	    (channel->socket_fd >= 0)) {
		return -EALREADY;
	}

	ret = sc1777y_secure_socket_connect(channel);
	if (ret < 0) {
		return ret;
	}

	channel->state = SC1777Y_SECURE_CHANNEL_TCP_CONNECTED;
	return sc1777y_secure_handshake(channel);
}

int sc1777y_secure_channel_close(struct sc1777y_secure_channel *channel)
{
	if (channel == NULL) {
		return -EINVAL;
	}

	cleanup_channel(channel);
	channel->state = SC1777Y_SECURE_CHANNEL_CLOSED;
	return 0;
}
