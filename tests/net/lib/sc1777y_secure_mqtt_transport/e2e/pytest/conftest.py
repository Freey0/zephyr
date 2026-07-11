# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import logging
import os
import shutil
import socket
import subprocess
import sys
import threading
import time
from collections.abc import Callable, Generator
from contextlib import suppress
from dataclasses import dataclass
from pathlib import Path

import pytest

HOST_DIR = (
    Path(__file__).resolve().parents[3] / "sc1777y_secure_channel" / "host"
)
sys.path.insert(0, str(HOST_DIR))

from security_gateway_peer import SecurityGatewayPeer  # noqa: E402

LOGGER = logging.getLogger(__name__)

BROKER_HOST = "127.0.0.1"
BROKER_PORT = 18884
GATEWAY_HOST = "192.0.2.2"
GATEWAY_PORT = 18883
DEVICE_TOPIC_PREFIX = "/v1/devices/869010075627892"
UPSTREAM_TOPICS = (
    f"{DEVICE_TOPIC_PREFIX}/topo/add",
    f"{DEVICE_TOPIC_PREFIX}/datas",
    f"{DEVICE_TOPIC_PREFIX}/test-up",
)
DOWNLINK_TOPIC = f"{DEVICE_TOPIC_PREFIX}/commands"
MOSQUITTO_CONFIG = (
    "listener 18884 127.0.0.1\n"
    "allow_anonymous true\n"
    "persistence false\n"
    "log_type all\n"
)


class TapFixtureError(RuntimeError):
    """A diagnostic failure while creating, validating, or removing the TAP."""


class ReconnectableSecurityGatewayPeer(SecurityGatewayPeer):
    """Security gateway with a test-controlled terminal-side disconnect."""

    def __init__(
        self, listen_addr: tuple[str, int], upstream_addr: tuple[str, int]
    ) -> None:
        super().__init__(listen_addr=listen_addr, upstream_addr=upstream_addr)
        self._terminal_condition = threading.Condition()
        self._controlled_terminal: socket.socket | None = None

    def _set_controlled_terminal(self, terminal: socket.socket | None) -> None:
        with self._terminal_condition:
            self._controlled_terminal = terminal
            self._terminal_condition.notify_all()

    def disconnect_terminal(self, timeout: float) -> None:
        """Shut down the active terminal and wait for its worker to exit."""
        deadline = time.monotonic() + timeout
        with self._terminal_condition:
            while self._controlled_terminal is None:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError("security gateway has no active terminal connection")
                self._terminal_condition.wait(remaining)
            terminal = self._controlled_terminal

        with suppress(OSError):
            terminal.shutdown(socket.SHUT_RDWR)

        with self._terminal_condition:
            while self._controlled_terminal is terminal:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError(
                        "security gateway terminal worker did not exit"
                    )
                self._terminal_condition.wait(remaining)

    def _serve_connection(self, terminal: socket.socket) -> None:
        self._set_controlled_terminal(terminal)
        try:
            super()._serve_connection(terminal)
        finally:
            with self._terminal_condition:
                if self._controlled_terminal is terminal:
                    self._controlled_terminal = None
                self._terminal_condition.notify_all()


def _require_program(name: str) -> Path:
    executable = shutil.which(name)
    if executable is None:
        pytest.fail(f"required host program is missing: {name}", pytrace=False)
    return Path(executable)


def _subscriber_command(executable: Path) -> list[str]:
    command = [
        str(executable),
        "-h",
        BROKER_HOST,
        "-p",
        str(BROKER_PORT),
        "-q",
        "1",
        "-v",
    ]
    for topic in UPSTREAM_TOPICS:
        command.extend(("-t", topic))
    return command


def _publisher_command(executable: Path, payload: str) -> list[str]:
    return [
        str(executable),
        "-h",
        BROKER_HOST,
        "-p",
        str(BROKER_PORT),
        "-q",
        "1",
        "-t",
        DOWNLINK_TOPIC,
        "-m",
        payload,
    ]


