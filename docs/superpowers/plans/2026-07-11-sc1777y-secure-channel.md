# SC1777Y Secure Channel Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a synchronous Zephyr secure-channel library that owns one TCP connection to the security gateway, completes the SC1777Y-backed handshake, and exposes encrypted bidirectional byte-stream APIs without depending on MQTT.

**Architecture:** `sc1777y_secure_channel` is a network library layered directly on the existing public `sc1777y.h` driver API. It encodes the gateway protocol, performs the three-message handshake, fragments and reassembles encrypted records, and owns the socket; native_sim uses the existing SPI emulator transparently behind the driver. Unit/component tests exercise public and internal library behavior, while a mandatory TAP end-to-end test connects the native_sim application through `SecurityGatewayPeer` to a host TCP echo service.

**Tech Stack:** Zephyr C, ztest, Zephyr sockets, SC1777Y public driver API, native_sim, SPI emulator through Devicetree, Twister pytest harness, Python standard library, Zephyr net-tools TAP.

## Global Constraints

- The source of truth is `docs/superpowers/specs/2026-07-11-sc1777y-secure-mqtt-transport-design.md`.
- This plan implements only the secure communication library; it must not include MQTT headers or symbols.
- The library opens one TCP socket to one security-gateway address and performs the handshake on that same socket.
- Production code calls `include/zephyr/drivers/misc/sc1777y.h` APIs directly; it must not add a crypto vtable or software SM implementation.
- New tests must not include `sc1777y_emul.h`, call `sc1777y_emul_*()`, inspect CLA/INS, or assert SPI bytes.
- The existing SPI emulator is enabled only as the native_sim chip substitute selected through Devicetree.
- No heap allocation, background thread, internal reconnect loop, second socket, or connection pool is allowed in the library.
- Maximum certificate length is 1878 bytes; maximum handshake frame is 2112 bytes; maximum plaintext record chunk is 2047 bytes; maximum ciphertext is 2048 bytes; maximum secure record is 2068 bytes.
- `sc1777y_secure_channel_send()` returns 0 only after all input bytes are framed and written; `recv()` returns plaintext byte count, `-EAGAIN`, another negative errno, or 0 for peer close.
- A protocol, authentication, padding, SC1777Y, timeout, or partial-write failure closes the socket and moves the channel to `FAILED`.
- The phase-one TAP end-to-end test is mandatory on every phase-one test run and requires `CAP_NET_ADMIN` or root plus Zephyr net-tools.

---

## File Map

### Production files

- Create `include/zephyr/net/sc1777y_secure_channel.h`: public constants, configuration, context, state, and lifecycle/send/receive APIs.
- Create `subsys/net/lib/sc1777y_secure_channel/Kconfig`: `CONFIG_SC1777Y_SECURE_CHANNEL` and log level.
- Create `subsys/net/lib/sc1777y_secure_channel/CMakeLists.txt`: compile the focused implementation files.
- Create `subsys/net/lib/sc1777y_secure_channel/secure_channel_internal.h`: private wire constants, parsed message structs, and cross-file helpers.
- Create `subsys/net/lib/sc1777y_secure_channel/secure_channel.c`: initialization, state, locks, failure/close handling.
- Create `subsys/net/lib/sc1777y_secure_channel/protocol.c`: handshake and record encoding/decoding plus padding.
- Create `subsys/net/lib/sc1777y_secure_channel/socket_io.c`: socket creation, binding, timeouts, send-all, incremental receive, and close.
- Create `subsys/net/lib/sc1777y_secure_channel/handshake.c`: direct SC1777Y calls and three-message negotiation.
- Create `subsys/net/lib/sc1777y_secure_channel/record.c`: chunking, IV import, encrypt/decrypt, RX record state, plaintext cache.
- Modify `subsys/net/lib/CMakeLists.txt`: add the library under its Kconfig symbol.
- Modify `subsys/net/lib/Kconfig`: source the new Kconfig.

### Unit/component tests

- Create `tests/net/lib/sc1777y_secure_channel/unit/CMakeLists.txt`.
- Create `tests/net/lib/sc1777y_secure_channel/unit/prj.conf`.
- Create `tests/net/lib/sc1777y_secure_channel/unit/testcase.yaml`.
- Create `tests/net/lib/sc1777y_secure_channel/unit/boards/native_sim.overlay`: mount SC1777Y under `spi0` without an emulator alias.
- Create `tests/net/lib/sc1777y_secure_channel/unit/src/test_protocol.c`.
- Create `tests/net/lib/sc1777y_secure_channel/unit/src/test_channel.c`.
- Create `tests/net/lib/sc1777y_secure_channel/unit/src/test_record.c`.

