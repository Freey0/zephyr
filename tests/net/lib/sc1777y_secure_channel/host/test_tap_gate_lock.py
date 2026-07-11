# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import multiprocessing
from pathlib import Path

import pytest
from tap_gate_lock import tap_gate_lock


def _hold_lock(lock_path: Path, acquired, release) -> None:
    with tap_gate_lock(lock_path):
        acquired.set()
        release.wait(timeout=5.0)


def _wait_for_lock(lock_path: Path, attempting, acquired) -> None:
    attempting.set()
    with tap_gate_lock(lock_path):
        acquired.set()


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
        assert first_acquired.wait(timeout=2.0)
        second.start()
        assert second_attempting.wait(timeout=2.0)
        assert not second_acquired.wait(timeout=0.2)

        release_first.set()
        assert second_acquired.wait(timeout=2.0)
    finally:
        release_first.set()
        first.join(timeout=2.0)
        second.join(timeout=2.0)
        if first.is_alive():
            first.terminate()
            first.join(timeout=2.0)
        if second.is_alive():
            second.terminate()
            second.join(timeout=2.0)

    assert first.exitcode == 0
    assert second.exitcode == 0


def test_tap_gate_lock_releases_after_exception(tmp_path: Path) -> None:
    lock_path = tmp_path / "zeth.lock"

    with (
        pytest.raises(RuntimeError, match="injected fixture failure"),
        tap_gate_lock(lock_path),
    ):
        raise RuntimeError("injected fixture failure")

    with tap_gate_lock(lock_path):
        pass
