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
	TEST_GATEWAY_RECORD_ECHO,
	TEST_GATEWAY_RECORD_SEND,
	TEST_GATEWAY_RECORD_FRAGMENTED,
	TEST_GATEWAY_RECORD_COALESCED,
	TEST_GATEWAY_RECORD_BAD_PADDING,
	TEST_GATEWAY_RECORD_BAD_LENGTH,
	TEST_GATEWAY_RECORD_HALF_CLOSE,
	TEST_GATEWAY_RECORD_EMPTY,
	TEST_GATEWAY_RECORD_RECONNECT,
	TEST_GATEWAY_WRONG_SUBTYPE,
	TEST_GATEWAY_WRONG_LENGTH,
	TEST_GATEWAY_WRONG_SN,
	TEST_GATEWAY_SHORT_RESPONSE,
	TEST_GATEWAY_PEER_CLOSE,
	TEST_GATEWAY_HANDSHAKE_STALL,
	TEST_GATEWAY_RECORD_HEADER_STALL,
	TEST_GATEWAY_RECORD_PARTIAL_STALL,
	TEST_GATEWAY_RECORD_SEND_STALL,
};

struct test_gateway {
	struct sockaddr_in address;
	int listen_fd;
	int result;
	enum test_gateway_mode mode;
	bool saw_confirm;
	bool joined;
	size_t expected_plaintext_len;
	size_t record_data_len;
	size_t record_count;
	size_t connection_count;
	uint8_t record_data[4096];
	struct k_sem done;
	struct k_sem record_ready;
	struct k_sem fragment_started;
	struct k_sem fragment_continue;
	struct k_thread thread;

	K_KERNEL_STACK_MEMBER(stack, 8192);
};

void test_gateway_start(struct test_gateway *gateway, enum test_gateway_mode mode);
const struct sockaddr *test_gateway_address(const struct test_gateway *gateway);
bool test_gateway_saw_confirm(struct test_gateway *gateway);
void test_gateway_expect_plaintext(struct test_gateway *gateway, size_t len);
void test_gateway_queue_plaintext(struct test_gateway *gateway, const uint8_t *data, size_t len);
void test_gateway_wait_fragment(struct test_gateway *gateway);
void test_gateway_release_fragment(struct test_gateway *gateway);
size_t test_gateway_record_count(const struct test_gateway *gateway);
const uint8_t *test_gateway_record_data(const struct test_gateway *gateway);
void test_gateway_wait(struct test_gateway *gateway);

#endif /* ZEPHYR_TESTS_NET_LIB_SC1777Y_SECURE_CHANNEL_UNIT_TEST_GATEWAY_H_ */
