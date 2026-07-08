# SC1777Y Driver Design

## Goal

Add a Zephyr driver for the SC1777Y security chip, a byte-level SPI emulator, and a native_sim sample that validates every documented interaction through the driver's semantic API.

## Scope

The driver implements the SC1777Y SPI command protocol from the product manual and exposes semantic APIs for the interaction flows in chapter 5. Applications and samples do not construct command frames, CLA/INS values, LRC bytes, polling bytes, or retry loops.

The emulator is a protocol-level deterministic model. It validates SC1777Y SPI framing and returns predictable data for cryptographic operations, but it does not implement real SM1, SM2, SM3, SM4, or SM7 algorithms.

## Architecture

### Public Driver API

Create `include/zephyr/drivers/misc/sc1777y.h`.

The header exposes:

- Status definitions for SC1777Y status words.
- Bounded buffer constants for command input and output.
- Enumerations for sensor model type and platform type.
- Semantic functions for chapter 5 flows.
- One low-level escape hatch, `sc1777y_command()`, for users who need an unsupported command while still using the driver's transport handling.

The public API is organized by user intent:

- Basic chip information: get version info, serial number, key version, random data.
- Terminal/sensor authentication: begin authentication, fetch peer information, encrypt peer challenge, verify peer authentication response.
- Terminal/sensor business data: encrypt and decrypt sensor-to-terminal and terminal-to-sensor payloads.
- Field key update: get update identity material, verify update authentication ciphertext, fetch update random, apply key update data.
- Platform setup: import platform public key, import AK, import IV, set and get platform type.
- Certificate request: generate SM2 keypair, generate certificate request.
- Session negotiation: begin session, hash request/response body, sign hash, verify response signature, generate auth response, confirm session.
- Session payload crypto: generate IV random, encrypt, decrypt.

### Private Transport Layer

Create `drivers/misc/sc1777y/sc1777y.c`.

The private transport layer owns:

- SPI mode 3 configuration.
- Command frame construction: `55 CLA INS P1 P2 Len1 Len2 DATA LRC1`.
- LRC calculation by XORing bytes and inverting the result.
- Command completion polling by reading until `0x55`.
- Response frame parsing: `SW1 SW2 Len1 Len2 DATA LRC2`.
- Receive LRC validation.
- Retry handling for send LRC errors (`6A90`) and receive LRC failures, up to 3 attempts.
- Mapping chip status words to negative errno values while preserving the raw status in a caller-visible result structure where useful.

The transport layer is not exposed as a generic SPI helper. It remains private to this driver because the timing, polling, LRC, and status model are specific to SC1777Y.

### Emulator

Create `drivers/misc/sc1777y/sc1777y_emul.c` and a private emulator header used by tests.

The emulator registers as a SPI emulator on `zephyr,spi-emul-controller`. It:

- Accepts only SC1777Y command frames.
- Validates the command header, length, and LRC at byte level.
- Returns `6A90` for command LRC errors.
- Implements the documented status words for malformed or unsupported commands.
- Models command completion polling by returning non-ready bytes before `0x55` when configured to do so.
- Produces deterministic payloads for all semantic API commands.
- Provides test controls for injecting receive LRC corruption, delayed readiness, specific status words, and fixed response payloads.

The emulator stores simple state required by the flows: chip serial number, key version bytes, platform type, imported public key presence, generated SM2 keypair flag, session started flag, IV, AK, and key-update authentication flag.

### Devicetree And Kconfig

Add bindings:

- `dts/bindings/misc/sc1777y.yaml` for common device properties.
- `dts/bindings/misc/sc1777y-spi.yaml` including `spi-device.yaml`.

Add Kconfig and build integration under `drivers/misc/sc1777y`:

- `CONFIG_SC1777Y` depends on `DT_HAS_*_SC1777Y_ENABLED` and selects `SPI`.
- `CONFIG_EMUL_SC1777Y` depends on `SC1777Y` and `EMUL`.

Register `drivers/misc/sc1777y` from `drivers/misc/CMakeLists.txt` and source its Kconfig from `drivers/misc/Kconfig`.

### Tests

Create `tests/drivers/misc/sc1777y`.

Tests are written before implementation and run on native_sim. They verify:

- LRC calculation and command framing.
- SPI mode and device readiness.
- Polling until `0x55`.
- Receive LRC validation and retry.
- Send LRC error retry on `6A90`.
- Unknown command and malformed length handling.
- Every public semantic API emits the expected chip command and parses the deterministic emulator response.
- Application-facing APIs reject NULL pointers, undersized buffers, invalid payload lengths, and unsupported enum values.

The tests include byte-level emulator assertions so regressions in CLA/INS/P1/P2/length encoding are caught even when semantic API calls still return success.

### Sample

Create `samples/drivers/sc1777y`.

The sample uses native_sim with an overlay that places an SC1777Y node under `spi0`.

The sample only calls public semantic API functions. It does not:

- Construct APDU-like command bytes.
- Set CLA, INS, P1, or P2 directly.
- Calculate LRC.
- Poll for `0x55`.
- Retry SPI transfers itself.

The sample runs a readable smoke flow:

1. Read chip version, serial number, key version, and random bytes.
2. Run terminal/sensor authentication through semantic API calls.
3. Encrypt and decrypt terminal/sensor business payloads.
4. Run field key update authentication and key update data application.
5. Configure platform public key, AK, IV, and platform type.
6. Generate SM2 keypair and certificate request.
7. Run session negotiation calls: begin, hash, sign, verify, auth, confirm.
8. Encrypt and decrypt session payloads.

The sample asserts expected deterministic emulator responses and prints concise PASS lines for each semantic group.

## Error Handling

Public APIs return `0` on `9000`. Non-success chip statuses return negative errno:

- `6A90` maps to retry while attempts remain, then `-EIO`.
- Invalid arguments map to `-EINVAL`.
- Unsupported chip commands map to `-ENOTSUP`.
- Security/authentication failures map to `-EACCES`.
- Timeout waiting for `0x55` maps to `-ETIMEDOUT`.
- SPI bus failures map to the underlying SPI errno when available.

APIs that need the exact chip status use a result structure containing `sw1` and `sw2`, so callers can distinguish chip-specific failures without parsing transport frames.

## Data Limits

The driver supports SC1777Y payloads large enough for all documented commands:

- General response data up to 2048 bytes for session encryption and decryption.
- Fixed-size outputs for serial number, random data, hashes, signatures, encrypted random values, auth response data, and session key digest.
- Input validation for documented block-size rules: sensor business data is an 8-byte multiple, session payload crypto is a 16-byte multiple from 16 to 2048 bytes.

Buffer sizes are explicit in the API. The driver never writes past caller-provided buffers and reports required sizes when output buffers are too small.

## Non-Goals

- Implementing real SM1, SM2, SM3, SM4, or SM7 in the emulator.
- Replacing Zephyr's generic crypto API.
- Making sample code construct SC1777Y command frames.
- Supporting I2C or UART transports.
- Modeling hardware power sequencing beyond device readiness and SPI command behavior.

## Acceptance Criteria

- The driver builds for native_sim.
- The emulator validates SC1777Y SPI frames at byte level.
- Tests cover all public semantic APIs and transport retry/error paths.
- The sample mounts the emulator on native_sim and validates all chapter 5 interactions through semantic API calls only.
- No sample code contains SC1777Y CLA/INS command literals or LRC handling.
