/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/net/sc1777y_secure_channel.h>
#include <zephyr/ztest.h>

#include "test_gateway.h"

#define SC1777Y_NODE DT_ALIAS(sc1777y_0)

static const uint8_t certificate[] = {0x01};
static const uint8_t platform_public_key[SC1777Y_PLATFORM_PUBLIC_KEY_LEN] = {0};
static struct sc1777y_secure_channel channel;
static struct test_gateway gateway;
static uint8_t input[3000];
static uint8_t output[3000];

static void connect_channel(void)
{
	struct sc1777y_secure_channel_config config = {
		.sc1777y = DEVICE_DT_GET(SC1777Y_NODE),
		.gateway = test_gateway_address(&gateway),
		.gateway_len = sizeof(struct sockaddr_in),
		.connect_timeout_ms = 1000,
		.io_timeout_ms = 1000,
		.certificate = certificate,
		.certificate_len = sizeof(certificate),
		.platform_public_key = platform_public_key,
		.platform_type = SC1777Y_PLATFORM_NANRUI,
	};

	zassert_ok(sc1777y_secure_channel_init(&channel, &config));
	zassert_ok(sc1777y_secure_channel_connect(&channel));
}

static void fill_input(size_t len)
{
	for (size_t i = 0U; i < len; ++i) {
		input[i] = (uint8_t)(i * 37U + 11U);
	}
}

static void recv_all(size_t len)
{
	size_t received = 0U;

	while (received < len) {
		int ret = sc1777y_secure_channel_recv(&channel, &output[received], len - received,
						      true);

		zassert_true(ret > 0, "recv returned %d after %zu bytes", ret, received);
		received += (size_t)ret;
	}
}

ZTEST(sc1777y_secure_record, test_send_and_recv_preserve_byte_stream_boundaries)
{
	static const size_t lengths[] = {1U, 15U, 16U, 17U, 2047U, 3000U};

	for (size_t i = 0U; i < ARRAY_SIZE(lengths); ++i) {
		size_t expected_records = DIV_ROUND_UP(lengths[i],
						       SC1777Y_SECURE_MAX_PLAINTEXT_CHUNK);

		fill_input(lengths[i]);
		memset(output, 0, lengths[i]);
		test_gateway_start(&gateway, TEST_GATEWAY_RECORD_ECHO);
		test_gateway_expect_plaintext(&gateway, lengths[i]);
		connect_channel();
		zassert_ok(sc1777y_secure_channel_send(&channel, input, lengths[i]));
		recv_all(lengths[i]);
		zassert_mem_equal(input, output, lengths[i]);
		zassert_equal(expected_records, test_gateway_record_count(&gateway));
		zassert_mem_equal(input, test_gateway_record_data(&gateway), lengths[i]);
		zassert_ok(sc1777y_secure_channel_close(&channel));
		test_gateway_wait(&gateway);
	}
}

ZTEST(sc1777y_secure_record, test_recv_caches_plaintext_remainder)
{
	uint8_t first[3];
	uint8_t second[5];

	test_gateway_start(&gateway, TEST_GATEWAY_RECORD_SEND);
	connect_channel();
	test_gateway_queue_plaintext(&gateway, (const uint8_t *)"12345678", 8U);
	zassert_equal(3, sc1777y_secure_channel_recv(&channel, first, sizeof(first), true));
	zassert_equal(5, sc1777y_secure_channel_recv(&channel, second, sizeof(second), false));
	zassert_mem_equal("123", first, sizeof(first));
	zassert_mem_equal("45678", second, sizeof(second));
	zassert_ok(sc1777y_secure_channel_close(&channel));
	test_gateway_wait(&gateway);
}

ZTEST(sc1777y_secure_record, test_nonblocking_recv_retains_fragmented_record)
{
	static const uint8_t plaintext[] = "fragmented record";
	int initial_ret;
	int fragmented_ret = 0;
	size_t partial_header_used;
	size_t partial_plain_len;
	int complete_ret;

	test_gateway_start(&gateway, TEST_GATEWAY_RECORD_FRAGMENTED);
	connect_channel();
	initial_ret = sc1777y_secure_channel_recv(&channel, output, sizeof(output), false);
	test_gateway_queue_plaintext(&gateway, plaintext, sizeof(plaintext) - 1U);
	test_gateway_wait_fragment(&gateway);
	for (size_t i = 0U; i < 100U; ++i) {
		fragmented_ret = sc1777y_secure_channel_recv(&channel, output, sizeof(output),
							     false);
		if (channel.header_used > 0U) {
			break;
		}
		k_yield();
	}
	partial_header_used = channel.header_used;
	partial_plain_len = channel.plain_len;
	test_gateway_release_fragment(&gateway);
	complete_ret = sc1777y_secure_channel_recv(&channel, output, sizeof(output), true);
	zassert_ok(sc1777y_secure_channel_close(&channel));
	test_gateway_wait(&gateway);

	zassert_equal(-EAGAIN, initial_ret);
	zassert_equal(-EAGAIN, fragmented_ret);
	zassert_equal(1, partial_header_used);
	zassert_equal(0, partial_plain_len);
	zassert_equal(sizeof(plaintext) - 1U, complete_ret);
	zassert_mem_equal(plaintext, output, sizeof(plaintext) - 1U);
}

