/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include "secure_channel_internal.h"

#define SC1777Y_SECURE_RECORD_HEADER_LEN 4U
#define SC1777Y_SECURE_RECORD_FIXED_LEN 20U

static void reset_record_state(struct sc1777y_secure_channel *channel)
{
	channel->header_used = 0U;
	channel->record_expected = 0U;
	channel->record_used = 0U;
}

static int send_chunk(struct sc1777y_secure_channel *channel, const uint8_t *data, size_t len)
{
	const struct device *dev = channel->config.sc1777y;
	uint8_t iv[SC1777Y_IV_LEN];
	uint8_t *ciphertext = &channel->tx_work[SC1777Y_SECURE_RECORD_FIXED_LEN];
	size_t padded_len = 0U;
	size_t ciphertext_len = 0U;
	size_t record_len = 0U;
	int ret;

	ret = k_mutex_lock(&channel->tx_lock, K_FOREVER);
	if (ret < 0) {
		return ret;
	}

	memcpy(ciphertext, data, len);
	ret = sc1777y_secure_pad(ciphertext, len, SC1777Y_SECURE_MAX_CIPHERTEXT_LEN,
				 &padded_len);
	if (ret < 0) {
		goto out;
	}

	ret = sc1777y_get_random(dev, iv, sizeof(iv));
	if (ret < 0) {
		goto out;
	}

	ret = k_mutex_lock(&channel->crypto_lock, K_FOREVER);
	if (ret < 0) {
		goto out;
	}
	ret = sc1777y_import_iv(dev, iv);
	if (ret == 0) {
		ret = sc1777y_session_encrypt(dev, ciphertext, padded_len, ciphertext,
					      SC1777Y_SECURE_MAX_CIPHERTEXT_LEN, &ciphertext_len);
	}
	(void)k_mutex_unlock(&channel->crypto_lock);
	if (ret < 0) {
		goto out;
	}
	if (ciphertext_len != padded_len) {
		ret = -EIO;
		goto out;
	}

	ret = sc1777y_secure_encode_record(iv, ciphertext, ciphertext_len, channel->tx_work,
					   sizeof(channel->tx_work), &record_len);
	if (ret == 0) {
		ret = sc1777y_secure_socket_send_all(channel, channel->tx_work, record_len);
	}

out:
	memset(iv, 0, sizeof(iv));
	memset(channel->tx_work, 0, sizeof(channel->tx_work));
	(void)k_mutex_unlock(&channel->tx_lock);
	return ret;
}

int sc1777y_secure_channel_send(struct sc1777y_secure_channel *channel, const uint8_t *data,
				size_t len)
{
	size_t offset = 0U;

	if ((channel == NULL) || ((data == NULL) && (len > 0U))) {
		return -EINVAL;
	}
	if (channel->state != SC1777Y_SECURE_CHANNEL_ESTABLISHED) {
		return -ENOTCONN;
	}

	while (offset < len) {
		size_t chunk_len = MIN(len - offset, SC1777Y_SECURE_MAX_PLAINTEXT_CHUNK);
		int ret = send_chunk(channel, &data[offset], chunk_len);

		if (ret < 0) {
			return sc1777y_secure_channel_fail(channel, ret);
		}
		offset += chunk_len;
	}

	return 0;
}

static int socket_recv(struct sc1777y_secure_channel *channel, uint8_t *data, size_t len,
			       bool shall_block)
{
	int flags = shall_block ? 0 : ZSOCK_MSG_DONTWAIT;

	for (;;) {
		ssize_t ret = zsock_recv(channel->socket_fd, data, len, flags);

		if (ret > 0) {
			return (int)ret;
		}
		if (ret == 0) {
			(void)sc1777y_secure_channel_close(channel);
			return 0;
		}
		if (errno == EINTR) {
			continue;
		}
		if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
			return -EAGAIN;
		}
		return -errno;
	}
}

