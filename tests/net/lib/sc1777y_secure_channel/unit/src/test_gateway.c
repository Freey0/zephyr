/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/sys/byteorder.h>
#include <zephyr/net/sc1777y_secure_channel.h>
#include <zephyr/ztest.h>

#include "test_gateway.h"

#define TEST_GATEWAY_RESPONSE_LEN 230U
#define TEST_GATEWAY_CONFIRM_LEN 184U
#define TEST_GATEWAY_RECORD_HEADER_LEN 4U
#define TEST_GATEWAY_RECORD_FIXED_LEN 20U
#define TEST_GATEWAY_CRYPTO_MASK 0xa5U

static int recv_exact(int fd, uint8_t *data, size_t len)
{
	size_t received = 0U;

	while (received < len) {
		ssize_t ret = zsock_recv(fd, &data[received], len - received, 0);

		if (ret > 0) {
			received += (size_t)ret;
			continue;
		}
		if (ret == 0) {
			return -ECONNRESET;
		}
		if (errno != EINTR) {
			return -errno;
		}
	}

	return 0;
}

static int send_all(int fd, const uint8_t *data, size_t len)
{
	size_t sent = 0U;

	while (sent < len) {
		ssize_t ret = zsock_send(fd, &data[sent], len - sent, 0);

		if (ret > 0) {
			sent += (size_t)ret;
			continue;
		}
		if (ret == 0) {
			return -ECONNRESET;
		}
		if (errno != EINTR) {
			return -errno;
		}
	}

	return 0;
}

static int send_fragmented_response(int fd, const uint8_t *response, size_t len)
{
	static const size_t fragment_sizes[] = {1U, 3U, 17U};
	size_t offset = 0U;
	int ret;

	for (size_t i = 0U; i < ARRAY_SIZE(fragment_sizes); ++i) {
		ret = send_all(fd, &response[offset], fragment_sizes[i]);
		if (ret < 0) {
			return ret;
		}
		offset += fragment_sizes[i];
		k_yield();
	}

	return send_all(fd, &response[offset], len - offset);
}

static int decode_plaintext(const uint8_t *record, size_t record_len, uint8_t *plaintext,
			    size_t plaintext_size, size_t *plaintext_len)
{
	size_t ciphertext_len;
	size_t marker;

	if ((record_len < TEST_GATEWAY_RECORD_FIXED_LEN) || (record[0] != 2U) ||
	    (record[1] != 0U) || (sys_get_be16(&record[2]) != record_len)) {
		return -EPROTO;
	}

	ciphertext_len = record_len - TEST_GATEWAY_RECORD_FIXED_LEN;
	if ((ciphertext_len == 0U) || ((ciphertext_len % SC1777Y_IV_LEN) != 0U) ||
	    (ciphertext_len > plaintext_size)) {
		return -EBADMSG;
	}

	for (size_t i = 0U; i < ciphertext_len; ++i) {
		plaintext[i] = record[TEST_GATEWAY_RECORD_FIXED_LEN + i] ^
			       TEST_GATEWAY_CRYPTO_MASK;
	}

	marker = ciphertext_len;
	while ((marker > 0U) && (plaintext[marker - 1U] == 0U)) {
		--marker;
	}
	if ((marker == 0U) || (plaintext[marker - 1U] != 0x80U) ||
	    (ciphertext_len - marker >= SC1777Y_IV_LEN)) {
		return -EBADMSG;
	}

	*plaintext_len = marker - 1U;
	return 0;
}

static size_t encode_record(const uint8_t *plaintext, size_t plaintext_len, uint8_t *record);

