/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/sc1777y_secure_channel.h>
#include <zephyr/net/sc1777y_secure_mqtt_transport.h>
#include <zephyr/sys/printk.h>

#include "payloads.h"

#define SC1777Y_NODE DT_ALIAS(sc1777y_0)
#define GATEWAY_PORT 18883
#define MQTT_BUFFER_SIZE 4096U
#define SUBSCRIBE_MESSAGE_ID 1U
#define UPSTREAM_MESSAGE_ID 2U
#define EVENT_LOOP_DELAY_MS 10
#define EVENT_LOOP_TIMEOUT_MS 20000

static const uint8_t certificate[] = "native-sim-terminal-certificate";
static const uint8_t platform_public_key[SC1777Y_PLATFORM_PUBLIC_KEY_LEN] = {
	[0 ... SC1777Y_PLATFORM_PUBLIC_KEY_LEN - 1] = 0x5a,
};
static const uint8_t mqtt_client_id[] = "sc1777y-terminal-" DEVICE_ID;
static const uint8_t qos1_upstream_payload[] = "phase2-qos1-upstream";
static const uint8_t expected_downlink_payload[] = "phase2-downstream";

static struct sc1777y_secure_channel secure_channel;
static struct mqtt_client mqtt_client;
static uint8_t mqtt_rx_buffer[MQTT_BUFFER_SIZE];
static uint8_t mqtt_tx_buffer[MQTT_BUFFER_SIZE];
static uint8_t downlink_buffer[128];

struct acceptance_state {
	bool connack_received;
	bool suback_received;
	uint8_t qos0_publish_count;
	bool qos1_upstream_sent;
	bool qos1_upstream_acked;
	bool downlink_received;
	bool downlink_acked;
	bool pingresp_received;
	bool disconnect_received;
	int error;
};

static struct acceptance_state state;

static void record_error(const char *operation, int error)
{
	if (state.error == 0) {
		state.error = error < 0 ? error : -EIO;
		printk("%s failed: %d\n", operation, error);
	}
}

static bool topic_matches(const struct mqtt_utf8 *topic, const char *expected)
{
	size_t expected_len = strlen(expected);

	return topic->size == expected_len &&
	       memcmp(topic->utf8, expected, expected_len) == 0;
}

static int publish_message(const char *topic, const uint8_t *payload, size_t payload_len,
			   enum mqtt_qos qos, uint16_t message_id)
{
	struct mqtt_publish_param publish = {
		.message = {
			.topic = {
				.topic = {
					.utf8 = (uint8_t *)topic,
					.size = strlen(topic),
				},
				.qos = qos,
			},
			.payload = {
				.data = (uint8_t *)payload,
				.len = payload_len,
			},
		},
		.message_id = message_id,
	};

	return mqtt_publish(&mqtt_client, &publish);
}

static int publish_acceptance_messages(void)
{
	int ret;

	ret = publish_message(TOPIC_TOPO_ADD, (const uint8_t *)topo_payload,
			      sizeof(topo_payload) - 1U, MQTT_QOS_0_AT_MOST_ONCE, 0U);
	if (ret < 0) {
		return ret;
	}
	state.qos0_publish_count++;

	ret = publish_message(TOPIC_DATA, (const uint8_t *)data_payload,
			      sizeof(data_payload) - 1U, MQTT_QOS_0_AT_MOST_ONCE, 0U);
	if (ret < 0) {
		return ret;
	}
	state.qos0_publish_count++;

	ret = publish_message(TOPIC_TEST_UP, qos1_upstream_payload,
			      sizeof(qos1_upstream_payload) - 1U,
			      MQTT_QOS_1_AT_LEAST_ONCE, UPSTREAM_MESSAGE_ID);
	if (ret == 0) {
		state.qos1_upstream_sent = true;
	}

	return ret;
}

static int subscribe_downlink(void)
{
	struct mqtt_topic topic = {
		.topic = {
			.utf8 = (uint8_t *)TOPIC_DOWNLINK,
			.size = sizeof(TOPIC_DOWNLINK) - 1U,
		},
		.qos = MQTT_QOS_1_AT_LEAST_ONCE,
	};
	const struct mqtt_subscription_list subscription = {
		.list = &topic,
		.list_count = 1U,
		.message_id = SUBSCRIBE_MESSAGE_ID,
	};

	return mqtt_subscribe(&mqtt_client, &subscription);
}

static int receive_downlink(struct mqtt_client *client,
			    const struct mqtt_publish_param *publish)
{
	const size_t payload_len = publish->message.payload.len;
	const struct mqtt_puback_param ack = {
		.message_id = publish->message_id,
	};
	size_t received = 0U;
	int ret;

	if (!topic_matches(&publish->message.topic.topic, TOPIC_DOWNLINK) ||
	    publish->message.topic.qos != MQTT_QOS_1_AT_LEAST_ONCE ||
	    payload_len != sizeof(expected_downlink_payload) - 1U) {
		return -EBADMSG;
	}

	while (received < payload_len) {
		ret = mqtt_read_publish_payload_blocking(client, &downlink_buffer[received],
						 payload_len - received);
		if (ret <= 0) {
			return ret == 0 ? -ECONNRESET : ret;
		}
		received += (size_t)ret;
	}

	if (memcmp(downlink_buffer, expected_downlink_payload, payload_len) != 0) {
		return -EBADMSG;
	}
	state.downlink_received = true;

	ret = mqtt_publish_qos1_ack(client, &ack);
	if (ret == 0) {
		state.downlink_acked = true;
		printk("MQTT_DOWNLINK_ACK\n");
	}

	return ret;
}

