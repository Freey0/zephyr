/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_TESTS_NET_LIB_SC1777Y_SECURE_CHANNEL_UNIT_TEST_GATEWAY_H_
#define ZEPHYR_TESTS_NET_LIB_SC1777Y_SECURE_CHANNEL_UNIT_TEST_GATEWAY_H_

#include <stdbool.h>

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>

enum test_gateway_mode {
	TEST_GATEWAY_SUCCESS,
	TEST_GATEWAY_WRONG_SUBTYPE,
	TEST_GATEWAY_WRONG_LENGTH,
	TEST_GATEWAY_WRONG_SN,
	TEST_GATEWAY_SHORT_RESPONSE,
	TEST_GATEWAY_PEER_CLOSE,
};

struct test_gateway {
	struct sockaddr_in address;
	int listen_fd;
	int result;
	enum test_gateway_mode mode;
	bool saw_confirm;
	bool joined;
	struct k_sem done;
	struct k_thread thread;

	K_KERNEL_STACK_MEMBER(stack, 4096);
};

void test_gateway_start(struct test_gateway *gateway, enum test_gateway_mode mode);
const struct sockaddr *test_gateway_address(const struct test_gateway *gateway);
bool test_gateway_saw_confirm(struct test_gateway *gateway);
void test_gateway_wait(struct test_gateway *gateway);

#endif /* ZEPHYR_TESTS_NET_LIB_SC1777Y_SECURE_CHANNEL_UNIT_TEST_GATEWAY_H_ */
