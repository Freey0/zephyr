/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_NET_SC1777Y_SECURE_CHANNEL_H_
#define ZEPHYR_INCLUDE_NET_SC1777Y_SECURE_CHANNEL_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/drivers/misc/sc1777y.h>
#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SC1777Y_SECURE_SIM_LEN 16U
#define SC1777Y_SECURE_DEVICE_ID_LEN 18U
#define SC1777Y_SECURE_MAX_CERTIFICATE_LEN 1878U
#define SC1777Y_SECURE_MAX_HANDSHAKE_LEN 2112U
#define SC1777Y_SECURE_MAX_PLAINTEXT_CHUNK 2047U
#define SC1777Y_SECURE_MAX_CIPHERTEXT_LEN 2048U
#define SC1777Y_SECURE_MAX_RECORD_LEN 2068U

enum sc1777y_secure_channel_state {
	SC1777Y_SECURE_CHANNEL_DISCONNECTED,
	SC1777Y_SECURE_CHANNEL_TCP_CONNECTED,
	SC1777Y_SECURE_CHANNEL_NEGOTIATING,
	SC1777Y_SECURE_CHANNEL_ESTABLISHED,
	SC1777Y_SECURE_CHANNEL_FAILED,
	SC1777Y_SECURE_CHANNEL_CLOSED,
};

struct sc1777y_secure_channel_config {
	const struct device *sc1777y;
	const struct sockaddr *gateway;
	socklen_t gateway_len;
	const char *if_name;
	int32_t connect_timeout_ms;
	int32_t io_timeout_ms;
	const uint8_t *certificate;
	size_t certificate_len;
	const uint8_t *platform_public_key;
	uint8_t sim[SC1777Y_SECURE_SIM_LEN];
	uint8_t device_id[SC1777Y_SECURE_DEVICE_ID_LEN];
	enum sc1777y_platform_type platform_type;
};

struct sc1777y_secure_channel {
	struct sc1777y_secure_channel_config config;
	struct sockaddr_storage gateway_storage;
	int socket_fd;
	enum sc1777y_secure_channel_state state;
	struct k_mutex tx_lock;
	struct k_mutex rx_lock;
	struct k_mutex crypto_lock;
	uint8_t tx_work[SC1777Y_SECURE_MAX_HANDSHAKE_LEN];
	uint8_t rx_record[SC1777Y_SECURE_MAX_RECORD_LEN];
	uint8_t plain_cache[SC1777Y_SECURE_MAX_PLAINTEXT_CHUNK];
	size_t header_used;
	size_t record_expected;
	size_t record_used;
	size_t plain_offset;
	size_t plain_len;
};

int sc1777y_secure_channel_init(struct sc1777y_secure_channel *channel,
				const struct sc1777y_secure_channel_config *config);
int sc1777y_secure_channel_connect(struct sc1777y_secure_channel *channel);
int sc1777y_secure_channel_send(struct sc1777y_secure_channel *channel,
				const uint8_t *data, size_t len);
int sc1777y_secure_channel_recv(struct sc1777y_secure_channel *channel,
				uint8_t *data, size_t size, bool shall_block);
int sc1777y_secure_channel_close(struct sc1777y_secure_channel *channel);
enum sc1777y_secure_channel_state
sc1777y_secure_channel_get_state(const struct sc1777y_secure_channel *channel);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_NET_SC1777Y_SECURE_CHANNEL_H_ */
