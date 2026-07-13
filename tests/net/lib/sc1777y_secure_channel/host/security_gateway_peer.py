# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

"""Deterministic SC1777Y security-gateway peer used by native_sim tests."""

from __future__ import annotations

import selectors
import socket
import struct
import threading
from collections.abc import Iterable
from contextlib import suppress

HANDSHAKE_RESPONSE_LEN = 230
HANDSHAKE_CONFIRM_LEN = 184
RECORD_FIXED_LEN = 20
MAX_CIPHERTEXT_LEN = 2048
MAX_PLAINTEXT_CHUNK = 2047
CRYPTO_MASK = 0xA5
WRITE_FRAGMENTS = (1, 3, 17, 64)
FAULT_BAD_HANDSHAKE_RESPONSE = "bad_handshake_response"
FAULT_BAD_RECORD_PADDING = "bad_record_padding"
FAULT_MODES = {FAULT_BAD_HANDSHAKE_RESPONSE, FAULT_BAD_RECORD_PADDING}


def _incrementing(base: int, length: int) -> bytes:
    return bytes((base + offset) & 0xFF for offset in range(length))


def _recv_exact(sock: socket.socket, length: int) -> bytes:
    data = bytearray()
    while len(data) < length:
        chunk = sock.recv(length - len(data))
        if not chunk:
            raise ConnectionError(f"peer closed with {length - len(data)} bytes pending")
        data.extend(chunk)
    return bytes(data)


def _send_fragmented(sock: socket.socket, data: bytes) -> None:
    offset = 0
    fragment = 0
    while offset < len(data):
        length = min(WRITE_FRAGMENTS[fragment % len(WRITE_FRAGMENTS)], len(data) - offset)
        sock.sendall(data[offset : offset + length])
        offset += length
        fragment += 1


def _unpad(data: bytes) -> bytes:
    if not data or len(data) > MAX_CIPHERTEXT_LEN or len(data) % 16:
        raise ValueError("invalid ciphertext length")
    marker = len(data)
    while marker and data[marker - 1] == 0:
        marker -= 1
    if marker == 0 or data[marker - 1] != 0x80 or len(data) - marker >= 16:
        raise ValueError("invalid SC1777Y record padding")
    plaintext = data[: marker - 1]
    if not plaintext:
        raise ValueError("padding-only record is not permitted")
    return plaintext


def _decode_record(record: bytes) -> bytes:
    if len(record) < RECORD_FIXED_LEN or record[:2] != b"\x02\x00":
        raise ValueError("invalid record type")
    if struct.unpack("!H", record[2:4])[0] != len(record):
        raise ValueError("invalid record length")
    ciphertext = bytes(byte ^ CRYPTO_MASK for byte in record[RECORD_FIXED_LEN:])
    return _unpad(ciphertext)