static int run_record_echo(struct test_gateway *gateway, int client_fd, uint8_t *record,
			   size_t record_size)
{
	uint8_t plaintext[SC1777Y_SECURE_MAX_CIPHERTEXT_LEN];
	size_t echo_offset = 0U;

	while (gateway->record_data_len < gateway->expected_plaintext_len) {
		size_t plaintext_len;
		size_t record_len;
		int ret;

		ret = recv_exact(client_fd, record, TEST_GATEWAY_RECORD_HEADER_LEN);
		if (ret < 0) {
			return ret;
		}
		record_len = sys_get_be16(&record[2]);
		if ((record_len < TEST_GATEWAY_RECORD_FIXED_LEN) || (record_len > record_size)) {
			return -EPROTO;
		}
		ret = recv_exact(client_fd, &record[TEST_GATEWAY_RECORD_HEADER_LEN],
				 record_len - TEST_GATEWAY_RECORD_HEADER_LEN);
		if (ret < 0) {
			return ret;
		}
		ret = decode_plaintext(record, record_len, plaintext, sizeof(plaintext),
				       &plaintext_len);
		if (ret < 0) {
			return ret;
		}
		if (plaintext_len > sizeof(gateway->record_data) - gateway->record_data_len) {
			return -EMSGSIZE;
		}
		memcpy(&gateway->record_data[gateway->record_data_len], plaintext, plaintext_len);
		gateway->record_data_len += plaintext_len;
		gateway->record_count++;
	}

	while (echo_offset < gateway->record_data_len) {
		size_t chunk_len = MIN(gateway->record_data_len - echo_offset,
				       SC1777Y_SECURE_MAX_PLAINTEXT_CHUNK);
		size_t record_len = encode_record(&gateway->record_data[echo_offset], chunk_len,
						  record);
		int ret = send_all(client_fd, record, record_len);

		if (ret < 0) {
			return ret;
		}
		echo_offset += chunk_len;
	}

	return 0;
}

static size_t encode_record(const uint8_t *plaintext, size_t plaintext_len, uint8_t *record)
{
	size_t ciphertext_len = ROUND_UP(plaintext_len + 1U, SC1777Y_IV_LEN);
	size_t record_len = TEST_GATEWAY_RECORD_FIXED_LEN + ciphertext_len;

	record[0] = 2U;
	record[1] = 0U;
	sys_put_be16((uint16_t)record_len, &record[2]);
	memset(&record[4], 0x3c, SC1777Y_IV_LEN);
	for (size_t i = 0U; i < ciphertext_len; ++i) {
		uint8_t padded = 0U;

		if (i < plaintext_len) {
			padded = plaintext[i];
		} else if (i == plaintext_len) {
			padded = 0x80U;
		}

		record[TEST_GATEWAY_RECORD_FIXED_LEN + i] = padded ^ TEST_GATEWAY_CRYPTO_MASK;
	}

	return record_len;
}

static int wait_for_peer_close(int client_fd)
{
	uint8_t byte;

	for (;;) {
		ssize_t ret = zsock_recv(client_fd, &byte, sizeof(byte), 0);

		if (ret == 0) {
			return 0;
		}
		if ((ret < 0) && (errno != EINTR)) {
			return -errno;
		}
	}
}

static int run_record_send(struct test_gateway *gateway, int client_fd, uint8_t *record)
{
	size_t offset = 0U;
	int ret;

	if (gateway->mode == TEST_GATEWAY_RECORD_BAD_PADDING) {
		memset(record, 0, 36U);
		record[0] = 2U;
		sys_put_be16(36U, &record[2]);
		memset(&record[4], 0x3c, SC1777Y_IV_LEN);
		memset(&record[TEST_GATEWAY_RECORD_FIXED_LEN], TEST_GATEWAY_CRYPTO_MASK,
		       SC1777Y_IV_LEN);
		ret = send_all(client_fd, record, 36U);
		return ret < 0 ? ret : wait_for_peer_close(client_fd);
	}

	if (gateway->mode == TEST_GATEWAY_RECORD_BAD_LENGTH) {
		memset(record, 0, 35U);
		record[0] = 2U;
		sys_put_be16(35U, &record[2]);
		memset(&record[4], 0x3c, SC1777Y_IV_LEN);
		ret = send_all(client_fd, record, 35U);
		return ret < 0 ? ret : wait_for_peer_close(client_fd);
	}

	if (gateway->mode == TEST_GATEWAY_RECORD_HALF_CLOSE) {
		memset(record, 0, 28U);
		record[0] = 2U;
		sys_put_be16(36U, &record[2]);
		memset(&record[4], 0x3c, SC1777Y_IV_LEN);
		return send_all(client_fd, record, 28U);
	}

	ret = k_sem_take(&gateway->record_ready, K_SECONDS(2));
	if (ret < 0) {
		return ret;
	}

	if (gateway->mode == TEST_GATEWAY_RECORD_COALESCED) {
		size_t wire_len = 0U;

		while (offset < gateway->record_data_len) {
			size_t chunk_len = MIN(gateway->record_data_len - offset,
					       SC1777Y_SECURE_MAX_PLAINTEXT_CHUNK);

			wire_len += encode_record(&gateway->record_data[offset], chunk_len,
						  &record[wire_len]);
			gateway->record_count++;
			offset += chunk_len;
		}
		ret = send_all(client_fd, record, wire_len);
		return ret < 0 ? ret : wait_for_peer_close(client_fd);
	}

	while (offset < gateway->record_data_len) {
		size_t chunk_len = MIN(gateway->record_data_len - offset,
				       SC1777Y_SECURE_MAX_PLAINTEXT_CHUNK);
		size_t record_len = encode_record(&gateway->record_data[offset], chunk_len, record);

		if (gateway->mode == TEST_GATEWAY_RECORD_FRAGMENTED) {
			ret = send_all(client_fd, record, 1U);
			if (ret == 0) {
				k_sem_give(&gateway->fragment_started);
				ret = k_sem_take(&gateway->fragment_continue, K_SECONDS(2));
			}
			for (size_t i = 1U; (ret == 0) && (i < record_len); ++i) {
				ret = send_all(client_fd, &record[i], 1U);
				k_yield();
			}
		} else {
			ret = send_all(client_fd, record, record_len);
		}
		if (ret < 0) {
			return ret;
		}
		gateway->record_count++;
		offset += chunk_len;
	}

	return wait_for_peer_close(client_fd);
}