### Phase-one end-to-end test

- Create `tests/net/lib/sc1777y_secure_channel/e2e/CMakeLists.txt`.
- Create `tests/net/lib/sc1777y_secure_channel/e2e/prj.conf`.
- Create `tests/net/lib/sc1777y_secure_channel/e2e/testcase.yaml`.
- Create `tests/net/lib/sc1777y_secure_channel/e2e/boards/native_sim.overlay`.
- Create `tests/net/lib/sc1777y_secure_channel/e2e/src/main.c`.
- Create `tests/net/lib/sc1777y_secure_channel/host/security_gateway_peer.py`: reusable deterministic gateway proxy.
- Create `tests/net/lib/sc1777y_secure_channel/e2e/pytest/conftest.py`: TAP, echo service, and gateway fixtures.
- Create `tests/net/lib/sc1777y_secure_channel/e2e/pytest/test_secure_channel.py`.
- Create `tests/net/lib/sc1777y_secure_channel/README.rst`: mandatory test command and prerequisites.

---

### Task 1: Register the library and lock the public API

**Files:**
- Create: `include/zephyr/net/sc1777y_secure_channel.h`
- Create: `subsys/net/lib/sc1777y_secure_channel/Kconfig`
- Create: `subsys/net/lib/sc1777y_secure_channel/CMakeLists.txt`
- Create: `subsys/net/lib/sc1777y_secure_channel/secure_channel.c`
- Modify: `subsys/net/lib/CMakeLists.txt`
- Modify: `subsys/net/lib/Kconfig`
- Create: `tests/net/lib/sc1777y_secure_channel/unit/CMakeLists.txt`
- Create: `tests/net/lib/sc1777y_secure_channel/unit/prj.conf`
- Create: `tests/net/lib/sc1777y_secure_channel/unit/testcase.yaml`
- Create: `tests/net/lib/sc1777y_secure_channel/unit/boards/native_sim.overlay`
- Create: `tests/net/lib/sc1777y_secure_channel/unit/src/test_channel.c`

**Interfaces:**
- Consumes: `enum sc1777y_platform_type` and `struct device` from `zephyr/drivers/misc/sc1777y.h`.
- Produces: `struct sc1777y_secure_channel_config`, `struct sc1777y_secure_channel`, `enum sc1777y_secure_channel_state`, `sc1777y_secure_channel_init()`, `connect()`, `send()`, `recv()`, `close()`, and `get_state()`.

- [ ] **Step 1: Add the failing public-API validation test**

```c
#include <zephyr/net/sc1777y_secure_channel.h>
#include <zephyr/ztest.h>

ZTEST(sc1777y_secure_channel, test_init_rejects_null_arguments)
{
	struct sc1777y_secure_channel channel;
	struct sc1777y_secure_channel_config config = {0};

	zassert_equal(-EINVAL, sc1777y_secure_channel_init(NULL, &config));
	zassert_equal(-EINVAL, sc1777y_secure_channel_init(&channel, NULL));
}

ZTEST_SUITE(sc1777y_secure_channel, NULL, NULL, NULL, NULL, NULL);
```

- [ ] **Step 2: Add the unit-test build files and verify RED**

`prj.conf` must enable `ZTEST`, `EMUL`, `SPI`, `SPI_EMUL`, `SC1777Y`, `SC1777Y_EMUL`, `NETWORKING`, `NET_SOCKETS`, `NET_IPV4`, `NET_LOOPBACK`, and `SC1777Y_SECURE_CHANNEL`.

Run:

```bash
west twister -T tests/net/lib/sc1777y_secure_channel/unit -p native_sim \
  --inline-logs --outdir build/twister_secure_channel_api
```

Expected: FAIL because `zephyr/net/sc1777y_secure_channel.h` and the Kconfig symbol do not exist.

- [ ] **Step 3: Define the exact public constants, configuration, and context**

The header must declare these exact public limits and signatures:

