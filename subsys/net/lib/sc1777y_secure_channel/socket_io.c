/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include "secure_channel_internal.h"

static int socket_error(void)
{
	return errno == 0 ? -EIO : -errno;
}

void sc1777y_secure_socket_close(struct sc1777y_secure_channel *channel)
{
	int socket_fd = channel->socket_fd;

	channel->socket_fd = -1;
	if (socket_fd >= 0) {
		(void)zsock_close(socket_fd);
	}
}

static int fail_and_close(struct sc1777y_secure_channel *channel, int ret)
{
	sc1777y_secure_socket_close(channel);
	return ret;
}

static int set_socket_options(struct sc1777y_secure_channel *channel)
{
	struct zsock_timeval timeout = {
		.tv_sec = channel->config.io_timeout_ms / MSEC_PER_SEC,
		.tv_usec = (channel->config.io_timeout_ms % MSEC_PER_SEC) * USEC_PER_MSEC,
	};
	int ret;

	ret = zsock_setsockopt(channel->socket_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout,
			       sizeof(timeout));
	if (ret < 0) {
		return socket_error();
	}

	ret = zsock_setsockopt(channel->socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout,
			       sizeof(timeout));
	if (ret < 0) {
		return socket_error();
	}

	if (channel->config.if_name != NULL) {
		struct ifreq ifname = {0};

		strncpy(ifname.ifr_name, channel->config.if_name, sizeof(ifname.ifr_name) - 1U);
		ret = zsock_setsockopt(channel->socket_fd, SOL_SOCKET, SO_BINDTODEVICE, &ifname,
				       sizeof(ifname));
		if (ret < 0) {
			return socket_error();
		}
	}

	return 0;
}

static int wait_for_connect(struct sc1777y_secure_channel *channel)
{
	struct zsock_pollfd pollfd = {
		.fd = channel->socket_fd,
		.events = ZSOCK_POLLOUT,
	};
	int64_t deadline = k_uptime_get() + channel->config.connect_timeout_ms;
	int socket_status = 0;
	socklen_t status_len = sizeof(socket_status);
	int ret;

	for (;;) {
		int64_t remaining = deadline - k_uptime_get();

		if (remaining <= 0) {
			return -ETIMEDOUT;
		}

		ret = zsock_poll(&pollfd, 1, (int)remaining);
		if (ret > 0) {
			break;
		}
		if (ret == 0) {
			return -ETIMEDOUT;
		}
		if (errno != EINTR) {
			return socket_error();
		}
	}

	ret = zsock_getsockopt(channel->socket_fd, SOL_SOCKET, SO_ERROR, &socket_status,
			       &status_len);
	if (ret < 0) {
		return socket_error();
	}
	if (socket_status != 0) {
		return -socket_status;
	}

	return 0;
}

int sc1777y_secure_socket_connect(struct sc1777y_secure_channel *channel)
{
	int flags;
	int ret;

	channel->socket_fd = zsock_socket(channel->config.gateway->sa_family, SOCK_STREAM,
					  IPPROTO_TCP);
	if (channel->socket_fd < 0) {
		return socket_error();
	}

	ret = set_socket_options(channel);
	if (ret < 0) {
		return fail_and_close(channel, ret);
	}

	flags = zsock_fcntl(channel->socket_fd, ZVFS_F_GETFL, 0);
	if (flags < 0) {
		return fail_and_close(channel, socket_error());
	}

	ret = zsock_fcntl(channel->socket_fd, ZVFS_F_SETFL, flags | ZVFS_O_NONBLOCK);
	if (ret < 0) {
		return fail_and_close(channel, socket_error());
	}

	ret = zsock_connect(channel->socket_fd, channel->config.gateway,
			    channel->config.gateway_len);
	if ((ret < 0) && (errno != EINPROGRESS) && (errno != EALREADY) && (errno != EINTR)) {
		ret = socket_error();
		goto out;
	}
	if (ret < 0) {
		ret = wait_for_connect(channel);
	}

out:
	if ((ret == 0) &&
	    (zsock_fcntl(channel->socket_fd, ZVFS_F_SETFL, flags & ~ZVFS_O_NONBLOCK) < 0)) {
		ret = socket_error();
	}

	return ret < 0 ? fail_and_close(channel, ret) : 0;
}

int sc1777y_secure_socket_send_all(struct sc1777y_secure_channel *channel,
				   const uint8_t *data, size_t len)
{
	size_t sent = 0U;

	while (sent < len) {
		ssize_t ret = zsock_send(channel->socket_fd, &data[sent], len - sent, 0);

		if (ret > 0) {
			sent += (size_t)ret;
			continue;
		}
		if (ret == 0) {
			return fail_and_close(channel, -ECONNRESET);
		}
		if (errno != EINTR) {
			return fail_and_close(channel, socket_error());
		}
	}

	return 0;
}

int sc1777y_secure_socket_recv_exact(struct sc1777y_secure_channel *channel,
				     uint8_t *data, size_t len)
{
	size_t received = 0U;

	while (received < len) {
		ssize_t ret = zsock_recv(channel->socket_fd, &data[received], len - received, 0);

		if (ret > 0) {
			received += (size_t)ret;
			continue;
		}
		if (ret == 0) {
			return fail_and_close(channel, -ECONNRESET);
		}
		if (errno != EINTR) {
			return fail_and_close(channel, socket_error());
		}
	}

	return 0;
}
