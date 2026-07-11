/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/net/mqtt.h>
#include <zephyr/net/sc1777y_secure_channel.h>
#include <zephyr/net/sc1777y_secure_mqtt_transport.h>

static struct sc1777y_secure_channel *channel_from_client(struct mqtt_client *client)
{
	if ((client == NULL) || (client->transport.type != MQTT_TRANSPORT_CUSTOM)) {
		return NULL;
	}

	return client->transport.custom_transport_data;
}

int sc1777y_secure_mqtt_transport_bind(struct mqtt_client *client,
				       struct sc1777y_secure_channel *channel)
{
	if ((client == NULL) || (channel == NULL)) {
		return -EINVAL;
	}

	client->broker = channel->config.gateway;
	client->transport.type = MQTT_TRANSPORT_CUSTOM;
	client->transport.custom_transport_data = channel;

	return 0;
}

int mqtt_client_custom_transport_connect(struct mqtt_client *client)
{
	struct sc1777y_secure_channel *channel = channel_from_client(client);

	return channel == NULL ? -EINVAL : sc1777y_secure_channel_connect(channel);
}

int mqtt_client_custom_transport_write(struct mqtt_client *client, const uint8_t *data,
				       uint32_t datalen)
{
	struct sc1777y_secure_channel *channel = channel_from_client(client);

	if ((channel == NULL) || ((data == NULL) && (datalen != 0U))) {
		return -EINVAL;
	}

	return datalen == 0U ? 0 : sc1777y_secure_channel_send(channel, data, datalen);
}

int mqtt_client_custom_transport_write_msg(struct mqtt_client *client,
					   const struct msghdr *message)
{
	struct sc1777y_secure_channel *channel = channel_from_client(client);

	if ((channel == NULL) || (message == NULL) ||
	    ((message->msg_iov == NULL) && (message->msg_iovlen != 0U))) {
		return -EINVAL;
	}

	for (size_t i = 0U; i < message->msg_iovlen; ++i) {
		const struct iovec *iov = &message->msg_iov[i];
		int ret;

		if (iov->iov_len == 0U) {
			continue;
		}

		ret = sc1777y_secure_channel_send(channel, iov->iov_base, iov->iov_len);
		if (ret < 0) {
			return ret;
		}
	}

	return 0;
}

int mqtt_client_custom_transport_read(struct mqtt_client *client, uint8_t *data,
				      uint32_t buflen, bool shall_block)
{
	struct sc1777y_secure_channel *channel = channel_from_client(client);

	return channel == NULL
		       ? -EINVAL
		       : sc1777y_secure_channel_recv(channel, data, buflen, shall_block);
}

int mqtt_client_custom_transport_disconnect(struct mqtt_client *client)
{
	struct sc1777y_secure_channel *channel = channel_from_client(client);

	return channel == NULL ? -EINVAL : sc1777y_secure_channel_close(channel);
}