static void mqtt_event_handler(struct mqtt_client *client, const struct mqtt_evt *event)
{
	int ret;

	switch (event->type) {
	case MQTT_EVT_CONNACK:
		if (event->result != 0) {
			record_error("MQTT CONNACK", event->result);
			break;
		}
		state.connack_received = true;
		ret = subscribe_downlink();
		if (ret < 0) {
			record_error("MQTT subscribe", ret);
		}
		break;
	case MQTT_EVT_SUBACK:
		if (event->result != 0 ||
		    event->param.suback.message_id != SUBSCRIBE_MESSAGE_ID ||
		    event->param.suback.return_codes.len != 1U ||
		    event->param.suback.return_codes.data[0] != MQTT_SUBACK_SUCCESS_QoS_1) {
			record_error("MQTT SUBACK", event->result != 0 ? event->result : -EPROTO);
			break;
		}
		state.suback_received = true;
		printk("MQTT_SUBSCRIBED\n");
		ret = publish_acceptance_messages();
		if (ret < 0) {
			record_error("MQTT publish", ret);
		}
		break;
	case MQTT_EVT_PUBACK:
		if (event->result != 0 ||
		    event->param.puback.message_id != UPSTREAM_MESSAGE_ID) {
			record_error("MQTT PUBACK", event->result != 0 ? event->result : -EPROTO);
			break;
		}
		state.qos1_upstream_acked = true;
		printk("MQTT_UPSTREAM_PUBACK\n");
		break;
	case MQTT_EVT_PUBLISH:
		ret = receive_downlink(client, &event->param.publish);
		if (ret < 0) {
			record_error("MQTT downlink", ret);
		}
		break;
	case MQTT_EVT_PINGRESP:
		if (event->result != 0) {
			record_error("MQTT PINGRESP", event->result);
			break;
		}
		state.pingresp_received = true;
		printk("MQTT_PINGRESP\n");
		break;
	case MQTT_EVT_DISCONNECT:
		state.disconnect_received = true;
		if (event->result != 0) {
			record_error("MQTT disconnect event", event->result);
		}
		break;
	default:
		break;
	}
}

static bool acceptance_flow_complete(void)
{
	return state.connack_received && state.suback_received &&
	       state.qos0_publish_count == 2U && state.qos1_upstream_sent &&
	       state.qos1_upstream_acked && state.downlink_received &&
	       state.downlink_acked && state.pingresp_received;
}

static void configure_mqtt_client(void)
{
	mqtt_client.evt_cb = mqtt_event_handler;
	mqtt_client.client_id.utf8 = (uint8_t *)mqtt_client_id;
	mqtt_client.client_id.size = sizeof(mqtt_client_id) - 1U;
	mqtt_client.rx_buf = mqtt_rx_buffer;
	mqtt_client.rx_buf_size = sizeof(mqtt_rx_buffer);
	mqtt_client.tx_buf = mqtt_tx_buffer;
	mqtt_client.tx_buf_size = sizeof(mqtt_tx_buffer);
	mqtt_client.keepalive = 5U;
	mqtt_client.protocol_version = MQTT_VERSION_3_1_1;
	mqtt_client.clean_session = 1U;
}

int main(void)
{
	const struct device *sc1777y = DEVICE_DT_GET(SC1777Y_NODE);
	struct sockaddr_in gateway = {
		.sin_family = AF_INET,
		.sin_port = htons(GATEWAY_PORT),
	};
	const struct sc1777y_secure_channel_config secure_config = {
		.sc1777y = sc1777y,
		.gateway = (const struct sockaddr *)&gateway,
		.gateway_len = sizeof(gateway),
		/* zeth is the host TAP; Zephyr exposes the peer interface as eth0. */
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
	int elapsed_ms = 0;
	int ret;

	if (!device_is_ready(sc1777y)) {
		printk("SC1777Y device is not ready\n");
		return 1;
	}
	if (net_addr_pton(AF_INET, "192.0.2.2", &gateway.sin_addr) != 0) {
		printk("gateway address parse failed\n");
		return 1;
	}

	ret = sc1777y_secure_channel_init(&secure_channel, &secure_config);
	if (ret < 0) {
		printk("secure channel init failed: %d\n", ret);
		return 1;
	}
	mqtt_client_init(&mqtt_client);
	configure_mqtt_client();
	ret = sc1777y_secure_mqtt_transport_bind(&mqtt_client, &secure_channel);
	if (ret < 0) {
		printk("secure MQTT transport bind failed: %d\n", ret);
		return 1;
	}
	ret = mqtt_connect(&mqtt_client);
	if (ret < 0) {
		printk("MQTT connect failed: %d\n", ret);
		(void)sc1777y_secure_channel_close(&secure_channel);
		return 1;
	}

	while (elapsed_ms < EVENT_LOOP_TIMEOUT_MS && state.error == 0 &&
	       !acceptance_flow_complete()) {
		ret = mqtt_input(&mqtt_client);
		if (ret < 0 && ret != -EAGAIN) {
			record_error("mqtt_input", ret);
			break;
		}

		ret = mqtt_live(&mqtt_client);
		if (ret < 0 && ret != -EAGAIN) {
			record_error("mqtt_live", ret);
			break;
		}

		k_msleep(EVENT_LOOP_DELAY_MS);
		elapsed_ms += EVENT_LOOP_DELAY_MS;
	}

	if (state.error != 0 || !acceptance_flow_complete()) {
		printk("MQTT acceptance flow failed: %d\n",
		       state.error != 0 ? state.error : -ETIMEDOUT);
		(void)mqtt_abort(&mqtt_client);
		return 1;
	}

	ret = mqtt_disconnect(&mqtt_client, NULL);
	if (ret < 0 || !state.disconnect_received) {
		printk("MQTT disconnect failed: %d\n", ret);
		return 1;
	}

	printk("MQTT_SECURE_E2E_PASS\n");
	return 0;
}
