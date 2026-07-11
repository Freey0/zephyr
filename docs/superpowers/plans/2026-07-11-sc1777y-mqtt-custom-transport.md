# SC1777Y MQTT Custom Transport Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a thin Zephyr MQTT custom transport that delegates all connection and byte-stream work to the completed SC1777Y secure-channel library, then prove the full path against a real local Mosquitto Broker over native_sim TAP.

**Architecture:** The adapter implements Zephyr's five required `mqtt_client_custom_transport_*` symbols and stores a secure-channel pointer in `client->transport.custom_transport_data`. It does not parse the security protocol, call SC1777Y, own a socket, or perform cryptography. The mandatory phase-two end-to-end test runs Zephyr MQTT through the adapter and phase-one library to `SecurityGatewayPeer`, which transparently proxies decrypted MQTT bytes to a real Mosquitto Broker; `mosquitto_sub` and `mosquitto_pub` observe and drive application traffic.

**Tech Stack:** Zephyr MQTT 3.1.1, `CONFIG_MQTT_LIB_CUSTOM_TRANSPORT`, phase-one `sc1777y_secure_channel` API, native_sim, TAP, Twister pytest harness, Python standard library, Mosquitto Broker, `mosquitto_sub`, `mosquitto_pub`.

## Global Constraints

- Phase one, `docs/superpowers/plans/2026-07-11-sc1777y-secure-channel.md`, must be complete and green before this plan starts.
- The source of truth is `docs/superpowers/specs/2026-07-11-sc1777y-secure-mqtt-transport-design.md`.
- The adapter may call only public `sc1777y_secure_channel_*` functions; it must not include SC1777Y driver or emulator headers.
- The adapter must not parse Type/Subtype/Len, add padding, manage IVs, access a socket fd, reconnect, allocate memory, or start a thread.
- `connect()` delegates the complete TCP-plus-handshake operation; MQTT CONNECT is emitted by Zephyr MQTT only after that function returns 0.
- `write()` and `write_msg()` preserve the exact MQTT byte order; successful calls return 0.
- `read()` returns the secure library's plaintext byte count, `-EAGAIN`, other errno, or 0 unchanged.
- The phase-two end-to-end test always uses a real isolated Mosquitto Broker. No BrokerStub, Paho, or in-process MQTT Broker is allowed.
- Test observation uses `mosquitto_sub`; downlink injection uses `mosquitto_pub` with QoS 1.
- The final default command must run both phase-one and phase-two tests, including both mandatory TAP end-to-end cases.

---

## File Map

### Production adapter

- Create `include/zephyr/net/sc1777y_secure_mqtt_transport.h`: adapter bind helper.
- Modify `subsys/net/lib/sc1777y_secure_channel/Kconfig`: add `CONFIG_SC1777Y_SECURE_CHANNEL_MQTT`.
- Modify `subsys/net/lib/sc1777y_secure_channel/CMakeLists.txt`: conditionally compile `mqtt_transport.c`.
- Create `subsys/net/lib/sc1777y_secure_channel/mqtt_transport.c`: five Zephyr custom transport symbols plus bind helper.

### Adapter tests

- Create `tests/net/lib/sc1777y_secure_mqtt_transport/unit/CMakeLists.txt`.
- Create `tests/net/lib/sc1777y_secure_mqtt_transport/unit/prj.conf`.
- Create `tests/net/lib/sc1777y_secure_mqtt_transport/unit/testcase.yaml`.
- Create `tests/net/lib/sc1777y_secure_mqtt_transport/unit/boards/native_sim.overlay`.
- Create `tests/net/lib/sc1777y_secure_mqtt_transport/unit/src/test_adapter.c`.

### Phase-two end-to-end test

