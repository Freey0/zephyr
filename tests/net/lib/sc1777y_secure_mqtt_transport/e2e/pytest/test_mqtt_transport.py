# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

import socket

import conftest
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


def test_controlled_disconnect_closes_terminal_only() -> None:
    gateway = conftest.ReconnectableSecurityGatewayPeer(
        listen_addr=("127.0.0.1", 0), upstream_addr=("127.0.0.1", 1)
    )
    terminal, peer = socket.socketpair()
    listener, listener_peer = socket.socketpair()
    gateway._listener = listener
    gateway._set_controlled_terminal(terminal)

    try:
        gateway.disconnect_terminal(timeout=0.1)
        peer.settimeout(0.1)

        assert peer.recv(1) == b""
        assert listener.fileno() >= 0
        assert not gateway._stop.is_set()
    finally:
        peer.close()
        listener.close()
        listener_peer.close()


def test_secure_mqtt_reconnects_with_fresh_security_handshake(
    mosquitto_broker,
    secure_gateway,
    upstream_subscriber,
    publish_downlink_qos1,
    unlaunched_dut: DeviceAdapter,
) -> None:
    unlaunched_dut.launch()
    unlaunched_dut.readlines_until("MQTT_UPSTREAM_PUBACK", timeout=30.0)
    secure_gateway.disconnect_terminal(timeout=5.0)
    unlaunched_dut.readlines_until("MQTT_SUBSCRIBED", timeout=30.0)
    publish_downlink_qos1("phase2-downstream")
    unlaunched_dut.readlines_until("MQTT_SECURE_E2E_PASS", timeout=30.0)
    unlaunched_dut.readlines_until("MQTT_SECURE_RECONNECT_PASS", timeout=5.0)

    messages = upstream_subscriber.wait_for_messages(4, timeout=10.0)
    assert messages[-1].payload == "phase2-after-reconnect"
    secure_gateway.raise_if_failed()
    assert secure_gateway.handshake_count == 2
