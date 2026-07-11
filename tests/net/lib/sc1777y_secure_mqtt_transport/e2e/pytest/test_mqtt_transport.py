# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

from twister_harness import DeviceAdapter


def test_secure_mqtt_against_real_broker(
    mosquitto_broker,
    secure_gateway,
    upstream_subscriber,
    publish_downlink_qos1,
    unlaunched_dut: DeviceAdapter,
) -> None:
    unlaunched_dut.launch()
    unlaunched_dut.readlines_until("MQTT_SUBSCRIBED", timeout=30.0)
    publish_downlink_qos1("phase2-downstream")
    unlaunched_dut.readlines_until("MQTT_SECURE_E2E_PASS", timeout=30.0)

    messages = upstream_subscriber.wait_for_messages(3, timeout=10.0)
    assert messages[0].topic.endswith("/topo/add")
    assert messages[1].topic.endswith("/datas")
    assert messages[2].payload == "phase2-qos1-upstream"
    secure_gateway.raise_if_failed()
    assert secure_gateway.handshake_count == 1