- Create `tests/net/lib/sc1777y_secure_mqtt_transport/e2e/CMakeLists.txt`.
- Create `tests/net/lib/sc1777y_secure_mqtt_transport/e2e/prj.conf`.
- Create `tests/net/lib/sc1777y_secure_mqtt_transport/e2e/testcase.yaml`.
- Create `tests/net/lib/sc1777y_secure_mqtt_transport/e2e/boards/native_sim.overlay`.
- Create `tests/net/lib/sc1777y_secure_mqtt_transport/e2e/src/main.c`.
- Create `tests/net/lib/sc1777y_secure_mqtt_transport/e2e/src/payloads.h`.
- Create `tests/net/lib/sc1777y_secure_mqtt_transport/e2e/pytest/conftest.py`.
- Create `tests/net/lib/sc1777y_secure_mqtt_transport/e2e/pytest/test_mqtt_transport.py`.
- Create `tests/net/lib/sc1777y_secure_mqtt_transport/README.rst`.

---

### Task 1: Add the thin adapter API and required MQTT symbols

**Files:**
- Create: `include/zephyr/net/sc1777y_secure_mqtt_transport.h`
- Modify: `subsys/net/lib/sc1777y_secure_channel/Kconfig`
- Modify: `subsys/net/lib/sc1777y_secure_channel/CMakeLists.txt`
- Create: `subsys/net/lib/sc1777y_secure_channel/mqtt_transport.c`
- Create: `tests/net/lib/sc1777y_secure_mqtt_transport/unit/CMakeLists.txt`
- Create: `tests/net/lib/sc1777y_secure_mqtt_transport/unit/prj.conf`
- Create: `tests/net/lib/sc1777y_secure_mqtt_transport/unit/testcase.yaml`
- Create: `tests/net/lib/sc1777y_secure_mqtt_transport/unit/boards/native_sim.overlay`
- Create: `tests/net/lib/sc1777y_secure_mqtt_transport/unit/src/test_adapter.c`

**Interfaces:**
- Consumes: all six public phase-one APIs and Zephyr `struct mqtt_client`.
- Produces: `sc1777y_secure_mqtt_transport_bind()` and the five global custom transport functions expected by Zephyr MQTT.

- [ ] **Step 1: Write failing bind and disconnected-delegation tests**

```c
#include <zephyr/net/mqtt.h>
#include <zephyr/net/sc1777y_secure_channel.h>
#include <zephyr/net/sc1777y_secure_mqtt_transport.h>
#include <zephyr/ztest.h>

ZTEST(sc1777y_secure_mqtt_transport, test_bind_selects_custom_transport)
{
	struct mqtt_client client;
	struct sc1777y_secure_channel channel;

	mqtt_client_init(&client);
	zassert_ok(sc1777y_secure_mqtt_transport_bind(&client, &channel));
	zassert_equal(MQTT_TRANSPORT_CUSTOM, client.transport.type);
	zassert_equal_ptr(&channel, client.transport.custom_transport_data);
}

ZTEST(sc1777y_secure_mqtt_transport, test_write_propagates_not_connected)
{
	static const uint8_t mqtt_bytes[] = {0xC0, 0x00};

	zassert_equal(-ENOTCONN,
		mqtt_client_custom_transport_write(&client, mqtt_bytes,
					   sizeof(mqtt_bytes)));
}
```

Also assert null client/channel/data validation, zero-length write success, empty iovec handling, and read error propagation from a disconnected real channel.

- [ ] **Step 2: Add build files and verify RED**

`prj.conf` must enable phase-one library, MQTT 3.1.1, `MQTT_LIB_CUSTOM_TRANSPORT`, SC1777Y, SPI emulator through Devicetree, ztest, loopback networking, and no TLS.

Run:

```bash
west twister -T tests/net/lib/sc1777y_secure_mqtt_transport/unit -p native_sim \
  --inline-logs --outdir build/twister_secure_mqtt_adapter
```

Expected: FAIL because the adapter header and symbols do not exist.

- [ ] **Step 3: Define the bind helper**

```c
int sc1777y_secure_mqtt_transport_bind(
	struct mqtt_client *client,
	struct sc1777y_secure_channel *channel);
```

