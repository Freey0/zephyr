# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import multiprocessing
from pathlib import Path

import conftest
import pytest

EVENT_TIMEOUT_SECONDS = 5.0
CONTENDED_WINDOW_SECONDS = 0.2
JOIN_TIMEOUT_SECONDS = 2.0


def _hold_lock(lock_path: Path, acquired, release) -> None:
    with conftest.tap_gate_lock(lock_path):
        acquired.set()
        release.wait(timeout=EVENT_TIMEOUT_SECONDS)


def _wait_for_lock(lock_path: Path, attempting, acquired) -> None:
    attempting.set()
    with conftest.tap_gate_lock(lock_path):
        acquired.set()


def _join_or_terminate(process) -> None:
    if process.pid is None:
        return
    process.join(timeout=JOIN_TIMEOUT_SECONDS)
    if process.is_alive():
        process.terminate()
        process.join(timeout=JOIN_TIMEOUT_SECONDS)


def test_tap_gate_lock_serializes_independent_processes(tmp_path: Path) -> None:
    context = multiprocessing.get_context("spawn")
    lock_path = tmp_path / "zeth.lock"
    first_acquired = context.Event()
    release_first = context.Event()
    second_attempting = context.Event()
    second_acquired = context.Event()
    first = context.Process(
        target=_hold_lock,
        args=(lock_path, first_acquired, release_first),
    )
    second = context.Process(
        target=_wait_for_lock,
        args=(lock_path, second_attempting, second_acquired),
    )

    first.start()
    try:
        assert first_acquired.wait(timeout=EVENT_TIMEOUT_SECONDS)
        second.start()
        assert second_attempting.wait(timeout=EVENT_TIMEOUT_SECONDS)
        assert not second_acquired.wait(timeout=CONTENDED_WINDOW_SECONDS)

        release_first.set()
        assert second_acquired.wait(timeout=EVENT_TIMEOUT_SECONDS)
    finally:
        release_first.set()
        _join_or_terminate(first)
        _join_or_terminate(second)

    assert first.exitcode == 0
    assert second.exitcode == 0


def test_tap_gate_lock_releases_after_exception(tmp_path: Path) -> None:
    context = multiprocessing.get_context("spawn")
    lock_path = tmp_path / "zeth.lock"
    contender_attempting = context.Event()
    contender_acquired = context.Event()
    contender = context.Process(
        target=_wait_for_lock,
        args=(lock_path, contender_attempting, contender_acquired),
    )

    try:
        with (
            pytest.raises(RuntimeError, match="injected fixture failure"),
            conftest.tap_gate_lock(lock_path),
        ):
            contender.start()
            assert contender_attempting.wait(timeout=EVENT_TIMEOUT_SECONDS)
            assert not contender_acquired.wait(timeout=CONTENDED_WINDOW_SECONDS)
            raise RuntimeError("injected fixture failure")

        assert contender_acquired.wait(timeout=EVENT_TIMEOUT_SECONDS)
    finally:
        _join_or_terminate(contender)

    assert contender.exitcode == 0