ZTEST(sc1777y_secure_record, test_recv_separates_coalesced_records)
{
	const size_t len = SC1777Y_SECURE_MAX_PLAINTEXT_CHUNK + 3U;

	fill_input(len);
	test_gateway_start(&gateway, TEST_GATEWAY_RECORD_COALESCED);
	connect_channel();
	test_gateway_queue_plaintext(&gateway, input, len);
	zassert_equal(SC1777Y_SECURE_MAX_PLAINTEXT_CHUNK,
		      sc1777y_secure_channel_recv(&channel, output, sizeof(output), true));
	zassert_equal(3, sc1777y_secure_channel_recv(
				 &channel, &output[SC1777Y_SECURE_MAX_PLAINTEXT_CHUNK], 3U, false));
	zassert_mem_equal(input, output, len);
	zassert_equal(2, test_gateway_record_count(&gateway));
	zassert_ok(sc1777y_secure_channel_close(&channel));
	test_gateway_wait(&gateway);
}

static void expect_record_failure(enum test_gateway_mode mode, int expected_error)
{
	test_gateway_start(&gateway, mode);
	connect_channel();
	zassert_equal(expected_error,
		      sc1777y_secure_channel_recv(&channel, output, sizeof(output), true));
	zassert_equal(SC1777Y_SECURE_CHANNEL_FAILED,
		      sc1777y_secure_channel_get_state(&channel));
	zassert_equal(-1, channel.socket_fd);
	test_gateway_wait(&gateway);
}

ZTEST(sc1777y_secure_record, test_recv_rejects_malformed_padding_and_ciphertext_length)
{
	expect_record_failure(TEST_GATEWAY_RECORD_BAD_PADDING, -EBADMSG);
	expect_record_failure(TEST_GATEWAY_RECORD_BAD_LENGTH, -EBADMSG);
}

ZTEST(sc1777y_secure_record, test_recv_returns_zero_when_peer_closes_mid_record)
{
	test_gateway_start(&gateway, TEST_GATEWAY_RECORD_HALF_CLOSE);
	connect_channel();
	zassert_equal(0, sc1777y_secure_channel_recv(&channel, output, sizeof(output), true));
	zassert_equal(SC1777Y_SECURE_CHANNEL_CLOSED,
		      sc1777y_secure_channel_get_state(&channel));
	zassert_equal(-1, channel.socket_fd);
	zassert_equal(0, channel.header_used);
	zassert_equal(0, channel.record_used);
	zassert_equal(0, channel.rx_record[0]);
	zassert_equal(0, channel.rx_record[4]);
	test_gateway_wait(&gateway);
}

ZTEST(sc1777y_secure_record, test_explicit_close_clears_cached_plaintext_before_reconnect)
{
	static const uint8_t old_plaintext[] = "old-data";
	uint8_t first[3];
	int reconnect_ret;

	test_gateway_start(&gateway, TEST_GATEWAY_RECORD_RECONNECT);
	connect_channel();
	test_gateway_queue_plaintext(&gateway, old_plaintext, sizeof(old_plaintext) - 1U);
	zassert_equal(sizeof(first),
		      sc1777y_secure_channel_recv(&channel, first, sizeof(first), true));
	zassert_mem_equal("old", first, sizeof(first));
	zassert_ok(sc1777y_secure_channel_close(&channel));
	zassert_ok(sc1777y_secure_channel_connect(&channel));
	reconnect_ret = sc1777y_secure_channel_recv(&channel, output, sizeof(output), false);
	zassert_ok(sc1777y_secure_channel_close(&channel));
	test_gateway_wait(&gateway);

	zassert_equal(-EAGAIN, reconnect_ret);
	zassert_equal(0, channel.header_used);
	zassert_equal(0, channel.record_used);
	zassert_equal(0, channel.plain_offset);
	zassert_equal(0, channel.plain_len);
	zassert_equal(0, channel.rx_record[0]);
	zassert_equal(0, channel.plain_cache[0]);
}

ZTEST(sc1777y_secure_record, test_recv_rejects_padding_only_record)
{
	enum sc1777y_secure_channel_state state;
	int socket_fd;
	int ret;

	test_gateway_start(&gateway, TEST_GATEWAY_RECORD_EMPTY);
	connect_channel();
	ret = sc1777y_secure_channel_recv(&channel, output, sizeof(output), true);
	state = sc1777y_secure_channel_get_state(&channel);
	socket_fd = channel.socket_fd;
	if (socket_fd >= 0) {
		zassert_ok(sc1777y_secure_channel_close(&channel));
	}
	test_gateway_wait(&gateway);

	zassert_equal(-EBADMSG, ret);
	zassert_equal(SC1777Y_SECURE_CHANNEL_FAILED, state);
	zassert_equal(-1, socket_fd);
}

ZTEST_SUITE(sc1777y_secure_record, NULL, NULL, NULL, NULL, NULL);