```c
#define SC1777Y_SECURE_SIM_LEN 16U
#define SC1777Y_SECURE_DEVICE_ID_LEN 18U
#define SC1777Y_SECURE_MAX_CERTIFICATE_LEN 1878U
#define SC1777Y_SECURE_MAX_HANDSHAKE_LEN 2112U
#define SC1777Y_SECURE_MAX_PLAINTEXT_CHUNK 2047U
#define SC1777Y_SECURE_MAX_CIPHERTEXT_LEN 2048U
#define SC1777Y_SECURE_MAX_RECORD_LEN 2068U

enum sc1777y_secure_channel_state {
	SC1777Y_SECURE_CHANNEL_DISCONNECTED,
	SC1777Y_SECURE_CHANNEL_TCP_CONNECTED,
	SC1777Y_SECURE_CHANNEL_NEGOTIATING,
	SC1777Y_SECURE_CHANNEL_ESTABLISHED,
	SC1777Y_SECURE_CHANNEL_FAILED,
	SC1777Y_SECURE_CHANNEL_CLOSED,
};

struct sc1777y_secure_channel_config {
	const struct device *sc1777y;
	const struct sockaddr *gateway;
	socklen_t gateway_len;
	const char *if_name;
	int32_t connect_timeout_ms;
	int32_t io_timeout_ms;
	const uint8_t *certificate;
	size_t certificate_len;
	const uint8_t *platform_public_key;
	uint8_t sim[SC1777Y_SECURE_SIM_LEN];
	uint8_t device_id[SC1777Y_SECURE_DEVICE_ID_LEN];
	enum sc1777y_platform_type platform_type;
};

struct sc1777y_secure_channel {
	struct sc1777y_secure_channel_config config;
	struct sockaddr_storage gateway_storage;
	int socket_fd;
	enum sc1777y_secure_channel_state state;
	struct k_mutex tx_lock;
	struct k_mutex rx_lock;
	struct k_mutex crypto_lock;
	uint8_t tx_work[SC1777Y_SECURE_MAX_HANDSHAKE_LEN];
	uint8_t rx_record[SC1777Y_SECURE_MAX_RECORD_LEN];
	uint8_t plain_cache[SC1777Y_SECURE_MAX_PLAINTEXT_CHUNK];
	size_t header_used;
	size_t record_expected;
	size_t record_used;
	size_t plain_offset;
	size_t plain_len;
};

int sc1777y_secure_channel_init(struct sc1777y_secure_channel *channel,
				const struct sc1777y_secure_channel_config *config);
int sc1777y_secure_channel_connect(struct sc1777y_secure_channel *channel);
int sc1777y_secure_channel_send(struct sc1777y_secure_channel *channel,
				const uint8_t *data, size_t len);
int sc1777y_secure_channel_recv(struct sc1777y_secure_channel *channel,
				uint8_t *data, size_t size, bool shall_block);
int sc1777y_secure_channel_close(struct sc1777y_secure_channel *channel);
enum sc1777y_secure_channel_state
sc1777y_secure_channel_get_state(const struct sc1777y_secure_channel *channel);
```

After copying the gateway to `gateway_storage`, initialization points the copied
`config.gateway` at that storage. The context must not own or copy the certificate or platform key;
the caller keeps both buffers valid until close.

- [ ] **Step 4: Implement initialization and state access minimally**

`sc1777y_secure_channel_init()` must reject null pointers, a non-ready device, unsupported gateway families, invalid address lengths, zero/negative timeouts, null certificate/public key, certificate lengths outside 1..1878, and platform types other than NANRUI/WANGAN. On success it copies the gateway, initializes locks, sets socket fd to `-1`, clears all lengths, and enters `DISCONNECTED`.

- [ ] **Step 5: Run the API tests and verify GREEN**

Run the Task 1 Twister command.

Expected: PASS; no test includes `sc1777y_emul.h`.

- [ ] **Step 6: Commit the library skeleton**

```bash
git add include/zephyr/net/sc1777y_secure_channel.h \
  subsys/net/lib/sc1777y_secure_channel subsys/net/lib/CMakeLists.txt \
  subsys/net/lib/Kconfig tests/net/lib/sc1777y_secure_channel/unit
git commit -m "net: add sc1777y secure channel skeleton"
```

---

### Task 2: Implement wire codecs and padding with golden tests

**Files:**
- Create: `subsys/net/lib/sc1777y_secure_channel/secure_channel_internal.h`
- Create: `subsys/net/lib/sc1777y_secure_channel/protocol.c`
- Modify: `subsys/net/lib/sc1777y_secure_channel/CMakeLists.txt`
- Create: `tests/net/lib/sc1777y_secure_channel/unit/src/test_protocol.c`
- Modify: `tests/net/lib/sc1777y_secure_channel/unit/CMakeLists.txt`