static int receive_record(struct sc1777y_secure_channel *channel, bool shall_block)
{
	while (channel->header_used < SC1777Y_SECURE_RECORD_HEADER_LEN) {
		int ret = socket_recv(channel, &channel->rx_record[channel->header_used],
				      SC1777Y_SECURE_RECORD_HEADER_LEN - channel->header_used,
				      shall_block);

		if (ret <= 0) {
			return ret;
		}
		channel->header_used += (size_t)ret;
	}

	if (channel->record_expected == 0U) {
		size_t ciphertext_len;
		int ret = sc1777y_secure_decode_record_header(channel->rx_record,
							     &channel->record_expected,
							     &ciphertext_len);

		if (ret < 0) {
			return ret;
		}
		channel->record_used = SC1777Y_SECURE_RECORD_HEADER_LEN;
	}

	while (channel->record_used < channel->record_expected) {
		int ret = socket_recv(channel, &channel->rx_record[channel->record_used],
				      channel->record_expected - channel->record_used, shall_block);

		if (ret <= 0) {
			return ret;
		}
		channel->record_used += (size_t)ret;
	}

	return 1;
}

static int decrypt_record(struct sc1777y_secure_channel *channel)
{
	const struct device *dev = channel->config.sc1777y;
	uint8_t *record_body = &channel->rx_record[SC1777Y_SECURE_RECORD_FIXED_LEN];
	size_t ciphertext_len = channel->record_expected - SC1777Y_SECURE_RECORD_FIXED_LEN;
	size_t decrypted_len = 0U;
	size_t plain_len = 0U;
	int ret;

	ret = k_mutex_lock(&channel->crypto_lock, K_FOREVER);
	if (ret < 0) {
		return ret;
	}
	ret = sc1777y_import_iv(dev, &channel->rx_record[SC1777Y_SECURE_RECORD_HEADER_LEN]);
	if (ret == 0) {
		ret = sc1777y_session_decrypt(dev, record_body, ciphertext_len, record_body,
					      SC1777Y_SECURE_MAX_CIPHERTEXT_LEN, &decrypted_len);
	}
	(void)k_mutex_unlock(&channel->crypto_lock);
	if (ret < 0) {
		return ret;
	}
	if (decrypted_len != ciphertext_len) {
		return -EIO;
	}

	ret = sc1777y_secure_unpad(record_body, decrypted_len, &plain_len);
	if (ret < 0) {
		memset(record_body, 0, decrypted_len);
		return ret;
	}
	if (plain_len == 0U) {
		memset(record_body, 0, decrypted_len);
		return -EBADMSG;
	}

	memcpy(channel->plain_cache, record_body, plain_len);
	channel->plain_offset = 0U;
	channel->plain_len = plain_len;
	memset(channel->rx_record, 0, channel->record_expected);
	reset_record_state(channel);
	return 0;
}

static int copy_plaintext(struct sc1777y_secure_channel *channel, uint8_t *data, size_t size)
{
	size_t available = channel->plain_len - channel->plain_offset;
	size_t copied = MIN(size, available);

	memcpy(data, &channel->plain_cache[channel->plain_offset], copied);
	memset(&channel->plain_cache[channel->plain_offset], 0, copied);
	channel->plain_offset += copied;
	if (channel->plain_offset == channel->plain_len) {
		channel->plain_offset = 0U;
		channel->plain_len = 0U;
	}

	return (int)copied;
}

int sc1777y_secure_channel_recv(struct sc1777y_secure_channel *channel, uint8_t *data,
				size_t size, bool shall_block)
{
	int ret;

	if ((channel == NULL) || ((data == NULL) && (size > 0U))) {
		return -EINVAL;
	}
	if (channel->state != SC1777Y_SECURE_CHANNEL_ESTABLISHED) {
		return -ENOTCONN;
	}
	if (size == 0U) {
		return 0;
	}

	ret = k_mutex_lock(&channel->rx_lock, K_FOREVER);
	if (ret < 0) {
		return sc1777y_secure_channel_fail(channel, ret);
	}

	if (channel->plain_len > channel->plain_offset) {
		ret = copy_plaintext(channel, data, size);
		goto out;
	}

	ret = receive_record(channel, shall_block);
	if (ret <= 0) {
		if ((ret < 0) && (ret != -EAGAIN)) {
			ret = sc1777y_secure_channel_fail(channel, ret);
		}
		goto out;
	}

	ret = decrypt_record(channel);
	if (ret < 0) {
		ret = sc1777y_secure_channel_fail(channel, ret);
		goto out;
	}
	ret = copy_plaintext(channel, data, size);

out:
	(void)k_mutex_unlock(&channel->rx_lock);
	return ret;
}
