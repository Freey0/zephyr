# SC1777Y Sample Guide Design

Date: 2026-07-09

## Goal

Reshape `samples/drivers/sc1777y` into a guide-style sample for new driver
users. The sample should explain how to follow the PDF chapter 5 interaction
flows with the SC1777Y driver. It should make the data exchanged between
Sensor, Maintenance Software, Platform, and Terminal clear.

The sample is not responsible for validating emulator fixed responses, APDU
frames, or edge cases. Those remain the job of `tests/drivers/misc/sc1777y`.

## Sample Responsibility

The sample should:

- Show the chapter 5 flows as executable examples.
- Explain prerequisites from the PDF before each flow.
- Show which user sends which data to which other user.
- Show which fields are consumed or produced by each driver API call.
- Check only whether each driver API call succeeds.
- End with `SC1777Y sample PASS` so the existing console harness can still
  validate that the example ran.

The sample should not:

- Compare emulator fixed data with `expect_equal`.
- Check emulator deterministic byte sequences with `expect_sequence`.
- Reconstruct emulator XOR behavior with `fill_xor`.
- Treat the sample as protocol-frame or emulator-output test coverage.

## Function Structure

Each `run_5_x_x_*()` function should follow this shape:

1. A block comment with the PDF section title.
2. An ASCII flow chart.
3. `Prerequisites:` listing protocol or deployment requirements from the PDF.
4. `Data exchanged:` listing fields exchanged between users.
5. `Guide:` explaining the purpose of the flow and the driver boundary.
6. Local variables for this flow's exchanged data.
7. `printf` lines that print the flow name, prerequisites, and exchanged data.
8. Step comments mapping PDF steps to driver calls or user handoffs.
9. Driver API calls with return-code checks.
10. A flow-level `PASS` line.

All data variables used by a flow should stay inside that flow function. The
file should keep only general helpers at file scope.

## Comment Style

Comments should read like a usage guide.

Driver-call steps should say what the current user is doing internally:

```c
/* Flow step 1, Terminal internal operation: generate Rand1[4]. */
rc = sc1777y_get_random4(dev, rand1);
```

User handoff steps should state the data movement, without explaining that
there is no driver call:

```c
/*
 * Flow step 2, Terminal-to-Sensor handoff:
 * send Rand1[4].
 */
printf("  Terminal -> Sensor: Rand1[4]\n");
```

The comments should avoid making the security chip an external participant.
The visible users are Terminal, Sensor, Maintenance Software, and Platform.
Chip interaction may be mentioned only as an internal operation of one user
when that makes the driver call clearer.

## Output Style

Runtime output should be useful as a flow trace. It should print field names
and lengths, not fixed emulator bytes.

Example:

```text
[5.3.3] Session negotiation
  Prerequisite: terminal certificate exists
  Prerequisite: platform public key has been imported
  Prerequisite: platform type has been selected before auth response
  Terminal -> Platform: RequestMsg { DATA, RequestSign[64] }
    DATA { Type, SubType, Len, Ver, SN, SIM, ID, Cert1, EnR1[128] }
    Key driver data: EnR1[128], RequestHash[32], RequestSign[64]
  Platform -> Terminal: ResponseMsg { Type, SubType, Len, SN, AuthFactor[32], EnR2[128], ResponseSign[64] }
    Key driver data: AuthFactor[32], EnR2[128], ResponseHash[32], ResponseSign[64]
  Terminal -> Platform: ConfirmMsg { Type, SubType, Len, SN, AuthResult, DKHash[32] }
    Key driver data: AuthResult, DKHash[32]
[5.3.3] PASS
```

Errors should still name the failing API:

```text
5.3.3 session_begin failed: -5
SC1777Y sample FAIL
```

## Platform Message Format

For Platform exchanges, preserve the PDF message shape first, then call out the
driver-relevant fields.

Example for session negotiation:

```c
 * Data exchanged:
 * - Terminal -> Platform:
 *   RequestMsg { DATA, RequestSign[64] }
 *   DATA { Type, SubType, Len, Ver, SN, SIM, ID, Cert1, EnR1[128] }
 *   Key driver data: EnR1[128], RequestHash[32], RequestSign[64]
 *
 * - Platform -> Terminal:
 *   ResponseMsg { Type, SubType, Len, SN, AuthFactor[32], EnR2[128],
 *                 ResponseSign[64] }
 *   Key driver data: AuthFactor[32], EnR2[128], ResponseHash[32],
 *                    ResponseSign[64]
 *
 * - Terminal -> Platform:
 *   ConfirmMsg { Type, SubType, Len, SN, AuthResult, DKHash[32] }
 *   Key driver data: AuthResult, DKHash[32]
```

For non-Platform flows, list the exchanged fields directly:

```c
 * Data exchanged:
 * - Terminal -> Sensor: Rand1[4]
 * - Sensor -> Terminal: sensorEsamID[8], Version[4], enRand1[8]
 * - Terminal -> Sensor: AuthResult
```

## Required Prerequisites

Each flow should include the important PDF constraints in `Prerequisites:`.
At minimum:

- 5.1.1 Identity authentication: Sensor and Terminal should complete identity
  authentication before business data exchange.
- 5.1.2 Business data: Terminal should maintain the mapping between Sensor
  device address and `sensorEsamID[8]` before handling Sensor data.
- 5.2.1 Key update/recovery: Maintenance Software should have the update or
  recovery USBKey and interface library, and a field-maintenance channel to
  Terminal.
- 5.3.1 Platform basic instructions: Platform public key, AK, and IV material
  come from Platform or Platform configuration.
- 5.3.2 Certificate request: Terminal should generate or confirm the local SM2
  key pair before generating CSR data. Regenerating the key pair overwrites the
  old key pair and requires certificate re-enrollment.
- 5.3.3 Session negotiation: before session begin, Terminal certificate exists
  and Platform public key has been imported. Before auth response generation,
  Platform type has been selected.
- 5.3.4 Session-key encryption: session negotiation has succeeded, and input
  length satisfies the 16-byte block requirement.
- 5.3.5 Session-key decryption: session negotiation has succeeded, and
  Terminal has parsed `IV[16]` and ciphertext from Platform's message.
- 5.3.6 Platform type selection: this should run before the 5.3.3 auth response
  step that depends on Platform type.

## Testing Strategy

The sample should be tested as an executable guide:

- `sample.yaml` keeps the console harness looking for `SC1777Y sample PASS`.
- The sample checks `rc` after each driver API call.
- The sample does not check fixed bytes returned by the emulator.
- Emulator behavior, APDU frames, error paths, and exact output bytes are
  covered by `tests/drivers/misc/sc1777y`.

After implementation, run:

```sh
scripts/twister -T samples/drivers/sc1777y -p native_sim --inline-logs
scripts/twister -T tests/drivers/misc/sc1777y -p native_sim --inline-logs
```

## Open Scope Boundary

This design does not add a new helper library or a second sample. It reshapes
the existing sample into a clearer guide while keeping the current test suite
as the source of detailed behavioral verification.
