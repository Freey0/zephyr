# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

import socket
import threading

import conftest
import pytest
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
    assert messages[0].topic == conftest.UPSTREAM_TOPICS[0]
    assert messages[0].payload == conftest.EXPECTED_TOPO_PAYLOAD
    assert messages[1].topic == conftest.UPSTREAM_TOPICS[1]
    assert messages[1].payload == conftest.EXPECTED_DATA_PAYLOAD
    assert messages[2].payload == "phase2-qos1-upstream"
    secure_gateway.raise_if_failed()
    assert secure_gateway.handshake_count == 1
    assert secure_gateway.coalesced_write_count >= 1


@pytest.mark.parametrize(
    ("faulty_secure_gateway", "failure_marker", "expected_handshakes"),
    [
        ("bad_handshake_response", "MQTT_SECURE_CONNECT_FAILURE", 0),
        ("bad_record_padding", "MQTT_SECURE_TRANSPORT_FAILURE", 1),
    ],
    indirect=["faulty_secure_gateway"],
)
def test_secure_mqtt_rejects_security_layer_faults(
    mosquitto_broker,
    faulty_secure_gateway,
    failure_marker: str,
    expected_handshakes: int,
    unlaunched_dut: DeviceAdapter,
) -> None:
    assert mosquitto_broker.is_running
    unlaunched_dut.launch()
    lines = unlaunched_dut.readlines_until(failure_marker, timeout=30.0)

    assert not any("MQTT_SECURE_E2E_PASS" in line for line in lines)
    assert faulty_secure_gateway.fault_injected
    assert faulty_secure_gateway.wait_for_terminal_close(timeout=5.0)
    assert faulty_secure_gateway.terminal_close_observed
    assert faulty_secure_gateway.handshake_request_count == 1
    assert faulty_secure_gateway.handshake_count == expected_handshakes
    faulty_secure_gateway.raise_if_failed()


def test_controlled_disconnect_shuts_down_terminal_only(monkeypatch) -> None:
    gateway = conftest.ReconnectableSecurityGatewayPeer(
        listen_addr=("127.0.0.1", 0), upstream_addr=("127.0.0.1", 1)
    )
    terminal, peer = socket.socketpair()
    listener, listener_peer = socket.socketpair()
    gateway._listener = listener
    entered = threading.Event()
    worker_errors: list[BaseException] = []

    def serve_until_terminal_shutdown(_gateway, active_terminal) -> None:
        entered.set()
        assert active_terminal.recv(1) == b""

    monkeypatch.setattr(
        conftest.SecurityGatewayPeer,
        "_serve_connection",
        serve_until_terminal_shutdown,
    )

    def run_worker() -> None:
        try:
            gateway._serve_connection(terminal)
        except BaseException as error:
            worker_errors.append(error)

    worker = threading.Thread(target=run_worker)
    worker.start()
    assert entered.wait(timeout=0.5)

    try:
        gateway.disconnect_terminal(timeout=0.5)
        worker.join(timeout=0.5)
        peer.settimeout(0.1)

        assert not worker.is_alive()
        assert worker_errors == []
        assert peer.recv(1) == b""
        assert terminal.fileno() >= 0
        assert listener.fileno() >= 0
        assert not gateway._stop.is_set()
    finally:
        terminal.close()
        peer.close()
        listener.close()
        listener_peer.close()


def test_controlled_disconnect_does_not_hide_worker_error(monkeypatch) -> None:
    gateway = conftest.ReconnectableSecurityGatewayPeer(
        listen_addr=("127.0.0.1", 0), upstream_addr=("127.0.0.1", 1)
    )
    terminal, peer = socket.socketpair()
    entered = threading.Event()
    release_error = threading.Event()
    injected_error = OSError("injected upstream failure")

    def fail_after_terminal_shutdown(_gateway, active_terminal) -> None:
        del active_terminal
        entered.set()
        assert release_error.wait(timeout=0.5)
        raise injected_error

    monkeypatch.setattr(
        conftest.SecurityGatewayPeer,
        "_serve_connection",
        fail_after_terminal_shutdown,
    )

    def run_worker() -> None:
        try:
            gateway._serve_connection(terminal)
        except BaseException as error:
            gateway.errors.append(error)

    worker = threading.Thread(target=run_worker)
    worker.start()
    assert entered.wait(timeout=0.5)
    release_timer = threading.Timer(0.05, release_error.set)
    release_timer.start()

    try:
        gateway.disconnect_terminal(timeout=0.5)
        worker.join(timeout=0.5)

        assert not worker.is_alive()
        assert gateway.errors == [injected_error]
    finally:
        release_error.set()
        release_timer.join(timeout=0.5)
        terminal.close()
        peer.close()


def test_controlled_disconnect_reports_active_terminal_shutdown_error() -> None:
    gateway = conftest.ReconnectableSecurityGatewayPeer(
        listen_addr=("127.0.0.1", 0), upstream_addr=("127.0.0.1", 1)
    )
    shutdown_error = OSError("injected terminal shutdown failure")

    class FailingTerminal:
        def shutdown(self, how: int) -> None:
            assert how == socket.SHUT_RDWR
            raise shutdown_error

    terminal = FailingTerminal()
    gateway._set_controlled_terminal(terminal)

    with pytest.raises(OSError) as raised:
        gateway.disconnect_terminal(timeout=0.01)

    assert raised.value is shutdown_error


def test_shutdown_error_is_ignored_after_worker_replaces_terminal() -> None:
    gateway = conftest.ReconnectableSecurityGatewayPeer(
        listen_addr=("127.0.0.1", 0), upstream_addr=("127.0.0.1", 1)
    )
    replacement = object()

    class RetiredTerminal:
        def shutdown(self, how: int) -> None:
            assert how == socket.SHUT_RDWR
            gateway._set_controlled_terminal(replacement)
            raise OSError("retired terminal shutdown raced with worker")

    gateway._set_controlled_terminal(RetiredTerminal())

    gateway.disconnect_terminal(timeout=0.01)

    assert gateway._controlled_terminal is replacement


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
    assert secure_gateway.coalesced_write_count >= 1