static int run_gateway(struct test_gateway *gateway)
{
	uint8_t request[SC1777Y_SECURE_MAX_HANDSHAKE_LEN];
	uint8_t response[TEST_GATEWAY_RESPONSE_LEN] = {0};
	uint8_t confirm[TEST_GATEWAY_CONFIRM_LEN];
	uint16_t request_len;
	uint16_t request_sn;
	int client_fd;
	int ret;

	client_fd = zsock_accept(gateway->listen_fd, NULL, NULL);
	if (client_fd < 0) {
		return -errno;
	}

	ret = recv_exact(client_fd, request, 4U);
	if (ret < 0) {
		goto out;
	}

	request_len = sys_get_be16(&request[2]);
	if ((request_len < 8U) || (request_len > sizeof(request))) {
		ret = -EPROTO;
		goto out;
	}

	ret = recv_exact(client_fd, &request[4], request_len - 4U);
	if (ret < 0) {
		goto out;
	}

	if (gateway->mode == TEST_GATEWAY_PEER_CLOSE) {
		ret = 0;
		goto out;
	}

	request_sn = sys_get_be16(&request[6]);
	response[0] = 1U;
	response[1] = gateway->mode == TEST_GATEWAY_WRONG_SUBTYPE ? 3U : 2U;
	sys_put_be16(gateway->mode == TEST_GATEWAY_WRONG_LENGTH ?
			     TEST_GATEWAY_RESPONSE_LEN - 1U : TEST_GATEWAY_RESPONSE_LEN,
		     &response[2]);
	sys_put_be16(gateway->mode == TEST_GATEWAY_WRONG_SN ? request_sn : request_sn + 1U,
		     &response[4]);
	for (size_t i = 6U; i < sizeof(response); ++i) {
		response[i] = (uint8_t)i;
	}

	if (gateway->mode == TEST_GATEWAY_SHORT_RESPONSE) {
		ret = send_all(client_fd, response, 21U);
		goto out;
	}

	ret = send_fragmented_response(client_fd, response, sizeof(response));
	if (ret < 0) {
		goto out;
	}

	if ((gateway->mode == TEST_GATEWAY_WRONG_SUBTYPE) ||
	    (gateway->mode == TEST_GATEWAY_WRONG_LENGTH) ||
	    (gateway->mode == TEST_GATEWAY_WRONG_SN)) {
		ret = 0;
		goto out;
	}

	ret = recv_exact(client_fd, confirm, sizeof(confirm));
	if (ret == 0) {
		gateway->saw_confirm = true;
	}
	if (ret < 0) {
		goto out;
	}

	if (gateway->mode == TEST_GATEWAY_RECORD_ECHO) {
		ret = run_record_echo(gateway, client_fd, request, sizeof(request));
		if (ret == 0) {
			ret = wait_for_peer_close(client_fd);
		}
	} else if ((gateway->mode >= TEST_GATEWAY_RECORD_SEND) &&
		   (gateway->mode <= TEST_GATEWAY_RECORD_HALF_CLOSE)) {
		ret = run_record_send(gateway, client_fd, request);
	}

out:
	(void)zsock_close(client_fd);
	return ret;
}

