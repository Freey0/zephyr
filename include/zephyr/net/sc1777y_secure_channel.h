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

/** @brief Secure-channel lifecycle state. */
enum sc1777y_secure_channel_state {
	/** Initialized, with no TCP connection opened yet. */
	SC1777Y_SECURE_CHANNEL_DISCONNECTED,
	/** The TCP connection is open, before negotiation starts. */
	SC1777Y_SECURE_CHANNEL_TCP_CONNECTED,
	/** SC1777Y-backed session negotiation is in progress. */
	SC1777Y_SECURE_CHANNEL_NEGOTIATING,
	/** Negotiation succeeded and encrypted stream I/O is permitted. */
	SC1777Y_SECURE_CHANNEL_ESTABLISHED,
	/** A connection, protocol, timeout, or cryptographic operation failed. */
	SC1777Y_SECURE_CHANNEL_FAILED,
	/** The channel was closed locally or reached peer EOF. */
	SC1777Y_SECURE_CHANNEL_CLOSED,
};

/**
 * @brief Secure-channel configuration.
 *
 * The configuration is copied by @ref sc1777y_secure_channel_init. The gateway
 * socket address itself is also copied. The caller retains ownership of the
 * certificate, platform public key, and optional interface-name buffers and
 * must keep them valid and unchanged through every connect attempt. This
 * includes a reconnect from the closed state; release them only when the
 * context will no longer be connected again or has been reinitialized with
 * replacement buffers.
 */
struct sc1777y_secure_channel_config {
	/** Ready SC1777Y device used for negotiation and record cryptography. */
	const struct device *sc1777y;
	/** Security-gateway TCP endpoint; copied during initialization. */
	const struct sockaddr *gateway;
	/** Size of @ref gateway, matching its address family. */
	socklen_t gateway_len;
	/** Optional caller-owned interface name used with `SO_BINDTODEVICE`. */
	const char *if_name;
	/** TCP connect timeout in milliseconds; must be positive. */
	int32_t connect_timeout_ms;
	/** Blocking negotiation and stream-I/O timeout in milliseconds. */
	int32_t io_timeout_ms;
	/** Caller-owned terminal certificate. */
	const uint8_t *certificate;
	/** Certificate length in the range 1..1878 bytes. */
	size_t certificate_len;
	/** Caller-owned 64-byte platform public key. */
	const uint8_t *platform_public_key;
	/** Protocol-formatted SIM field. */
	uint8_t sim[SC1777Y_SECURE_SIM_LEN];
	/** Protocol-formatted device identifier field. */
	uint8_t device_id[SC1777Y_SECURE_DEVICE_ID_LEN];
	/** Security platform selected in the SC1777Y. */
	enum sc1777y_platform_type platform_type;
};

/**
 * @brief Caller-owned secure-channel context.
 *
 * Do not inspect or modify fields directly. One send and one receive operation
 * may run concurrently; each direction is serialized independently and access
 * to the SC1777Y is serialized internally. Initialization, connect, close, and
 * state access must not run concurrently with each other or with send/receive.
 */
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

/**
 * @brief Initialize a caller-owned secure channel.
 *
 * @param channel Context to initialize.
 * @param config Configuration copied into @p channel.
 *
 * @retval 0 on success.
 * @retval -EINVAL for null/invalid configuration, unavailable device, or
 *         non-positive timeouts.
 * @retval -EAFNOSUPPORT for an unsupported gateway address family.
 * @retval -EMSGSIZE when the certificate exceeds 1878 bytes.
 */
int sc1777y_secure_channel_init(struct sc1777y_secure_channel *channel,
				const struct sc1777y_secure_channel_config *config);

/**
 * @brief Open TCP and complete the SC1777Y-backed security negotiation.
 *
 * @retval 0 after the channel reaches @ref SC1777Y_SECURE_CHANNEL_ESTABLISHED.
 * @retval -EALREADY unless the channel is disconnected or closed.
 * @retval -ETIMEDOUT when connect or blocking negotiation I/O times out.
 * @return Other negative errno values from socket, protocol, or SC1777Y
 *         operations. Such failures close the socket and leave the channel in
 *         @ref SC1777Y_SECURE_CHANNEL_FAILED.
 */
int sc1777y_secure_channel_connect(struct sc1777y_secure_channel *channel);

/**
 * @brief Send plaintext through encrypted security records.
 *
 * @retval 0 when all bytes have been sent (including a zero-length send).
 * @retval -EINVAL for invalid arguments.
 * @retval -ENOTCONN unless the channel is established.
 * @retval -ETIMEDOUT when blocking socket output times out.
 * @return Other negative errno values from socket or SC1777Y operations.
 *         Transfer errors close the socket and leave the channel failed.
 */
int sc1777y_secure_channel_send(struct sc1777y_secure_channel *channel,
				const uint8_t *data, size_t len);

/**
 * @brief Receive plaintext from encrypted security records.
 *
 * @param channel Established channel.
 * @param data Destination buffer.
 * @param size Destination capacity. Zero returns zero without reading.
 * @param shall_block Select blocking I/O when true.
 *
 * @return A positive plaintext byte count, zero for peer EOF, or a negative
 *         errno value. A nonblocking call may return -EAGAIN without changing
 *         the established state. Blocking receive timeout returns -ETIMEDOUT;
 *         all other receive/protocol/SC1777Y failures close the socket and
 *         leave the channel failed. Peer EOF closes the channel.
 */
int sc1777y_secure_channel_recv(struct sc1777y_secure_channel *channel,
				uint8_t *data, size_t size, bool shall_block);

/**
 * @brief Close the socket and erase transient channel buffers.
 *
 * This operation is idempotent.
 *
 * @retval 0 on success.
 * @retval -EINVAL when @p channel is null.
 */
int sc1777y_secure_channel_close(struct sc1777y_secure_channel *channel);

/**
 * @brief Return the current lifecycle state.
 *
 * The caller must provide external synchronization if another thread can
 * change the lifecycle state concurrently.
 */
enum sc1777y_secure_channel_state
sc1777y_secure_channel_get_state(const struct sc1777y_secure_channel *channel);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_NET_SC1777Y_SECURE_CHANNEL_H_ */
