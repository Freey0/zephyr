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
from collections.abc import Generator
from pathlib import Path

import pytest

HOST_DIR = Path(__file__).resolve().parents[2] / "host"
sys.path.insert(0, str(HOST_DIR))

from security_gateway_peer import SecurityGatewayPeer  # noqa: E402

LOGGER = logging.getLogger(__name__)


class TapFixtureError(RuntimeError):
    """A diagnostic failure while creating, validating, or removing the TAP."""


class _TcpEchoService:
    def __init__(self) -> None:
        self.listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen()
        self.listener.settimeout(0.2)
        self.address = self.listener.getsockname()
        self.stop_event = threading.Event()
        self.error: BaseException | None = None
        self.thread = threading.Thread(target=self._run, name="tcp-echo", daemon=True)

    def start(self) -> None:
        self.thread.start()

    def stop(self) -> None:
        self.stop_event.set()
        self.listener.close()
        self.thread.join(timeout=3.0)
        if self.thread.is_alive() and self.error is None:
            self.error = RuntimeError("TCP echo service did not stop")

    def _run(self) -> None:
        try:
            while not self.stop_event.is_set():
                try:
                    connection, _ = self.listener.accept()
                except socket.timeout:
                    continue
                except OSError:
                    if self.stop_event.is_set():
                        return
                    raise
                with connection:
                    while not self.stop_event.is_set():
                        data = connection.recv(8192)
                        if not data:
                            break
                        connection.sendall(data)
        except BaseException as error:
            if not self.stop_event.is_set():
                self.error = error


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
        self.echo: _TcpEchoService | None = None
        self.gateway: SecurityGatewayPeer | None = None

    def start(self) -> SecurityGatewayPeer:
        self.tap_start_attempted = True
        _run_net_setup(self.setup_script, "start")
        _verify_tap()

        self.echo = _TcpEchoService()
        self.echo.start()
        self.gateway = SecurityGatewayPeer(("192.0.2.2", 18883), self.echo.address)
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

        if self.echo is not None:
            try:
                self.echo.stop()
            except BaseException as error:
                errors.append(f"TCP echo stop failed: {error!r}")
            if self.echo.error is not None:
                errors.append(f"TCP echo worker failed: {self.echo.error!r}")

        if self.tap_start_attempted:
            try:
                _run_net_setup(self.setup_script, "stop")
            except BaseException as error:
                errors.append(f"TAP cleanup failed: {error!r}")

        return errors


@pytest.fixture()
def secure_gateway() -> Generator[SecurityGatewayPeer, None, None]:
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