It rejects null pointers, sets `client->broker` to the channel's copied gateway address,
sets `client->transport.type = MQTT_TRANSPORT_CUSTOM`, and stores the channel pointer in
`custom_transport_data`. The gateway therefore has one source of truth. The helper does not
initialize or connect the channel.

- [ ] **Step 4: Implement exact one-line delegation behavior**

```c
int mqtt_client_custom_transport_connect(struct mqtt_client *client)
{
	struct sc1777y_secure_channel *channel = channel_from_client(client);

	return channel == NULL ? -EINVAL : sc1777y_secure_channel_connect(channel);
}

int mqtt_client_custom_transport_write(struct mqtt_client *client,
				       const uint8_t *data, uint32_t datalen)
{
	struct sc1777y_secure_channel *channel = channel_from_client(client);

	if (channel == NULL || (data == NULL && datalen != 0U)) {
		return -EINVAL;
	}

	return datalen == 0U ? 0 : sc1777y_secure_channel_send(channel, data, datalen);
}
```

`read()` delegates to `sc1777y_secure_channel_recv()`. `disconnect()` delegates to `close()`. `write_msg()` validates `message`, then calls `send()` once for each non-empty iovec in order and stops on the first error. It must not mutate the caller's `msghdr` or iovec array.

- [ ] **Step 5: Run adapter tests and verify GREEN**

Run the Task 1 Twister command.

Expected: PASS.

- [ ] **Step 6: Enforce the thin boundary**

Run:

```bash
rg -n 'sc1777y_(get|set|import|session|hash|sign|verify|generate)|zsock_|Type|Subtype|0x80' \
  subsys/net/lib/sc1777y_secure_channel/mqtt_transport.c
```

Expected: no matches except the prefix inside `sc1777y_secure_channel_*` function names; there must be no driver, socket, protocol, padding, or IV calls.

- [ ] **Step 7: Commit the thin adapter**

```bash
git add include/zephyr/net/sc1777y_secure_mqtt_transport.h \
  subsys/net/lib/sc1777y_secure_channel \
  tests/net/lib/sc1777y_secure_mqtt_transport/unit
git commit -m "net: add sc1777y mqtt custom transport"
```

---

### Task 2: Build the native_sim MQTT acceptance application

**Files:**
- Create: `tests/net/lib/sc1777y_secure_mqtt_transport/e2e/CMakeLists.txt`
- Create: `tests/net/lib/sc1777y_secure_mqtt_transport/e2e/prj.conf`
- Create: `tests/net/lib/sc1777y_secure_mqtt_transport/e2e/testcase.yaml`
- Create: `tests/net/lib/sc1777y_secure_mqtt_transport/e2e/boards/native_sim.overlay`
- Create: `tests/net/lib/sc1777y_secure_mqtt_transport/e2e/src/main.c`
- Create: `tests/net/lib/sc1777y_secure_mqtt_transport/e2e/src/payloads.h`

**Interfaces:**
- Consumes: Zephyr MQTT API, bind helper from Task 1, complete phase-one channel API.
- Produces: one deterministic MQTT client flow with observable PASS markers.

- [ ] **Step 1: Add exact topics and payload fixtures**

`payloads.h` must define:

