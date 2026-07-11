/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TESTS_NET_LIB_SC1777Y_SECURE_MQTT_TRANSPORT_E2E_PAYLOADS_H_
#define TESTS_NET_LIB_SC1777Y_SECURE_MQTT_TRANSPORT_E2E_PAYLOADS_H_

#define DEVICE_ID "869010075627892"
#define TOPIC_TOPO_ADD "/v1/devices/" DEVICE_ID "/topo/add"
#define TOPIC_DATA "/v1/devices/" DEVICE_ID "/datas"
#define TOPIC_DOWNLINK "/v1/devices/" DEVICE_ID "/commands"
#define TOPIC_TEST_UP "/v1/devices/" DEVICE_ID "/test-up"

static const char topo_payload[] =
	"{\"mid\":869010075627892,\"deviceInfos\":{\"nodeId\":\"869010075627892\","
	"\"name\":\"XJ_ZNJDX\",\"description\":\"XJ_ZNJDX\","
	"\"manufacturerId\":\"869010075627892\",\"model\":\"XJ_ZNJDX\"}}";

static const char data_payload[] =
	"{\"devices\": [{\"deviceId\": \"869010075627892\",\"services\": [{\"data\": {"
	"\"type\": \"400\",\"id\": \"869010075627892\",\"date\": \"260512\","
	"\"time\": \"154112\",\"height\": \"0.00000000\",\"geo_sep\": \"0.00000000\","
	"\"longitude\": \"0.00000000\",\"latitude\": \"0.00000000\","
	"\"speed\": \"0.00000000\",\"direction\": \"0.00000000\",\"voltage\": \"380\","
	"\"temperature\": \"2530\",\"state_a\": \"0\",\"state_b\": \"0\","
	"\"state_c\": \"0\",\"date1\": \"0\",\"date2\": \"0\",\"date3\": \"0\","
	"\"precision\": \"0\",\"satellite\": \"0\",\"count\": \"0\",\"alarm\": \"0\"},"
	"\"eventTime\": \"20260512T154112Z\",\"serviceId\": \"analog\"}]}]}";

#endif /* TESTS_NET_LIB_SC1777Y_SECURE_MQTT_TRANSPORT_E2E_PAYLOADS_H_ */