**Interfaces:**
- Consumes: public length constants and config fields from Task 1.
- Produces: `sc1777y_secure_encode_request_body()`, `append_signature()`, `decode_response()`, `encode_confirm()`, `encode_record()`, `decode_record_header()`, `pad()`, and `unpad()` as private helpers.

- [ ] **Step 1: Write failing padding and frame tests**

Use these exact assertions:

```c
ZTEST(sc1777y_secure_protocol, test_pad_full_block_adds_another_block)
{
	uint8_t buf[32] = {0};
	size_t padded_len = 0;

	zassert_ok(sc1777y_secure_pad(buf, 16, sizeof(buf), &padded_len));
	zassert_equal(32, padded_len);
	zassert_equal(0x80, buf[16]);
	zassert_mem_equal((uint8_t[15]){0}, &buf[17], 15);
}

ZTEST(sc1777y_secure_protocol, test_decode_response_rejects_wrong_sn)
{
	uint8_t response[230] = {0x01, 0x02, 0x00, 0xE6, 0x12, 0x35};
	struct sc1777y_secure_handshake_response parsed;

	zassert_equal(-EPROTO,
		sc1777y_secure_decode_response(response, sizeof(response), 0x1233, &parsed));
}
```

Also add golden tests for request length `234+n`, response length 230, confirm length 184, record length `20+n`, network byte order, padding lengths 1/15/16/17/2047, malformed zero padding, invalid record type/subtype, and ciphertext lengths not divisible by 16.

- [ ] **Step 2: Run protocol tests and verify RED**

Run:

```bash
west twister -T tests/net/lib/sc1777y_secure_channel/unit -p native_sim \
  --inline-logs --outdir build/twister_secure_channel_protocol
```

Expected: FAIL at link time because the private codec helpers are undefined.

- [ ] **Step 3: Define the private parsed-response structure and helper signatures**

```c
struct sc1777y_secure_handshake_response {
	uint16_t sn;
	uint8_t auth_factor[SC1777Y_AUTH_FACTOR_LEN];
	uint8_t en_r2[SC1777Y_SESSION_RANDOM_LEN];
	uint8_t signature[SC1777Y_SIGNATURE_LEN];
};

int sc1777y_secure_pad(uint8_t *buf, size_t plain_len, size_t capacity,
			 size_t *padded_len);
int sc1777y_secure_unpad(uint8_t *buf, size_t padded_len, size_t *plain_len);
```

Define these remaining helper signatures in the same header; none may allocate memory:

```c
int sc1777y_secure_encode_request_body(
	const struct sc1777y_secure_channel_config *config, uint16_t sn,
	const uint8_t en_r1[SC1777Y_SESSION_RANDOM_LEN],
	uint8_t *out, size_t out_size, size_t *out_len);
int sc1777y_secure_append_signature(
	uint8_t *request, size_t request_size, size_t body_len,
	const uint8_t signature[SC1777Y_SIGNATURE_LEN], size_t *request_len);
int sc1777y_secure_decode_response(
	const uint8_t *frame, size_t frame_len, uint16_t request_sn,
	struct sc1777y_secure_handshake_response *response);
int sc1777y_secure_encode_confirm(
	uint16_t request_sn,
	const uint8_t auth_result[SC1777Y_AUTH_RESPONSE_LEN],
	const uint8_t dk_hash[SC1777Y_SESSION_DKHASH_LEN],
	uint8_t *out, size_t out_size, size_t *out_len);
int sc1777y_secure_encode_record(
	const uint8_t iv[SC1777Y_IV_LEN], const uint8_t *ciphertext,
	size_t ciphertext_len, uint8_t *out, size_t out_size, size_t *out_len);
int sc1777y_secure_decode_record_header(
	const uint8_t header[4], size_t *record_len, size_t *ciphertext_len);
```

- [ ] **Step 4: Implement codecs with explicit byte writes**

Use `sys_put_be16()`/`sys_get_be16()` for every wire length and SN. Encode the final request length into the request body before hashing. Reject truncation instead of clipping. `unpad()` scans trailing zero bytes backward and requires the first nonzero byte to be `0x80`.

- [ ] **Step 5: Run protocol tests and verify GREEN**

Run the Task 2 Twister command.

Expected: PASS for all golden, boundary, and malformed-frame cases.