```c
#define DEVICE_ID "869010075627892"
#define TOPIC_TOPO_ADD "/v1/devices/" DEVICE_ID "/topo/add"
#define TOPIC_DATA "/v1/devices/" DEVICE_ID "/datas"
#define TOPIC_DOWNLINK "/v1/devices/" DEVICE_ID "/commands"
#define TOPIC_TEST_UP "/v1/devices/" DEVICE_ID "/test-up"

static const char topo_payload[] =
	"{\"mid\":869010075627892,\"deviceInfos\":{\"nodeId\":\"869010075627892\"," \
	"\"name\":\"XJ_ZNJDX\",\"description\":\"XJ_ZNJDX\"," \
	"\"manufacturerId\":\"869010075627892\",\"model\":\"XJ_ZNJDX\"}}";

static const char data_payload[] =
	"{\"devices\": [{\"deviceId\": \"869010075627892\",\"services\": [{\"data\": {"
	"\"type\": \"400\",\"id\": \"869010075627892\",\"date\": \"260512\","
	"\"time\": \"154112\",\"height\": \"0.00000000\",\"geo_sep\": \"0.00000000\","
	"\"longitude\": \"0.00000000\",\"latitude\": \"0.00000000\","
	"\"speed\": \"0.00000000\",\"direction\": \"0.00000000\",\"voltage\": \"380\","
	"\"temperature\": \"2530\",\"state_a\": \"0\",\"state_b\": \"0\","
	"\"state_c\": \"0\",\"date1\": \"0\",\"date2\": \"0\",\"date3\": \"0\","
	"\"precision\": \"0\",\"satellite\": \"0\",\"count\": \"0\",\"alarm\": \"0\"},"
	"\"eventTime\": \"20260512T154112Z\",\"serviceId\": \"analog\"}]}]}";
```

The strings above are the phase-two payload fixtures; do not replace them with shorter smoke-test data.

- [ ] **Step 2: Add the MQTT state-machine source and verify build RED**

The app state must track CONNACK, SUBACK, two QoS 0 publishes, one QoS 1 publish/PUBACK, one QoS 1 downlink/PUBACK, PINGRESP, and DISCONNECT. First add calls to `sc1777y_secure_mqtt_transport_bind()` and verify the test does not yet pass because the host harness is absent.

Run:

```bash
west twister -T tests/net/lib/sc1777y_secure_mqtt_transport/e2e -p native_sim \
  --inline-logs --outdir build/twister_secure_mqtt_app
```

Expected: build succeeds, pytest/harness fails to connect to a gateway or is not yet present.

- [ ] **Step 3: Configure the secure channel and MQTT client**

The gateway address is `192.0.2.2:18883`; terminal is `192.0.2.1/24`; interface is `zeth`. Use MQTT 3.1.1, client id `sc1777y-terminal-869010075627892`, clean session, keepalive 5 seconds, and caller-owned MQTT RX/TX buffers of at least 4096 bytes.

Initialization order must be:

```c
sc1777y_secure_channel_init(&secure_channel, &secure_config);
mqtt_client_init(&mqtt_client);
sc1777y_secure_mqtt_transport_bind(&mqtt_client, &secure_channel);
mqtt_connect(&mqtt_client);
```

- [ ] **Step 4: Implement the event-driven acceptance flow**

After CONNACK subscribe QoS 1 to `TOPIC_DOWNLINK`. After SUBACK publish the two attachment payloads at QoS 0 and `phase2-qos1-upstream` on `TOPIC_TEST_UP` at QoS 1. Run a 10 ms event loop that calls `mqtt_input()` and treats `-EAGAIN` as no input, then calls `mqtt_live()` to drive keepalive; do not expose the secure socket fd. On downlink, read the entire payload with `mqtt_read_publish_payload_blocking()` and call `mqtt_publish_qos1_ack()`. Print `MQTT_SECURE_E2E_PASS` only after PUBACK, downlink ACK, and PINGRESP are all observed.

- [ ] **Step 5: Build and retain the expected failing host-side result**

Run the Task 2 command.

Expected: application builds with no direct SC1777Y calls in `main.c`; test remains RED until Task 3 supplies Mosquitto and the gateway proxy.

- [ ] **Step 6: Commit the MQTT acceptance application**

```bash
git add tests/net/lib/sc1777y_secure_mqtt_transport/e2e
git commit -m "tests: add secure mqtt native_sim client"
```

---

### Task 3: Orchestrate SecurityGatewayPeer and a real Mosquitto Broker

**Files:**
- Create: `tests/net/lib/sc1777y_secure_mqtt_transport/e2e/pytest/conftest.py`
- Create: `tests/net/lib/sc1777y_secure_mqtt_transport/e2e/pytest/test_mqtt_transport.py`
- Modify: `tests/net/lib/sc1777y_secure_mqtt_transport/e2e/testcase.yaml`

