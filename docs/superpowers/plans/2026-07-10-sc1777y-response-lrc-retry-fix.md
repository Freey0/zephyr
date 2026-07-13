# SC1777Y Response LRC Retry Fix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make a response LRC failure restart only the SC1777Y query/receive flow, while preserving full-command retransmission for a valid `6A90` response.

**Architecture:** Keep the public driver API unchanged. Move the initial command write outside the bounded attempt loop; an invalid response LRC loops back to query/receive, while a valid `6A90` explicitly writes the command again. Extend the emulator only enough to replay a corrected response on a receive restart and lock the distinction down with update-path regression tests.

**Tech Stack:** Zephyr C driver, SPI emulator, ztest, Twister on `native_sim`.

## Global Constraints

- `6A90` remains a full-command retransmission as required by SC1777Y section 4.7.1.
- A host-detected response LRC mismatch never retransmits the command; it restarts query/receive as required by section 4.7.2.
- The existing maximum of three total response attempts remains unchanged.
- `sc1777y_apply_key_update()` must send `80 22 02 01` exactly once when only the first response LRC is corrupted.
- No public SC1777Y driver API changes.
- No unrelated SPI timing or error-mapping changes.

---

### Task 1: Lock down receive-only retry behavior

**Files:**
- Modify: `tests/drivers/misc/sc1777y/src/protocol.c`
- Modify: `tests/drivers/misc/sc1777y/src/api_update.c`

**Interfaces:**
- Consumes: `sc1777y_emul_corrupt_next_response_lrc()`, `sc1777y_emul_get_command_count()`.
- Produces: regression coverage proving one command write for a response LRC retry and two command writes for a transient `6A90`.

- [x] **Step 1: Write failing tests**

Change the protocol test to require one command write after a response LRC failure, and add the side-effecting update-path test:

```c
ZTEST_F(sc1777y, test_command_restarts_receive_after_response_lrc_error)
{
	const struct sc1777y_command cmd = {.cla = 0x00, .ins = 0x84, .p1 = 0x00, .p2 = 0x04};
	uint8_t out[4];
	size_t out_len;

	sc1777y_emul_corrupt_next_response_lrc(fixture->emul);
	zassert_ok(sc1777y_command(fixture->dev, &cmd, out, sizeof(out), &out_len, NULL));
	zassert_equal(1, sc1777y_emul_get_command_count(fixture->emul));
}

ZTEST_F(sc1777y, test_apply_key_update_does_not_resend_after_response_lrc_error)
{
	const uint8_t key_data[] = {0x10, 0x20, 0x30, 0x40};

	sc1777y_emul_corrupt_next_response_lrc(fixture->emul);
	zassert_ok(sc1777y_apply_key_update(fixture->dev, key_data, sizeof(key_data)));
	zassert_equal(1, sc1777y_emul_get_command_count(fixture->emul));
}
```

- [x] **Step 2: Run Twister and verify RED**

Run:

```bash
/home/imch/zephyrproject-v4.3.0/.venv/bin/west twister \
  -T tests/drivers/misc/sc1777y -p native_sim --inline-logs \
  --outdir /tmp/twister_sc1777y_lrc_red
```

Expected: the two response-LRC tests fail because `command_count` is 2 instead of 1; the `6A90` retry test remains green.

### Task 2: Separate command and response retry actions

**Files:**
- Modify: `drivers/misc/sc1777y/sc1777y.c`
- Modify: `drivers/misc/sc1777y/sc1777y_emul.c`

**Interfaces:**
- Consumes: existing `sc1777y_write_frame()`, `sc1777y_poll_ready()`, and `sc1777y_read_response()` private helpers.
- Produces: unchanged public `sc1777y_command()` behavior with corrected transport actions.

- [x] **Step 1: Implement response replay in the emulator**

Track whether the stored response LRC was intentionally corrupted. When a new one-byte query starts after the full response was consumed, restore the correct LRC and reset `response_offset`, `ready_polls_remaining`, and `response_ready` without incrementing `command_count`.

```c
if (rx_bufs != NULL && rx_bufs->count == 1U && rx_bufs->buffers[0].len == 1U &&
    data->response_ready && data->response_offset == data->response_len) {
	if (data->response_lrc_corrupted) {
		data->response[data->response_len - 1U] ^= 0xFFU;
		data->response_lrc_corrupted = false;
	}
	data->response_offset = 0U;
	data->ready_polls_remaining = data->ready_delay;
	data->response_ready = false;
}
```

- [x] **Step 2: Implement the minimal driver fix**

Write the command once before the attempt loop. Inside the loop:

```c
ret = sc1777y_write_frame(&cfg->bus, data->frame, frame_len);
if (ret != 0) {
	return ret;
}

for (int attempt = 0; attempt < SC1777Y_MAX_RETRIES; attempt++) {
	ret = sc1777y_poll_ready(&cfg->bus);
	if (ret != 0) {
		return ret;
	}
	ret = sc1777y_read_response(&cfg->bus, data->response, sizeof(data->response),
				    &response_len, &local_status);
	if (ret == -EBADMSG) {
		continue;
	}
	if (ret != 0) {
		return ret;
	}
	/* A valid 6A90 response is the only branch that writes the command again. */
}
```

On the final invalid response LRC, preserve the existing exhausted-retry result `-EIO`. Leave all other return paths unchanged.

- [x] **Step 3: Run Twister and verify GREEN**

Run the Task 1 command again with `/tmp/twister_sc1777y_lrc_green` as the output directory.

Expected: all SC1777Y driver test cases pass, including the unchanged `6A90` tests.

### Task 3: Correct normative documentation and verify the complete change

**Files:**
- Modify: `docs/superpowers/specs/2026-07-08-sc1777y-driver-design.md`
- Modify: `docs/superpowers/plans/2026-07-08-sc1777y-driver.md`
- Review only: `docs/superpowers/specs/2026-07-10-sc1777y-maintenance-software-design.md`

**Interfaces:**
- Consumes: SC1777Y sections 4.7.1 and 4.7.2.
- Produces: unambiguous documentation distinguishing command retransmission from response re-reception.

- [x] **Step 1: Correct the driver design and historical implementation plan**

State explicitly:

- valid `6A90` response: retransmit the full command;
- invalid response LRC: restart query/receive without retransmitting the command;
- at most three total response attempts.

Update the historical test example so it expects `command_count == 1` for a response LRC failure.

- [x] **Step 2: Run formatting and focused verification**

Run:

```bash
git diff --check
/home/imch/zephyrproject-v4.3.0/.venv/bin/west twister \
  -T tests/drivers/misc/sc1777y -p native_sim --inline-logs \
  --outdir /tmp/twister_sc1777y_lrc_final
```

Expected: `git diff --check` exits 0 and all SC1777Y tests pass without warnings.

- [x] **Step 3: Review the maintenance design closure separately**

Re-evaluate the remaining business-closure findings after removing this driver defect from the list. Do not edit the maintenance design until the user chooses the desired closure policies.

- [x] **Step 4: Commit the focused change**

```bash
git add drivers/misc/sc1777y/sc1777y.c \
  drivers/misc/sc1777y/sc1777y_emul.c \
  tests/drivers/misc/sc1777y/src/protocol.c \
  tests/drivers/misc/sc1777y/src/api_update.c \
  docs/superpowers/specs/2026-07-08-sc1777y-driver-design.md \
  docs/superpowers/plans/2026-07-08-sc1777y-driver.md \
  docs/superpowers/plans/2026-07-10-sc1777y-response-lrc-retry-fix.md
git commit -m "drivers: fix sc1777y response LRC retry"
```