- [ ] **Step 6: Commit protocol codecs**

```bash
git add subsys/net/lib/sc1777y_secure_channel tests/net/lib/sc1777y_secure_channel/unit
git commit -m "net: add sc1777y secure protocol codecs"
```

---

### Task 3: Implement socket ownership and three-message handshake

**Files:**
- Create: `subsys/net/lib/sc1777y_secure_channel/socket_io.c`
- Create: `subsys/net/lib/sc1777y_secure_channel/handshake.c`
- Modify: `subsys/net/lib/sc1777y_secure_channel/secure_channel_internal.h`
- Modify: `subsys/net/lib/sc1777y_secure_channel/secure_channel.c`
- Modify: `subsys/net/lib/sc1777y_secure_channel/CMakeLists.txt`
- Modify: `tests/net/lib/sc1777y_secure_channel/unit/src/test_channel.c`
- Create: `tests/net/lib/sc1777y_secure_channel/unit/src/test_gateway.c`
- Create: `tests/net/lib/sc1777y_secure_channel/unit/src/test_gateway.h`
- Modify: `tests/net/lib/sc1777y_secure_channel/unit/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1 channel/config API, Task 2 codecs, public SC1777Y calls.
- Produces: a working `sc1777y_secure_channel_connect()` that returns only after confirm is fully sent.

- [ ] **Step 1: Add a loopback gateway test that exercises the public connect API**

The in-process test gateway must listen on `127.0.0.1`, accept one connection, read the request length from bytes 2..3, send a deterministic 230-byte response in fragments of 1, 3, 17, and remaining bytes, then read the 184-byte confirm.

```c
ZTEST(sc1777y_secure_channel, test_connect_completes_handshake_before_return)
{
	struct test_gateway gateway;
	struct sc1777y_secure_channel channel;
	struct sc1777y_secure_channel_config config;

	test_gateway_start(&gateway, TEST_GATEWAY_SUCCESS);
	test_channel_config(&config, test_gateway_address(&gateway));
	zassert_ok(sc1777y_secure_channel_init(&channel, &config));
	zassert_ok(sc1777y_secure_channel_connect(&channel));
	zassert_equal(SC1777Y_SECURE_CHANNEL_ESTABLISHED,
		      sc1777y_secure_channel_get_state(&channel));
	zassert_true(test_gateway_saw_confirm(&gateway));
	zassert_ok(sc1777y_secure_channel_close(&channel));
}
```

The fixture gets the chip only with `DEVICE_DT_GET(DT_ALIAS(sc1777y_0))`; it has no emulator pointer.

- [ ] **Step 2: Run the handshake test and verify RED**

Run the unit Twister command with outdir `build/twister_secure_channel_handshake`.

Expected: FAIL because connect still returns `-ENOSYS` or the handshake helpers do not exist.

- [ ] **Step 3: Implement socket primitives**

Implement private functions:

```c
int sc1777y_secure_socket_connect(struct sc1777y_secure_channel *channel);
int sc1777y_secure_socket_send_all(struct sc1777y_secure_channel *channel,
				   const uint8_t *data, size_t len);
int sc1777y_secure_socket_recv_exact(struct sc1777y_secure_channel *channel,
				     uint8_t *data, size_t len);