**Interfaces:**
- Consumes: reusable phase-one `SecurityGatewayPeer`, phase-two native app, Mosquitto CLI.
- Produces: a real-Broker happy-path phase-two TAP end-to-end test.

- [ ] **Step 1: Write the failing pytest assertion flow**

```python
def test_secure_mqtt_against_real_broker(
    dut, mosquitto_broker, secure_gateway, upstream_subscriber
):
    dut.launch()
    dut.readlines_until("MQTT_SUBSCRIBED", timeout=30.0)
    publish_downlink_qos1("phase2-downstream")
    dut.readlines_until("MQTT_SECURE_E2E_PASS", timeout=30.0)

    messages = upstream_subscriber.wait_for_messages(3, timeout=10.0)
    assert messages[0].topic.endswith("/topo/add")
    assert messages[1].topic.endswith("/datas")
    assert messages[2].payload == "phase2-qos1-upstream"
    assert secure_gateway.handshake_count == 1
```

- [ ] **Step 2: Run pytest/Twister and verify RED**

Run the Task 2 Twister command.

Expected: FAIL because Mosquitto fixtures are undefined.

- [ ] **Step 3: Implement the isolated Mosquitto fixture**

Create a temporary config containing exactly:

```text
listener 18884 127.0.0.1
allow_anonymous true
persistence false
log_type all
```

Start `mosquitto -c <config> -v`, wait until the log contains `Opening ipv4 listen socket on port 18884`, and terminate it in `finally`. Missing `mosquitto` is `pytest.fail()`.

- [ ] **Step 4: Start the real observer and downlink client tools**

Run the observer as:

```bash
mosquitto_sub -h 127.0.0.1 -p 18884 -q 1 -v \
  -t '/v1/devices/869010075627892/topo/add' \
  -t '/v1/devices/869010075627892/datas' \
  -t '/v1/devices/869010075627892/test-up'
```

Publish downlink as:

```bash
mosquitto_pub -h 127.0.0.1 -p 18884 -q 1 \
  -t '/v1/devices/869010075627892/commands' -m 'phase2-downstream'
```

The fixture parses `mosquitto_sub -v` output into topic/payload records. Missing either CLI is `pytest.fail()`.

- [ ] **Step 5: Start TAP and the transparent gateway**

Reuse the phase-one TAP fixture behavior. Instantiate:

```python
SecurityGatewayPeer(
    listen_addr=("192.0.2.2", 18883),
    upstream_addr=("127.0.0.1", 18884),
)
```

Do not add MQTT parsing to `SecurityGatewayPeer`; it only decrypts/encrypts secure records and copies plaintext bytes between sockets.

- [ ] **Step 6: Run the real-Broker test and verify GREEN**

Run the Task 2 Twister command.

Expected: PASS; Mosquitto logs a real client connection, `mosquitto_sub` observes all three upbound publications, `mosquitto_pub` drives the downlink, and the DUT prints `MQTT_SECURE_E2E_PASS`.

- [ ] **Step 7: Commit the real-Mosquitto harness**

```bash
git add tests/net/lib/sc1777y_secure_mqtt_transport/e2e
git commit -m "tests: proxy secure mqtt to real mosquitto"
```

---

### Task 4: Add MQTT-specific reconnect and transport-failure acceptance

**Files:**
- Modify: `tests/net/lib/sc1777y_secure_mqtt_transport/e2e/src/main.c`
- Modify: `tests/net/lib/sc1777y_secure_mqtt_transport/e2e/pytest/test_mqtt_transport.py`
- Modify: `tests/net/lib/sc1777y_secure_mqtt_transport/e2e/pytest/conftest.py`

**Interfaces:**
- Consumes: complete phase-two path.
- Produces: proof that MQTT reconnect creates a fresh socket and fresh security handshake.

- [ ] **Step 1: Add a failing reconnect scenario**

