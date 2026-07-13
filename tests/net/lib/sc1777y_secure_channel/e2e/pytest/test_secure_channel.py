# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

from twister_harness import DeviceAdapter


def test_secure_channel_echo(
    unlaunched_dut: DeviceAdapter, secure_gateway
) -> None:
    unlaunched_dut.launch()
    unlaunched_dut.readlines_until("SECURE_CHANNEL_E2E_PASS", timeout=30.0)
    secure_gateway.raise_if_failed()
    assert secure_gateway.handshake_count == 2
    assert secure_gateway.forwarded_plaintext_bytes >= 6000
    assert secure_gateway.coalesced_write_count >= 1