void sc1777y_secure_socket_close(struct sc1777y_secure_channel *channel);
```

Set `SO_SNDTIMEO` and `SO_RCVTIMEO`, apply `SO_BINDTODEVICE` when `if_name` is non-null, handle EINTR, treat send returning 0 as `-ECONNRESET`, and close after any partial-write failure. Enforce `connect_timeout_ms` by using a nonblocking connect, polling `POLLOUT`, checking `SO_ERROR`, and restoring blocking mode before the handshake.

- [ ] **Step 4: Implement the handshake with direct driver calls**

The call order must be exactly:

```c
sc1777y_set_platform_type(dev, config->platform_type);
sc1777y_import_platform_public_key(dev, config->platform_public_key);
sc1777y_session_begin(dev, en_r1);
sc1777y_get_random(dev, sn_bytes, sizeof(sn_bytes));
sc1777y_hash(dev, SC1777Y_HASH_REQUEST, request_body, request_body_len, request_hash);
sc1777y_sign_hash(dev, request_hash, request_signature);
sc1777y_hash(dev, SC1777Y_HASH_RESPONSE, response_body, response_body_len, response_hash);
sc1777y_verify_signature(dev, response_hash, response.signature);
sc1777y_generate_auth_response(dev, response.auth_factor, auth_result);
sc1777y_session_confirm(dev, response.en_r2, dk_hash);
```

Use the request SN bytes as a big-endian 16-bit value. Enter `NEGOTIATING` before the first chip call and `ESTABLISHED` only after all 184 confirm bytes are written.

- [ ] **Step 5: Add malformed response and connection-failure tests**

Use gateway modes for wrong subtype, wrong length, wrong SN, short response, and peer close. Assert `-EPROTO` or `-ECONNRESET`, state `FAILED`, and socket fd `-1`. Do not inject chip faults through the emulator; existing driver tests own chip-error injection.

- [ ] **Step 6: Run all unit tests and verify GREEN**

Expected: success handshake passes through the real driver and transparent emulator; gateway fault cases close the connection.

- [ ] **Step 7: Commit socket and handshake support**

```bash
git add subsys/net/lib/sc1777y_secure_channel tests/net/lib/sc1777y_secure_channel/unit
git commit -m "net: add sc1777y secure handshake"
```

---

### Task 4: Implement encrypted send and incremental receive

**Files:**
- Create: `subsys/net/lib/sc1777y_secure_channel/record.c`
- Modify: `subsys/net/lib/sc1777y_secure_channel/secure_channel.c`
- Modify: `subsys/net/lib/sc1777y_secure_channel/secure_channel_internal.h`
- Modify: `subsys/net/lib/sc1777y_secure_channel/CMakeLists.txt`
- Create: `tests/net/lib/sc1777y_secure_channel/unit/src/test_record.c`
- Modify: `tests/net/lib/sc1777y_secure_channel/unit/src/test_gateway.c`
- Modify: `tests/net/lib/sc1777y_secure_channel/unit/CMakeLists.txt`

**Interfaces:**
- Consumes: established channel, socket send/receive helpers, protocol/padding helpers, `sc1777y_get_random()`, `import_iv()`, `session_encrypt()`, and `session_decrypt()`.
- Produces: complete public `send()` and `recv()` byte-stream behavior.

- [ ] **Step 1: Write failing public send/receive tests**

Add table-driven cases for 1, 15, 16, 17, 2047, and 3000 bytes. For 3000 bytes, assert the gateway observes two records and returns the original bytes. Add a small-buffer read test:

```c
ZTEST(sc1777y_secure_record, test_recv_caches_plaintext_remainder)
{
	uint8_t first[3];
	uint8_t second[5];

	test_channel_connect_success(&channel, &gateway);
	test_gateway_queue_plaintext(&gateway, (uint8_t *)"12345678", 8);
	zassert_equal(3, sc1777y_secure_channel_recv(&channel, first, sizeof(first), true));
	zassert_equal(5, sc1777y_secure_channel_recv(&channel, second, sizeof(second), false));
	zassert_mem_equal("123", first, 3);
	zassert_mem_equal("45678", second, 5);
}
```

- [ ] **Step 2: Run record tests and verify RED**

Run the unit Twister command with outdir `build/twister_secure_channel_record`.

Expected: FAIL because send/recv return `-ENOSYS`.

- [ ] **Step 3: Implement record send**

For each chunk, hold the TX lock, pad into the TX work buffer, obtain a 16-byte random IV, then hold the crypto lock across `sc1777y_import_iv()` and `sc1777y_session_encrypt()`. Encode one Type=2/Subtype=0 frame and call send-all. Zero the IV and padded plaintext before releasing the TX lock.

- [ ] **Step 4: Implement the nonblocking RX state machine**

Maintain `header_used`, `record_expected`, `record_used`, `plain_offset`, and `plain_len` in the channel. With `shall_block=false`, pass `ZSOCK_MSG_DONTWAIT` and return `-EAGAIN` until a complete record is available. Hold the crypto lock across IV import and decrypt. Validate padding before exposing bytes; retain unread plaintext for later calls.

- [ ] **Step 5: Add malformed padding, fragmented input, coalesced input, and peer-close tests**

Gateway modes must send one byte per TCP write, two records in one TCP write, an all-zero padded block, a ciphertext length of 15, and close halfway through a record. Assert `-EBADMSG`, `-EPROTO`, or 0 as defined by the public contract, plus state transition to `FAILED`/`CLOSED`.

- [ ] **Step 6: Run unit tests and verify GREEN**

Expected: all byte-stream, cache, fragmentation, and error cases pass without any direct emulator control.

- [ ] **Step 7: Commit record transport**

```bash
git add subsys/net/lib/sc1777y_secure_channel tests/net/lib/sc1777y_secure_channel/unit
git commit -m "net: add sc1777y encrypted record stream"
```

---

### Task 5: Make failure, close, and reconnect semantics deterministic

**Files:**
- Modify: `subsys/net/lib/sc1777y_secure_channel/secure_channel.c`
- Modify: `subsys/net/lib/sc1777y_secure_channel/socket_io.c`
- Modify: `subsys/net/lib/sc1777y_secure_channel/handshake.c`
- Modify: `subsys/net/lib/sc1777y_secure_channel/record.c`
- Modify: `tests/net/lib/sc1777y_secure_channel/unit/src/test_channel.c`
- Modify: `tests/net/lib/sc1777y_secure_channel/unit/src/test_record.c`

**Interfaces:**
- Consumes: all Task 1-4 state and I/O paths.
- Produces: one common failure path and safe reuse through a fresh `connect()` after `close()`.

- [ ] **Step 1: Write failing state-transition tests**

Test `send()` before connect (`-ENOTCONN`), double connect (`-EALREADY`), close before connect (0), close twice (0), reconnect after clean close (success), and reconnect after a failed connection only after explicit close.

- [ ] **Step 2: Run tests and verify RED**

Expected: at least double-close, reconnect, or buffer reset assertions fail.

- [ ] **Step 3: Centralize failure cleanup**

Add one private helper:

```c
int sc1777y_secure_channel_fail(struct sc1777y_secure_channel *channel, int error);
```

It closes the socket, clears record progress and plaintext cache, zeros transient buffers, sets `FAILED`, and returns the original negative errno. Clean close performs the same cleanup and sets `CLOSED`.

- [ ] **Step 4: Make reconnect start from a clean explicit-close state**

Permit `connect()` from `DISCONNECTED` or `CLOSED`; reject it from `FAILED` until `close()` is called. Never retry internally.

- [ ] **Step 5: Run all phase-one unit tests and verify GREEN**

Run:

```bash
west twister -T tests/net/lib/sc1777y_secure_channel/unit -p native_sim \
  --inline-logs --outdir build/twister_secure_channel_unit_complete