def _encode_records(plaintext: bytes) -> Iterable[bytes]:
    offset = 0
    while offset < len(plaintext):
        chunk = plaintext[offset : offset + MAX_PLAINTEXT_CHUNK]
        padded_length = ((len(chunk) + 1 + 15) // 16) * 16
        padded = chunk + b"\x80" + bytes(padded_length - len(chunk) - 1)
        ciphertext = bytes(byte ^ CRYPTO_MASK for byte in padded)
        iv = _incrementing(0xA0, 16)
        record_length = RECORD_FIXED_LEN + len(ciphertext)
        yield b"\x02\x00" + struct.pack("!H", record_length) + iv + ciphertext
        offset += len(chunk)


class SecurityGatewayPeer:
    def __init__(
        self,
        listen_addr: tuple[str, int],
        upstream_addr: tuple[str, int],
        fault_mode: str | None = None,
    ) -> None:
        if fault_mode is not None and fault_mode not in FAULT_MODES:
            raise ValueError(f"unsupported security fault mode: {fault_mode}")
        self.listen_addr = listen_addr
        self.upstream_addr = upstream_addr
        self.fault_mode = fault_mode
        self.handshake_request_count = 0
        self.handshake_count = 0
        self.forwarded_plaintext_bytes = 0
        self.coalesced_write_count = 0
        self.fault_injected = False
        self.terminal_close_observed = False
        self._terminal_closed = threading.Event()
        self.errors: list[BaseException] = []
        self._listener: socket.socket | None = None
        self._terminal: socket.socket | None = None
        self._upstream: socket.socket | None = None
        self._worker: threading.Thread | None = None
        self._stop = threading.Event()
        self._ready = threading.Event()

    def start(self) -> None:
        """Listen, accept the terminal, negotiate, and proxy until stopped."""
        if self._worker is not None:
            raise RuntimeError("gateway peer is already running")
        self._listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._listener.bind(self.listen_addr)
        self._listener.listen()
        self._listener.settimeout(0.2)
        self._worker = threading.Thread(target=self._run, name="security-gateway", daemon=True)
        self._worker.start()
        if not self._ready.wait(timeout=2.0):
            raise RuntimeError("security gateway worker did not start")

    def stop(self) -> None:
        """Close listener, terminal, upstream, and worker threads."""
        self._stop.set()
        for sock in (self._terminal, self._upstream, self._listener):
            if sock is not None:
                with suppress(OSError):
                    sock.shutdown(socket.SHUT_RDWR)
                with suppress(OSError):
                    sock.close()
        if self._worker is not None:
            self._worker.join(timeout=3.0)
            if self._worker.is_alive():
                self.errors.append(RuntimeError("security gateway worker did not stop"))
        self._terminal = None
        self._upstream = None
        self._listener = None
        self._worker = None

    def raise_if_failed(self) -> None:
        if self.errors:
            raise RuntimeError("security gateway failed") from self.errors[0]

    def _run(self) -> None:
        self._ready.set()
        assert self._listener is not None
        try:
            while not self._stop.is_set():
                try:
                    terminal, _ = self._listener.accept()
                except TimeoutError:
                    continue
                except OSError:
                    if self._stop.is_set():
                        break
                    raise
                self._terminal = terminal
                try:
                    self._serve_connection(terminal)
                except (ConnectionError, OSError):
                    if not self._stop.is_set():
                        raise
                finally:
                    terminal.close()
                    self._terminal = None
                    if self._upstream is not None:
                        self._upstream.close()
                        self._upstream = None
        except BaseException as error:
            if not self._stop.is_set():
                self.errors.append(error)

    def _wait_for_terminal_close(self, terminal: socket.socket) -> None:
        while terminal.recv(4096):
            pass
        self.terminal_close_observed = True
        self._terminal_closed.set()

    def wait_for_terminal_close(self, timeout: float) -> bool:
        """Wait until the terminal closes after an injected security fault."""
        return self._terminal_closed.wait(timeout)

    def _negotiate(self, terminal: socket.socket) -> bool:
        header = _recv_exact(terminal, 4)
        if header[:2] != b"\x01\x01":
            raise ValueError("invalid handshake request type")
        request_length = struct.unpack("!H", header[2:])[0]
        if request_length < 235 or request_length > 2112:
            raise ValueError("invalid handshake request length")
        request = header + _recv_exact(terminal, request_length - 4)
        if request[4:6] != b"\x01\x00":
            raise ValueError("invalid handshake protocol version")
        request_sn = struct.unpack("!H", request[6:8])[0]
        if request_sn != 0xA0A1:
            raise ValueError(f"unexpected deterministic request SN {request_sn:#06x}")
        certificate_length = request_length - 234
        en_r1_start = 42 + certificate_length
        if request[en_r1_start : en_r1_start + 128] != _incrementing(0xC0, 128):
            raise ValueError("request EnR1 does not match SC1777Y emulator")
        if request[-64:] != _incrementing(0xE0, 64):
            raise ValueError("request signature does not match SC1777Y emulator")
        self.handshake_request_count += 1

        auth_factor = _incrementing(0x40, 32)
        en_r2 = _incrementing(0x20, 128)
        signature = _incrementing(0xE0, 64)
        response_subtype = (
            b"\x03" if self.fault_mode == FAULT_BAD_HANDSHAKE_RESPONSE else b"\x02"
        )
        response = (
            b"\x01"
            + response_subtype
            + struct.pack("!H", HANDSHAKE_RESPONSE_LEN)
            + struct.pack("!H", (request_sn + 1) & 0xFFFF)
            + auth_factor
            + en_r2
            + signature
        )
        _send_fragmented(terminal, response)

        if self.fault_mode == FAULT_BAD_HANDSHAKE_RESPONSE:
            self.fault_injected = True
            self._wait_for_terminal_close(terminal)
            return False

        confirm = _recv_exact(terminal, HANDSHAKE_CONFIRM_LEN)
        if confirm[:2] != b"\x01\x03":
            raise ValueError("invalid handshake confirm type")
        if struct.unpack("!H", confirm[2:4])[0] != HANDSHAKE_CONFIRM_LEN:
            raise ValueError("invalid handshake confirm length")
        if struct.unpack("!H", confirm[4:6])[0] != ((request_sn + 2) & 0xFFFF):
            raise ValueError("invalid handshake confirm SN")
        if confirm[6:152] != _incrementing(0x70, 146):
            raise ValueError("confirm authentication result does not match emulator")
        if confirm[152:] != _incrementing(0x90, 32):
            raise ValueError("confirm DKHash does not match emulator")
        self.handshake_count += 1
        return True

    def _forward_upstream_plaintext(
        self, terminal: socket.socket, plaintext: bytes
    ) -> None:
        if len(plaintext) >= 2:
            split = len(plaintext) // 2
            records = [
                *_encode_records(plaintext[:split]),
                *_encode_records(plaintext[split:]),
            ]
            terminal.sendall(b"".join(records))
            self.coalesced_write_count += 1
            return

        for record in _encode_records(plaintext):
            _send_fragmented(terminal, record)

    def _inject_bad_padding(self, terminal: socket.socket) -> None:
        record_length = RECORD_FIXED_LEN + 16
        record = (
            b"\x02\x00"
            + struct.pack("!H", record_length)
            + _incrementing(0xA0, 16)
            + bytes([CRYPTO_MASK]) * 16
        )
        terminal.sendall(record)
        self.fault_injected = True
        self._wait_for_terminal_close(terminal)

    def _serve_connection(self, terminal: socket.socket) -> None:
        terminal.settimeout(5.0)
        if not self._negotiate(terminal):
            return
        if self.fault_mode == FAULT_BAD_RECORD_PADDING:
            self._inject_bad_padding(terminal)
            return
        terminal.settimeout(None)
        upstream = socket.create_connection(self.upstream_addr, timeout=5.0)
        upstream.settimeout(None)
        self._upstream = upstream
        selector = selectors.DefaultSelector()
        selector.register(terminal, selectors.EVENT_READ, "terminal")
        selector.register(upstream, selectors.EVENT_READ, "upstream")
        terminal_buffer = bytearray()
        try:
            while not self._stop.is_set():
                for key, _ in selector.select(timeout=0.2):
                    data = key.fileobj.recv(4094)
                    if not data:
                        return
                    if key.data == "terminal":
                        terminal_buffer.extend(data)
                        self._forward_terminal_records(terminal_buffer, upstream)
                    else:
                        self._forward_upstream_plaintext(terminal, data)
        finally:
            selector.close()

    def _forward_terminal_records(
        self, buffer: bytearray, upstream: socket.socket
    ) -> None:
        while len(buffer) >= 4:
            if buffer[:2] != b"\x02\x00":
                raise ValueError("invalid terminal record type")
            record_length = struct.unpack("!H", buffer[2:4])[0]
            if (
                record_length < RECORD_FIXED_LEN + 16
                or record_length > RECORD_FIXED_LEN + 2048
            ):
                raise ValueError("invalid terminal record length")
            if len(buffer) < record_length:
                return
            record = bytes(buffer[:record_length])
            del buffer[:record_length]
            plaintext = _decode_record(record)
            upstream.sendall(plaintext)
            self.forwarded_plaintext_bytes += len(plaintext)