static void gateway_thread(void *arg1, void *arg2, void *arg3)
{
	struct test_gateway *gateway = arg1;

	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);
	gateway->result = run_gateway(gateway);
	(void)zsock_close(gateway->listen_fd);
	gateway->listen_fd = -1;
	k_sem_give(&gateway->done);
}

void test_gateway_start(struct test_gateway *gateway, enum test_gateway_mode mode)
{
	static uint16_t next_port = 42420U;
	socklen_t address_len = sizeof(gateway->address);
	uint16_t port = next_port++;

	memset(gateway, 0, sizeof(*gateway));
	gateway->mode = mode;
	gateway->listen_fd = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	zassert_true(gateway->listen_fd >= 0, "socket failed: %d", errno);

	gateway->address = (struct sockaddr_in){
		.sin_family = AF_INET,
		.sin_port = htons(port),
		.sin_addr.s_addr = htonl(0x7f000001U),
	};
	zassert_ok(zsock_bind(gateway->listen_fd,
			      (const struct sockaddr *)&gateway->address,
			      sizeof(gateway->address)),
		   "bind failed: %d", errno);
	zassert_ok(zsock_getsockname(gateway->listen_fd,
				     (struct sockaddr *)&gateway->address, &address_len),
		   "getsockname failed: %d", errno);
	zassert_ok(zsock_listen(gateway->listen_fd, 1), "listen failed: %d", errno);
	k_sem_init(&gateway->done, 0, 1);
	k_sem_init(&gateway->record_ready, 0, 1);
	k_sem_init(&gateway->fragment_started, 0, 1);
	k_sem_init(&gateway->fragment_continue, 0, 1);

	k_thread_create(&gateway->thread, gateway->stack,
			K_KERNEL_STACK_SIZEOF(gateway->stack), gateway_thread, gateway, NULL,
			NULL, K_PRIO_PREEMPT(0), 0, K_NO_WAIT);
}

const struct sockaddr *test_gateway_address(const struct test_gateway *gateway)
{
	return (const struct sockaddr *)&gateway->address;
}

void test_gateway_wait(struct test_gateway *gateway)
{
	if (gateway->joined) {
		return;
	}

	zassert_ok(k_sem_take(&gateway->done, K_SECONDS(2)), "gateway did not finish");
	zassert_ok(k_thread_join(&gateway->thread, K_SECONDS(2)), "gateway did not join");
	gateway->joined = true;
	zassert_ok(gateway->result, "gateway failed: %d", gateway->result);
}

bool test_gateway_saw_confirm(struct test_gateway *gateway)
{
	test_gateway_wait(gateway);
	return gateway->saw_confirm;
}

void test_gateway_expect_plaintext(struct test_gateway *gateway, size_t len)
{
	zassert_true(len <= sizeof(gateway->record_data));
	gateway->expected_plaintext_len = len;
}

void test_gateway_queue_plaintext(struct test_gateway *gateway, const uint8_t *data, size_t len)
{
	zassert_not_null(data);
	zassert_true(len <= sizeof(gateway->record_data));
	memcpy(gateway->record_data, data, len);
	gateway->record_data_len = len;
	k_sem_give(&gateway->record_ready);
}

void test_gateway_wait_fragment(struct test_gateway *gateway)
{
	zassert_ok(k_sem_take(&gateway->fragment_started, K_SECONDS(2)),
		   "gateway did not send first fragment");
}

void test_gateway_release_fragment(struct test_gateway *gateway)
{
	k_sem_give(&gateway->fragment_continue);
}

size_t test_gateway_record_count(const struct test_gateway *gateway)
{
	return gateway->record_count;
}

const uint8_t *test_gateway_record_data(const struct test_gateway *gateway)
{
	return gateway->record_data;
}