```

Expected: PASS.

- [ ] **Step 6: Commit deterministic lifecycle behavior**

```bash
git add subsys/net/lib/sc1777y_secure_channel tests/net/lib/sc1777y_secure_channel/unit
git commit -m "net: harden sc1777y secure channel lifecycle"
```

---

### Task 6: Add the mandatory native_sim TAP end-to-end test

**Files:**
- Create: `tests/net/lib/sc1777y_secure_channel/e2e/CMakeLists.txt`
- Create: `tests/net/lib/sc1777y_secure_channel/e2e/prj.conf`
- Create: `tests/net/lib/sc1777y_secure_channel/e2e/testcase.yaml`
- Create: `tests/net/lib/sc1777y_secure_channel/e2e/boards/native_sim.overlay`
- Create: `tests/net/lib/sc1777y_secure_channel/e2e/src/main.c`
- Create: `tests/net/lib/sc1777y_secure_channel/host/security_gateway_peer.py`
- Create: `tests/net/lib/sc1777y_secure_channel/e2e/pytest/conftest.py`
- Create: `tests/net/lib/sc1777y_secure_channel/e2e/pytest/test_secure_channel.py`

**Interfaces:**
- Consumes: the complete phase-one public API.
- Produces: reusable `SecurityGatewayPeer(listen_addr, upstream_addr)` and a mandatory real-network phase-one acceptance test.

- [ ] **Step 1: Add the failing pytest harness and terminal app expectation**

```python
def test_secure_channel_echo(dut, secure_gateway):
    dut.launch()
    dut.readlines_until("SECURE_CHANNEL_E2E_PASS", timeout=30.0)
    assert secure_gateway.handshake_count == 2
    assert secure_gateway.forwarded_plaintext_bytes >= 6000
```

Configure the terminal as `192.0.2.1/24`, gateway `192.0.2.2`, security port 18883, and bind interface `zeth`.

- [ ] **Step 2: Run Twister and verify RED**

Run as a user with TAP privileges:

```bash
west twister -T tests/net/lib/sc1777y_secure_channel/e2e -p native_sim \
  --inline-logs --outdir build/twister_secure_channel_e2e