The gateway fixture closes the terminal connection immediately after the first QoS 1 PUBACK. The DUT must observe MQTT disconnect, call `sc1777y_secure_channel_close()`, rerun channel init/connect through `mqtt_connect()`, resubscribe, publish `phase2-after-reconnect`, receive it through `mosquitto_sub`, and print `MQTT_SECURE_RECONNECT_PASS`.

- [ ] **Step 2: Run and verify RED**

Expected: the application exits or stalls after the forced disconnect because reconnect flow is absent.

- [ ] **Step 3: Implement application-owned reconnect**

Use one bounded retry in the test app only. The library and adapter remain free of retry loops. Reset MQTT client state, explicitly close the channel, reinitialize both contexts, bind the adapter again, and call `mqtt_connect()`.

- [ ] **Step 4: Verify two independent handshakes and post-reconnect traffic**

Assert `secure_gateway.handshake_count == 2`, `mosquitto_sub` receives `phase2-after-reconnect`, and both PASS markers appear.

- [ ] **Step 5: Run the phase-two e2e test and verify GREEN**

Run the Task 2 Twister command.

Expected: happy path and reconnect path PASS against the real Broker.

- [ ] **Step 6: Commit reconnect acceptance**

```bash
git add tests/net/lib/sc1777y_secure_mqtt_transport/e2e
git commit -m "tests: cover secure mqtt reconnect"
```

---

### Task 5: Document and run the combined mandatory gate

**Files:**
- Create: `tests/net/lib/sc1777y_secure_mqtt_transport/README.rst`
- Modify: `tests/net/lib/sc1777y_secure_mqtt_transport/unit/testcase.yaml`
- Modify: `tests/net/lib/sc1777y_secure_mqtt_transport/e2e/testcase.yaml`
- Modify: `tests/net/lib/sc1777y_secure_channel/README.rst`

**Interfaces:**
- Consumes: phase-one and phase-two tests.
- Produces: one default command that always runs both end-to-end tests.

- [ ] **Step 1: Document all mandatory host prerequisites**

List `CAP_NET_ADMIN`/root, `NET_TOOLS_BASE`, `mosquitto`, `mosquitto_sub`, and `mosquitto_pub`. State that neither TAP e2e case may be skipped.

- [ ] **Step 2: Document the exact combined command**

```bash
west twister \
  -T tests/net/lib/sc1777y_secure_channel \
  -T tests/net/lib/sc1777y_secure_mqtt_transport \
  -p native_sim --inline-logs \
  --outdir build/twister_sc1777y_secure_mqtt_all
```

- [ ] **Step 3: Run boundary scans**

```bash
rg -n 'drivers/misc/sc1777y|sc1777y_emul|zsock_|Type|Subtype|padding|IV' \
  subsys/net/lib/sc1777y_secure_channel/mqtt_transport.c \
  tests/net/lib/sc1777y_secure_mqtt_transport/unit/src/test_adapter.c
```

Expected: no direct driver/emulator, socket, protocol, padding, or IV usage in the adapter and its unit test.

- [ ] **Step 4: Run formatting/static checks**

```bash
git diff --check
./scripts/checkpatch.pl --git HEAD~4..HEAD
```

Expected: no errors.

- [ ] **Step 5: Run the complete mandatory two-phase gate**

Run the combined Twister command.

Expected: phase-one unit tests PASS, phase-one TAP echo e2e PASS, adapter unit tests PASS, and phase-two TAP real-Mosquitto e2e plus reconnect PASS.

- [ ] **Step 6: Commit final test documentation**

```bash
git add tests/net/lib/sc1777y_secure_channel/README.rst \
  tests/net/lib/sc1777y_secure_mqtt_transport/README.rst \
  tests/net/lib/sc1777y_secure_mqtt_transport/unit/testcase.yaml \
  tests/net/lib/sc1777y_secure_mqtt_transport/e2e/testcase.yaml
git commit -m "docs: add secure mqtt mandatory test gate"
```

The feature is complete only when Task 5 passes without filtering either end-to-end test.