class MosquittoBroker:
    """An isolated real Mosquitto process with observable readiness."""

    def __init__(self, executable: Path, config: Path) -> None:
        self.executable = executable
        self.config = config
        self._process: subprocess.Popen[str] | None = None
        self._reader: threading.Thread | None = None
        self._logs: list[str] = []
        self._condition = threading.Condition()

    @property
    def is_running(self) -> bool:
        return self._process is not None and self._process.poll() is None

    @property
    def log_count(self) -> int:
        with self._condition:
            return len(self._logs)

    def start(self) -> None:
        if self._process is not None:
            raise RuntimeError("Mosquitto Broker is already started")
        try:
            self._process = subprocess.Popen(
                [str(self.executable), "-c", str(self.config), "-v"],
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                bufsize=1,
            )
        except OSError as error:
            self._process = None
            raise RuntimeError(f"could not start Mosquitto Broker: {error}") from error
        self._reader = threading.Thread(
            target=self._read_logs, name="mosquitto-log", daemon=True
        )
        self._reader.start()
        if not self.wait_for_log(
            "Opening ipv4 listen socket on port 18884", timeout=5.0
        ):
            details = self.log_text
            self.stop()
            raise RuntimeError(f"Mosquitto Broker did not become ready: {details}")

    @property
    def log_text(self) -> str:
        with self._condition:
            return "".join(self._logs)

    def wait_for_log(self, text: str, timeout: float, start: int = 0) -> bool:
        deadline = time.monotonic() + timeout
        with self._condition:
            while True:
                if any(text in line for line in self._logs[start:]):
                    return True
                if self._process is not None and self._process.poll() is not None:
                    return False
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    return False
                self._condition.wait(remaining)

    def stop(self) -> None:
        process = self._process
        reader = self._reader
        if process is not None and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=3.0)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=3.0)
        if process is not None and process.stdout is not None:
            process.stdout.close()
        if reader is not None:
            reader.join(timeout=3.0)
            if reader.is_alive():
                raise RuntimeError("Mosquitto log reader did not stop")
        self._reader = None
        self._process = None

    def _read_logs(self) -> None:
        process = self._process
        assert process is not None and process.stdout is not None
        try:
            for line in process.stdout:
                with self._condition:
                    self._logs.append(line)
                    self._condition.notify_all()
        finally:
            with self._condition:
                self._condition.notify_all()


@dataclass(frozen=True)
class ObservedMessage:
    topic: str
    payload: str


def _parse_subscriber_line(line: str) -> ObservedMessage:
    topic, separator, payload = line.rstrip("\r\n").partition(" ")
    if not separator or topic not in UPSTREAM_TOPICS:
        raise ValueError(f"unexpected mosquitto_sub output: {line!r}")
    return ObservedMessage(topic=topic, payload=payload)


class MosquittoSubscriber:
    """A real mosquitto_sub observer that records topic/payload lines."""

    def __init__(self, executable: Path) -> None:
        self.executable = executable
        self._process: subprocess.Popen[str] | None = None
        self._reader: threading.Thread | None = None
        self._messages: list[ObservedMessage] = []
        self._output: list[str] = []
        self._condition = threading.Condition()

    def start(self) -> None:
        if self._process is not None:
            raise RuntimeError("mosquitto_sub is already started")
        try:
            self._process = subprocess.Popen(
                _subscriber_command(self.executable),
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                bufsize=1,
            )
        except OSError as error:
            self._process = None
            raise RuntimeError(f"could not start mosquitto_sub: {error}") from error
        self._reader = threading.Thread(
            target=self._read_messages, name="mosquitto-sub", daemon=True
        )
        self._reader.start()

    def wait_for_messages(
        self, count: int, timeout: float
    ) -> list[ObservedMessage]:
        deadline = time.monotonic() + timeout
        with self._condition:
            while len(self._messages) < count:
                if self._process is not None and self._process.poll() is not None:
                    raise RuntimeError(
                        "mosquitto_sub exited before receiving messages: "
                        + "".join(self._output)
                    )
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError(
                        f"received {len(self._messages)} of {count} MQTT messages; "
                        f"output={''.join(self._output)!r}"
                    )
                self._condition.wait(remaining)
            return list(self._messages[:count])

    def stop(self) -> None:
        process = self._process
        reader = self._reader
        if process is not None and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=3.0)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=3.0)
        if process is not None and process.stdout is not None:
            process.stdout.close()
        if reader is not None:
            reader.join(timeout=3.0)
            if reader.is_alive():
                raise RuntimeError("mosquitto_sub output reader did not stop")
        self._reader = None
        self._process = None

    def _read_messages(self) -> None:
        process = self._process
        assert process is not None and process.stdout is not None
        try:
            for line in process.stdout:
                with self._condition:
                    self._output.append(line)
                    with suppress(ValueError):
                        self._messages.append(_parse_subscriber_line(line))
                    self._condition.notify_all()
        finally:
            with self._condition:
                self._condition.notify_all()


