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
