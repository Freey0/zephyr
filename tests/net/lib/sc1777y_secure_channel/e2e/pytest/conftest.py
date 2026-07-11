# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import os
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
        pytest.fail(f"net-setup.sh {action} could not run: {error}")
    if result.returncode != 0:
        pytest.fail(
            f"net-setup.sh {action} failed with {result.returncode}: "
            f"{result.stdout}{result.stderr}"
        )


@pytest.fixture()
def secure_gateway() -> Generator[SecurityGatewayPeer, None, None]:
    net_tools = os.environ.get("NET_TOOLS_BASE")
    if not net_tools:
        pytest.fail("NET_TOOLS_BASE is required for mandatory TAP testing")
    setup_script = Path(net_tools) / "net-setup.sh"
    if not setup_script.is_file() or not os.access(setup_script, os.X_OK):
        pytest.fail(f"required executable does not exist: {setup_script}")

    setup_started = False
    echo = _TcpEchoService()
    gateway: SecurityGatewayPeer | None = None
    try:
        _run_net_setup(setup_script, "start")
        setup_started = True
        echo.start()
        gateway = SecurityGatewayPeer(("192.0.2.2", 18883), echo.address)
        gateway.start()
        yield gateway
        gateway.raise_if_failed()
        if echo.error is not None:
            raise RuntimeError("TCP echo service failed") from echo.error
    finally:
        if gateway is not None:
            gateway.stop()
        echo.stop()
        if setup_started:
            _run_net_setup(setup_script, "stop")
