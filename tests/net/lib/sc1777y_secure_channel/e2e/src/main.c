/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/sc1777y_secure_channel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#define SC1777Y_NODE DT_ALIAS(sc1777y_0)
#define GATEWAY_PORT 18883

static struct sc1777y_secure_channel channel;
static uint8_t tx_payload[3000];
static uint8_t rx_payload[3000];
static const uint8_t certificate[] = "native-sim-terminal-certificate";
static const uint8_t platform_public_key[SC1777Y_PLATFORM_PUBLIC_KEY_LEN] = {
	[0 ... SC1777Y_PLATFORM_PUBLIC_KEY_LEN - 1] = 0x5a,
};

static int recv_exact(size_t length)
{
	size_t received = 0U;

	while (received < length) {
		int ret = sc1777y_secure_channel_recv(&channel, &rx_payload[received],
						 length - received, true);

		if (ret <= 0) {
			return ret == 0 ? -ECONNRESET : ret;
		}
		received += (size_t)ret;
	}

	return 0;
}

static int exchange_payload(size_t length, uint8_t seed)
{
	int ret;

	for (size_t i = 0U; i < length; ++i) {
		tx_payload[i] = seed + (uint8_t)i;
	}
	memset(rx_payload, 0, length);

	ret = sc1777y_secure_channel_send(&channel, tx_payload, length);
	if (ret < 0) {
		return ret;
	}
	ret = recv_exact(length);
	if (ret < 0) {
		return ret;
	}

	return memcmp(tx_payload, rx_payload, length) == 0 ? 0 : -EBADMSG;
}

int main(void)
{
	static const size_t lengths[] = {1U, 16U, 2047U, 3000U};
	const struct device *sc1777y = DEVICE_DT_GET(SC1777Y_NODE);
	struct sockaddr_in gateway = {
		.sin_family = AF_INET,
		.sin_port = htons(GATEWAY_PORT),
	};
	struct sc1777y_secure_channel_config config = {
		.sc1777y = sc1777y,
		.gateway = (const struct sockaddr *)&gateway,
		.gateway_len = sizeof(gateway),
		/* zeth is the host TAP name; Zephyr exposes that interface as eth0. */
		.if_name = "eth0",
		.connect_timeout_ms = 5000,
		.io_timeout_ms = 5000,
		.certificate = certificate,
		.certificate_len = sizeof(certificate) - 1U,
		.platform_public_key = platform_public_key,
		.sim = {0x31, 0x33, 0x38, 0x30, 0x30, 0x31, 0x33, 0x38},
		.device_id = {0x53, 0x43, 0x31, 0x37, 0x37, 0x37, 0x59},
		.platform_type = SC1777Y_PLATFORM_NANRUI,
	};
	int ret;

	if (!device_is_ready(sc1777y)) {
		printk("SC1777Y device is not ready\n");
		return 1;
	}
	if (zsock_inet_pton(AF_INET, "192.0.2.2", &gateway.sin_addr) != 1) {
		printk("gateway address parse failed\n");
		return 1;
	}

	ret = sc1777y_secure_channel_init(&channel, &config);
	if (ret < 0) {
		printk("secure channel init failed: %d\n", ret);
		return 1;
	}
	ret = sc1777y_secure_channel_connect(&channel);
	if (ret < 0) {
		printk("secure channel connect failed: %d\n", ret);
		return 1;
	}
	for (size_t i = 0U; i < ARRAY_SIZE(lengths); ++i) {
		ret = exchange_payload(lengths[i], (uint8_t)(0x10U + i));
		if (ret < 0) {
			printk("secure echo %zu failed: %d\n", lengths[i], ret);
			return 1;
		}
	}
	ret = sc1777y_secure_channel_close(&channel);
	if (ret < 0) {
		printk("secure channel close failed: %d\n", ret);
		return 1;
	}

	ret = sc1777y_secure_channel_connect(&channel);
	if (ret < 0) {
		printk("secure channel reconnect failed: %d\n", ret);
		return 1;
	}
	ret = exchange_payload(2047U, 0x5cU);
	if (ret < 0) {
		printk("secure echo after reconnect failed: %d\n", ret);
		return 1;
	}
	ret = sc1777y_secure_channel_close(&channel);
	if (ret < 0) {
		printk("secure channel final close failed: %d\n", ret);
		return 1;
	}

	printk("SECURE_CHANNEL_E2E_PASS\n");
	return 0;
}
