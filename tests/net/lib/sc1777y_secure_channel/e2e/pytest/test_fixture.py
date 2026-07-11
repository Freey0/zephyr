# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
from types import SimpleNamespace

import pytest

import conftest


def _result(returncode: int = 0, stdout: str = "", stderr: str = ""):
    return SimpleNamespace(returncode=returncode, stdout=stdout, stderr=stderr)


def test_tap_validation_rejects_script_false_success(monkeypatch):
    responses = iter(
        (
            _result(stdout="7: zeth: <BROADCAST,UP> mtu 1500"),
            _result(stdout=""),
        )
    )
    monkeypatch.setattr(conftest.shutil, "which", lambda _: "/sbin/ip")
    monkeypatch.setattr(conftest.subprocess, "run", lambda *args, **kwargs: next(responses))

    with pytest.raises(conftest.TapFixtureError, match="does not have 192.0.2.2/24"):
        conftest._verify_tap()


def test_net_setup_exception_is_diagnostic(monkeypatch):
    def raise_timeout(*args, **kwargs):
        raise conftest.subprocess.TimeoutExpired("net-setup.sh", 20.0)

    monkeypatch.setattr(conftest.subprocess, "run", raise_timeout)

    with pytest.raises(conftest.TapFixtureError, match="start could not run"):
        conftest._run_net_setup(Path("/net-tools/net-setup.sh"), "start")


def test_attempted_start_always_stops_tap(monkeypatch):
    actions = []

    def net_setup(script, action):
        actions.append(action)
        if action == "start":
            raise conftest.TapFixtureError("partial start")

    monkeypatch.setattr(conftest, "_run_net_setup", net_setup)
    environment = conftest._SecureGatewayEnvironment(Path("/net-tools/net-setup.sh"))

    with pytest.raises(conftest.TapFixtureError, match="partial start"):
        environment.start()
    assert environment.stop() == []
    assert actions == ["start", "stop"]


def test_cleanup_runs_in_order_and_reports_late_errors(monkeypatch):
    actions = []

    class Gateway:
        errors = []

        def stop(self):
            actions.append("gateway")
            self.errors.append(RuntimeError("late gateway failure"))

    class Echo:
        error = None

        def stop(self):
            actions.append("echo")
            self.error = RuntimeError("late echo failure")

    def net_setup(script, action):
        actions.append(action)

    monkeypatch.setattr(conftest, "_run_net_setup", net_setup)
    environment = conftest._SecureGatewayEnvironment(Path("/net-tools/net-setup.sh"))
    environment.gateway = Gateway()
    environment.echo = Echo()
    environment.tap_start_attempted = True

    errors = environment.stop()

    assert actions == ["gateway", "echo", "stop"]
    assert any("late gateway failure" in error for error in errors)
    assert any("late echo failure" in error for error in errors)


def test_cleanup_continues_after_stop_exception(monkeypatch):
    actions = []

    class Gateway:
        errors = []

        def stop(self):
            actions.append("gateway")
            raise RuntimeError("gateway stop exception")

    class Echo:
        error = None

        def stop(self):
            actions.append("echo")

    def net_setup(script, action):
        actions.append(action)

    monkeypatch.setattr(conftest, "_run_net_setup", net_setup)
    environment = conftest._SecureGatewayEnvironment(Path("/net-tools/net-setup.sh"))
    environment.gateway = Gateway()
    environment.echo = Echo()
    environment.tap_start_attempted = True

    errors = environment.stop()

    assert actions == ["gateway", "echo", "stop"]
    assert any("gateway stop exception" in error for error in errors)
