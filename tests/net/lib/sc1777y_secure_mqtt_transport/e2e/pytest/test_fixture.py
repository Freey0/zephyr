# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path

import conftest


def test_broker_config_is_exact() -> None:
    assert conftest.MOSQUITTO_CONFIG == (
        "listener 18884 127.0.0.1\n"
        "allow_anonymous true\n"
        "persistence false\n"
        "log_type all\n"
    )


def test_subscriber_command_observes_exact_topics() -> None:
    assert conftest._subscriber_command(Path("/usr/bin/mosquitto_sub")) == [
        "/usr/bin/mosquitto_sub",
        "-h",
        "127.0.0.1",
        "-p",
        "18884",
        "-q",
        "1",
        "-v",
        "-t",
        "/v1/devices/869010075627892/topo/add",
        "-t",
        "/v1/devices/869010075627892/datas",
        "-t",
        "/v1/devices/869010075627892/test-up",
    ]


def test_publisher_command_injects_qos1_downlink() -> None:
    assert conftest._publisher_command(
        Path("/usr/bin/mosquitto_pub"), "phase2-downstream"
    ) == [
        "/usr/bin/mosquitto_pub",
        "-h",
        "127.0.0.1",
        "-p",
        "18884",
        "-q",
        "1",
        "-t",
        "/v1/devices/869010075627892/commands",
        "-m",
        "phase2-downstream",
    ]


def test_subscriber_output_keeps_payload_spaces() -> None:
    message = conftest._parse_subscriber_line(
        "/v1/devices/869010075627892/datas payload with spaces\n"
    )

    assert message.topic == "/v1/devices/869010075627892/datas"
    assert message.payload == "payload with spaces"


def test_real_mosquitto_broker_reaches_readiness_and_stops(tmp_path) -> None:
    executable = conftest._require_program("mosquitto")
    config = tmp_path / "mosquitto.conf"
    config.write_text(conftest.MOSQUITTO_CONFIG, encoding="utf-8")
    broker = conftest.MosquittoBroker(executable, config)

    try:
        broker.start()
        assert broker.is_running
        assert broker.wait_for_log(
            "Opening ipv4 listen socket on port 18884", timeout=5.0
        )
    finally:
        broker.stop()

    assert not broker.is_running