```

Expected: FAIL because the host proxy and terminal app do not exist.

- [ ] **Step 3: Implement the deterministic gateway peer**

The Python class must:

```python
class SecurityGatewayPeer:
    def __init__(self, listen_addr: tuple[str, int], upstream_addr: tuple[str, int]):
        self.listen_addr = listen_addr
        self.upstream_addr = upstream_addr
        self.handshake_count = 0
        self.forwarded_plaintext_bytes = 0

    def start(self) -> None:
        """Listen, accept the terminal, negotiate, and proxy until stopped."""

    def stop(self) -> None:
        """Close listener, terminal, upstream, and worker threads."""
```

It validates request Type/Subtype/Len/Ver/SN, returns the deterministic 230-byte response matching the current SC1777Y emulator, validates the 184-byte confirm, XORs record ciphertext with `0xA5`, checks/adds `0x80 00...` padding, and transparently proxies plaintext to the configured upstream TCP service. It must support deliberate fragmentation sizes `[1, 3, 17, 64]` for server-to-terminal writes.

- [ ] **Step 4: Implement TAP and echo fixtures**

`conftest.py` must require `NET_TOOLS_BASE`, run `net-setup.sh start` before launching the DUT, run `net-setup.sh stop` in `finally`, start a loopback TCP echo service on an ephemeral port, start `SecurityGatewayPeer` on `192.0.2.2:18883`, and expose the peer object to pytest. Missing permissions or tools must call `pytest.fail()`, not `pytest.skip()`.

- [ ] **Step 5: Implement the native_sim terminal acceptance flow**

The app must obtain only `DEVICE_DT_GET(DT_ALIAS(sc1777y_0))`, initialize the channel, connect, send/receive payloads of 1, 16, 2047, and 3000 bytes, close, reconnect, repeat one payload, and print `SECURE_CHANNEL_E2E_PASS`. It must not include emulator headers.

- [ ] **Step 6: Run the phase-one end-to-end test and verify GREEN**

Run the Task 6 Twister command.

Expected: PASS; pytest reports two completed handshakes and bidirectional echo over TAP.

- [ ] **Step 7: Commit phase-one end-to-end coverage**

```bash
git add tests/net/lib/sc1777y_secure_channel/e2e \
  tests/net/lib/sc1777y_secure_channel/host
git commit -m "tests: add sc1777y secure channel e2e"
```

---

### Task 7: Document and run the complete phase-one gate

**Files:**
- Create: `tests/net/lib/sc1777y_secure_channel/README.rst`
- Modify: `tests/net/lib/sc1777y_secure_channel/unit/testcase.yaml`
- Modify: `tests/net/lib/sc1777y_secure_channel/e2e/testcase.yaml`

**Interfaces:**
- Consumes: all phase-one tests.
- Produces: one documented mandatory command used before phase two begins.

- [ ] **Step 1: Write the README command and prerequisites**

Document `CAP_NET_ADMIN`/root, `NET_TOOLS_BASE`, TAP address ownership, native_sim, and this exact command:

```bash
west twister -T tests/net/lib/sc1777y_secure_channel -p native_sim \
  --inline-logs --outdir build/twister_secure_channel_phase1
```

State explicitly that the e2e case must not be filtered or skipped.

- [ ] **Step 2: Run format and static checks**

```bash
git diff --check
./scripts/checkpatch.pl --git HEAD~6..HEAD
```

Expected: no whitespace or checkpatch errors in the new C/header/Kconfig files.

- [ ] **Step 3: Run the complete mandatory phase-one gate**

Run the README command.

Expected: unit/component tests PASS and TAP end-to-end PASS.

- [ ] **Step 4: Confirm the emulator boundary**

Run:

```bash
rg -n 'sc1777y_emul|EMUL_DT_GET|struct emul' tests/net/lib/sc1777y_secure_channel
```

Expected: no matches in C test source; Kconfig/overlay may contain `SC1777Y_EMUL` or the emulated Devicetree node only.

- [ ] **Step 5: Commit phase-one documentation**

```bash
git add tests/net/lib/sc1777y_secure_channel/README.rst \
  tests/net/lib/sc1777y_secure_channel/unit/testcase.yaml \
  tests/net/lib/sc1777y_secure_channel/e2e/testcase.yaml
git commit -m "docs: add secure channel test gate"
```

Phase one is complete only after Task 7 passes. Do not begin the MQTT custom-transport plan before this gate is green.