def _run_net_setup(script: Path, action: str) -> None:
    try:
        result = subprocess.run(
            [str(script), "--iface", "zeth", action],
            cwd=script.parent,
            check=False,
            capture_output=True,
            text=True,
            timeout=20.0,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise TapFixtureError(f"net-setup.sh {action} could not run: {error}") from error
    if result.returncode != 0:
        raise TapFixtureError(
            f"net-setup.sh {action} failed with {result.returncode}: "
            f"{result.stdout}{result.stderr}"
        )


def _run_ip_query(ip_tool: str, arguments: list[str], description: str) -> str:
    command = [ip_tool, *arguments]
    try:
        result = subprocess.run(
            command,
            check=False,
            capture_output=True,
            text=True,
            timeout=5.0,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise TapFixtureError(f"could not verify {description}: {error}") from error
    if result.returncode != 0:
        raise TapFixtureError(
            f"could not verify {description}; {' '.join(command)} returned "
            f"{result.returncode}: {result.stdout}{result.stderr}"
        )
    return result.stdout


def _verify_tap() -> None:
    ip_tool = shutil.which("ip")
    if ip_tool is None:
        raise TapFixtureError("the 'ip' tool is required to verify TAP setup")
    link = _run_ip_query(ip_tool, ["-o", "link", "show", "dev", "zeth"], "TAP zeth")
    if "zeth" not in link:
        raise TapFixtureError(f"net-setup.sh reported success but TAP zeth is absent: {link!r}")
    addresses = _run_ip_query(
        ip_tool,
        ["-o", "-4", "addr", "show", "dev", "zeth"],
        "IPv4 address on TAP zeth",
    )
    if "inet 192.0.2.2/24" not in addresses:
        raise TapFixtureError(
            "net-setup.sh reported success but zeth does not have 192.0.2.2/24: "
            f"{addresses!r}"
        )


class _SecureGatewayEnvironment:
    def __init__(self, setup_script: Path) -> None:
        self.setup_script = setup_script
        self.tap_start_attempted = False
        self.gateway: ReconnectableSecurityGatewayPeer | None = None

    def start(self) -> ReconnectableSecurityGatewayPeer:
        self.tap_start_attempted = True
        _run_net_setup(self.setup_script, "start")
        _verify_tap()
        self.gateway = ReconnectableSecurityGatewayPeer(
            listen_addr=(GATEWAY_HOST, GATEWAY_PORT),
            upstream_addr=(BROKER_HOST, BROKER_PORT),
        )
        self.gateway.start()
        return self.gateway

    def stop(self) -> list[str]:
        errors: list[str] = []
        if self.gateway is not None:
            try:
                self.gateway.stop()
            except BaseException as error:
                errors.append(f"security gateway stop failed: {error!r}")
            errors.extend(
                f"security gateway worker failed: {error!r}"
                for error in self.gateway.errors
            )
        if self.tap_start_attempted:
            try:
                _run_net_setup(self.setup_script, "stop")
            except BaseException as error:
                errors.append(f"TAP cleanup failed: {error!r}")
        return errors


@pytest.fixture()
def mosquitto_broker(tmp_path: Path) -> Generator[MosquittoBroker, None, None]:
    executable = _require_program("mosquitto")
    config = tmp_path / "mosquitto.conf"
    config.write_text(MOSQUITTO_CONFIG, encoding="utf-8")
    broker = MosquittoBroker(executable, config)
    primary_error: BaseException | None = None
    try:
        try:
            broker.start()
        except BaseException as error:
            pytest.fail(f"Mosquitto Broker setup failed: {error!r}", pytrace=False)
        yield broker
    except BaseException as error:
        primary_error = error
        raise
    finally:
        try:
            broker.stop()
        except BaseException as error:
            if primary_error is None:
                pytest.fail(
                    f"Mosquitto Broker cleanup failed: {error!r}", pytrace=False
                )
            LOGGER.error("Mosquitto Broker cleanup also failed: %r", error)


@pytest.fixture()
def upstream_subscriber(
    mosquitto_broker: MosquittoBroker,
) -> Generator[MosquittoSubscriber, None, None]:
    executable = _require_program("mosquitto_sub")
    subscriber = MosquittoSubscriber(executable)
    log_start = mosquitto_broker.log_count
    primary_error: BaseException | None = None
    try:
        try:
            subscriber.start()
            if not mosquitto_broker.wait_for_log(
                "Sending SUBACK", timeout=5.0, start=log_start
            ):
                raise RuntimeError(
                    "mosquitto_sub did not complete subscription: "
                    + mosquitto_broker.log_text
                )
        except BaseException as error:
            pytest.fail(f"mosquitto_sub setup failed: {error!r}", pytrace=False)
        yield subscriber
    except BaseException as error:
        primary_error = error
        raise
    finally:
        try:
            subscriber.stop()
        except BaseException as error:
            if primary_error is None:
                pytest.fail(f"mosquitto_sub cleanup failed: {error!r}", pytrace=False)
            LOGGER.error("mosquitto_sub cleanup also failed: %r", error)


@pytest.fixture()
def publish_downlink_qos1(
    mosquitto_broker: MosquittoBroker,
) -> Callable[[str], None]:
    del mosquitto_broker
    executable = _require_program("mosquitto_pub")

    def publish(payload: str) -> None:
        try:
            result = subprocess.run(
                _publisher_command(executable, payload),
                check=False,
                capture_output=True,
                text=True,
                timeout=10.0,
            )
        except (OSError, subprocess.TimeoutExpired) as error:
            pytest.fail(f"mosquitto_pub could not run: {error!r}", pytrace=False)
        if result.returncode != 0:
            pytest.fail(
                f"mosquitto_pub failed with {result.returncode}: "
                f"{result.stdout}{result.stderr}",
                pytrace=False,
            )

    return publish


@pytest.fixture()
def secure_gateway(
    mosquitto_broker: MosquittoBroker,
    upstream_subscriber: MosquittoSubscriber,
    publish_downlink_qos1: Callable[[str], None],
) -> Generator[ReconnectableSecurityGatewayPeer, None, None]:
    del mosquitto_broker, upstream_subscriber, publish_downlink_qos1
    net_tools = os.environ.get("NET_TOOLS_BASE")
    if not net_tools:
        pytest.fail("NET_TOOLS_BASE is required for mandatory TAP testing")
    setup_script = Path(net_tools) / "net-setup.sh"
    if not setup_script.is_file() or not os.access(setup_script, os.X_OK):
        pytest.fail(f"required executable does not exist: {setup_script}")

    environment = _SecureGatewayEnvironment(setup_script)
    primary_error: BaseException | None = None
    try:
        try:
            gateway = environment.start()
        except BaseException as error:
            pytest.fail(f"secure gateway setup failed: {error!r}", pytrace=False)
        yield gateway
    except BaseException as error:
        primary_error = error
        raise
    finally:
        cleanup_errors = environment.stop()
        if cleanup_errors:
            details = "; ".join(cleanup_errors)
            if primary_error is None:
                pytest.fail(f"secure gateway cleanup failed: {details}", pytrace=False)
            LOGGER.error("secure gateway cleanup also failed: %s", details)
