# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import fcntl
import os
from collections.abc import Generator
from contextlib import contextmanager
from pathlib import Path

TAP_GATE_LOCK_PATH = Path("/tmp/zephyr-sc1777y-zeth.lock")


@contextmanager
def tap_gate_lock(lock_path: Path = TAP_GATE_LOCK_PATH) -> Generator[None, None, None]:
    """Hold the cross-process lock protecting the shared host TAP interface."""
    flags = os.O_RDONLY | os.O_CREAT | os.O_CLOEXEC
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW
    descriptor = os.open(lock_path, flags, 0o666)
    try:
        fcntl.flock(descriptor, fcntl.LOCK_EX)
        try:
            yield
        finally:
            fcntl.flock(descriptor, fcntl.LOCK_UN)
    finally:
        os.close(descriptor)
