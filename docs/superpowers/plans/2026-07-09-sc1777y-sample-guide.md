# SC1777Y Sample Guide Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 `samples/drivers/sc1777y` 改成说明书式可执行样例，清楚展示 PDF 第 5 章中 Terminal、Sensor、Maintenance Software、Platform 之间交换的数据和驱动 API 调用位置。

**Architecture:** 保留一个 sample 源文件，不新增 helper 库。`main.c` 只做流程演示、字段/长度日志、API 返回值检查；详细 emul 输出和 APDU 正确性继续由 `tests/drivers/misc/sc1777y` 覆盖。

**Tech Stack:** Zephyr sample、SC1777Y misc driver API、native_sim、twister console harness。

## Global Constraints

- 样例只检查每个驱动 API 的 `rc`。
- 样例不使用 `expect_equal`、`expect_sequence`、`fill_xor` 验证 emul 固定返回。
- 运行输出打印字段名和长度，不打印固定 emul 字节。
- 平台报文必须保留 PDF 中的完整字段结构，并额外列出 `Key driver data`。
- 非平台流程直接列出用户之间交换的字段和长度。
- 注释和日志只以 Terminal、Sensor、Maintenance Software、Platform 作为可见参与方。
- 用户间交付步骤只描述数据流转，不写 `No driver call is needed...` 这类解释。
- 每个流程函数内部声明本流程使用的数据变量。
- `sample.yaml` 仍需通过匹配 `SC1777Y sample PASS` 判断样例执行成功。

---

## File Structure

- Modify: `samples/drivers/sc1777y/src/main.c`
  - 责任：说明 PDF 第 5 章每个交互流程；打印前置条件、交换字段、平台报文结构；调用驱动 API；只检查返回值。
- Modify: `samples/drivers/sc1777y/sample.yaml`
  - 责任：将 sample 名称从测试/芯片口径改成流程说明口径；console harness 保持不变。
- Do not modify for this plan: `drivers/misc/sc1777y/sc1777y.c`
  - 驱动实现已经由独立改动覆盖，本计划不改驱动行为。
- Do not modify for this plan: `include/zephyr/drivers/misc/sc1777y.h`
  - 公共 API 已存在，本计划只调整样例使用方式。
- Do not modify for this plan: `tests/drivers/misc/sc1777y/*`
  - emul 固定字节、APDU 帧、错误路径继续由这些测试承担。

---

### Task 1: 清理 sample 的测试式 helper，建立说明书式输出基础

**Files:**
- Modify: `samples/drivers/sc1777y/src/main.c:1-90`

**Interfaces:**
- Consumes: existing `fail_step(const char *step, int rc)`
- Produces:
  - `static int fail_step(const char *step, int rc)`
  - `static void fill_example_bytes(uint8_t *buf, size_t len, uint8_t seed)`

- [ ] **Step 1: 删除测试式 helper**

Remove these functions from `samples/drivers/sc1777y/src/main.c`:

```c
static void fill_sequence(uint8_t *buf, size_t len, uint8_t start)
static void fill_xor(uint8_t *dst, const uint8_t *src, size_t len, uint8_t mask)
static bool expect_equal(const uint8_t *actual, const uint8_t *expected, size_t len)
static bool expect_sequence(const uint8_t *actual, size_t len, uint8_t start)
static bool expect_default_identity(const struct sc1777y_identity *identity)
static bool expect_default_serial(const uint8_t value[SC1777Y_SERIAL_LEN])
```

Also remove `#include <stdbool.h>` and `#include <string.h>` if they become unused.

- [ ] **Step 2: 添加样例输入数据 helper**

Add this helper after `fail_check()`:

```c
static void fill_example_bytes(uint8_t *buf, size_t len, uint8_t seed)
{
	for (size_t i = 0; i < len; i++) {
		buf[i] = seed + (uint8_t)i;
	}
}
```

This helper is only for constructing example input owned by Sensor,
Maintenance Software, Platform, or Terminal. It must not be used to check
emulator output.

- [ ] **Step 3: 保留 API 失败输出**

Keep this exact failure path so sample failures still identify the driver API:

```c
static int fail_step(const char *step, int rc)
{
	printf("%s failed: %d\n", step, rc);
	printf("SC1777Y sample FAIL\n");
	return 1;
}
```

Remove `fail_check()` after all callers are deleted by later tasks. If Task 1
is implemented before later tasks, keep `fail_check()` temporarily and delete
it in Task 5.

- [ ] **Step 4: Run a text check**

Run:

```sh
rg -n "expect_equal|expect_sequence|fill_xor" samples/drivers/sc1777y/src/main.c
```

Expected after all tasks are complete: no matches. During Task 1 alone,
matches may remain only if later flow functions still call these helpers.

- [ ] **Step 5: Commit**

Do not commit if this task is implemented in a dirty tree with unrelated
changes staged. If committing task-by-task, use:

```sh
git add samples/drivers/sc1777y/src/main.c
git commit -m "samples: prepare sc1777y guide output"
```

---

### Task 2: 重写 5.1 和 5.2 非平台流程为字段交换说明

**Files:**
- Modify: `samples/drivers/sc1777y/src/main.c`

**Interfaces:**
- Consumes:
  - `fill_example_bytes(uint8_t *buf, size_t len, uint8_t seed)`
  - `fail_step(const char *step, int rc)`
- Produces:
  - `run_5_1_1_identity_auth(const struct device *dev)`
  - `run_5_1_2_business_data(const struct device *dev)`
  - `run_5_2_1_key_update(const struct device *dev)`

- [ ] **Step 1: Rewrite `run_5_1_1_identity_auth()`**

Replace the function comment and body with guide-style structure:

```c
/*
 * 5.1.1 Identity authentication flow
 *
 * Flow:
 * +----------+        Rand1[4]        +--------+
 * | Terminal |----------------------->| Sensor |
 * |          |                        |        |
 * |          |<-----------------------|        |
 * +----------+ sensorEsamID[8],       +--------+
 *              Version[4], enRand1[8]
 *
 * Prerequisites:
 * - Sensor and Terminal should complete identity authentication before
 *   business data exchange.
 *
 * Data exchanged:
 * - Terminal -> Sensor: Rand1[4]
 * - Sensor -> Terminal: sensorEsamID[8], Version[4], enRand1[8]
 * - Terminal -> Sensor: AuthResult
 *
 * Guide:
 * Terminal creates Rand1[4]. Sensor returns identity and encrypted Rand1.
 * Terminal verifies the response and decides the authentication result.
 */
static int run_5_1_1_identity_auth(const struct device *dev)
{
	uint8_t rand1[4];
	struct sc1777y_identity sensor_identity;
	uint8_t en_rand1[8];
	uint8_t rand1_verify[4];
	int rc;

	printf("[5.1.1] Identity authentication\n");
	printf("  Prerequisite: identity authentication before business data exchange\n");
	printf("  Terminal -> Sensor: Rand1[4]\n");

	/* Flow step 1, Terminal internal operation: generate Rand1[4]. */
	rc = sc1777y_get_random4(dev, rand1);
	if (rc != 0) {
		return fail_step("5.1.1 get_random4", rc);
	}

	/*
	 * Flow step 2, Terminal-to-Sensor handoff:
	 * send Rand1[4].
	 */

	printf("  Sensor -> Terminal: sensorEsamID[8], Version[4], enRand1[8]\n");

	/* Flow step 3, Sensor internal operation: get sensorEsamID[8] and Version[4]. */
	rc = sc1777y_get_sensor_identity(dev, &sensor_identity);
	if (rc != 0) {
		return fail_step("5.1.1 get_sensor_identity", rc);
	}

	/* Flow step 4, Sensor internal operation: encrypt Rand1[4] into enRand1[8]. */
	rc = sc1777y_encrypt_sensor_challenge(dev, rand1, en_rand1);
	if (rc != 0) {
		return fail_step("5.1.1 encrypt_sensor_challenge", rc);
	}

	/*
	 * Flow step 5, Sensor-to-Terminal handoff:
	 * send sensorEsamID[8], Version[4], enRand1[8].
	 */

	/* Flow step 6, Terminal internal operation: verify sensorEsamID[8] and enRand1[8]. */
	rc = sc1777y_verify_sensor_auth(dev, SC1777Y_SENSOR_NEW, sensor_identity.serial,
					en_rand1, rand1_verify);
	if (rc != 0) {
		return fail_step("5.1.1 verify_sensor_auth", rc);
	}

	printf("  Terminal -> Sensor: AuthResult\n");
	printf("[5.1.1] PASS\n");
	return 0;
}
```

- [ ] **Step 2: Rewrite `run_5_1_2_business_data()`**

The function must use descriptive local variable names and no content checks:

```c
static int run_5_1_2_business_data(const struct device *dev)
{
	static const uint8_t sensor_esam_id[SC1777Y_SERIAL_LEN] = {
		'S', 'C', 23, 119, 0, 0, 0, 1
	};
	uint8_t data1[8];
	uint8_t en_data1[8];
	uint8_t data1_plain[8];
	uint8_t data2[8];
	uint8_t en_data2[8];
	uint8_t data2_plain[8];
	size_t out_len = 0U;
	int rc;

	printf("[5.1.2] Business data\n");
	printf("  Prerequisite: Terminal maps Sensor address to sensorEsamID[8]\n");
	printf("  Sensor -> Terminal: Data1[8] encrypted as enData1[8]\n");

	fill_example_bytes(data1, sizeof(data1), 0x10);

	/* Sensor-to-Terminal flow step 1, Sensor internal operation: encrypt Data1[8]. */
	rc = sc1777y_sensor_encrypt(dev, data1, sizeof(data1), en_data1,
				    sizeof(en_data1), &out_len);
	if (rc != 0) {
		return fail_step("5.1.2 sensor_encrypt", rc);
	}

	/*
	 * Sensor-to-Terminal flow step 2:
	 * Sensor sends enData1[8] to Terminal.
	 */

	/* Sensor-to-Terminal flow step 3, Terminal internal operation: decrypt enData1[8]. */
	rc = sc1777y_terminal_decrypt_sensor(dev, SC1777Y_SENSOR_NEW, sensor_esam_id,
					     en_data1, out_len, data1_plain,
					     sizeof(data1_plain), &out_len);
	if (rc != 0) {
		return fail_step("5.1.2 terminal_decrypt_sensor", rc);
	}

	printf("  Terminal -> Sensor: Data2[8] encrypted as enData2[8]\n");
	fill_example_bytes(data2, sizeof(data2), 0x20);

	/* Terminal-to-Sensor flow step 1, Terminal internal operation: encrypt Data2[8]. */
	rc = sc1777y_terminal_encrypt_sensor(dev, SC1777Y_SENSOR_NEW, sensor_esam_id,
					     data2, sizeof(data2), en_data2,
					     sizeof(en_data2), &out_len);
	if (rc != 0) {
		return fail_step("5.1.2 terminal_encrypt_sensor", rc);
	}

	/*
	 * Terminal-to-Sensor flow step 2:
	 * Terminal sends enData2[8] to Sensor.
	 */

	/* Terminal-to-Sensor flow step 3, Sensor internal operation: decrypt enData2[8]. */
	rc = sc1777y_sensor_decrypt_from_terminal(dev, en_data2, out_len, data2_plain,
						  sizeof(data2_plain), &out_len);
	if (rc != 0) {
		return fail_step("5.1.2 sensor_decrypt_from_terminal", rc);
	}

	printf("[5.1.2] PASS\n");
	return 0;
}
```

Add the full block comment above the function with:

```c
 * Prerequisites:
 * - Terminal should maintain the mapping between Sensor device address and
 *   sensorEsamID[8] before handling Sensor data.
 *
 * Data exchanged:
 * - Sensor -> Terminal: enData1[8]
 * - Terminal -> Sensor: enData2[8]
```

- [ ] **Step 3: Rewrite `run_5_2_1_key_update()`**

Use `Maintenance Software` in comments and logs:

```c
printf("[5.2.1] Key update/recovery\n");
printf("  Prerequisite: Maintenance Software has update/recovery USBKey and interface library\n");
printf("  Maintenance Software -> Terminal: identity request\n");
printf("  Terminal -> Maintenance Software: EsamID[8], Version[4], ERand1[8]\n");
printf("  Maintenance Software -> Terminal: enERand1[8]\n");
printf("  Terminal -> Maintenance Software: AuthResult, ERand2[8]\n");
printf("  Maintenance Software -> Terminal: KeyData[len]\n");
printf("  Terminal -> Maintenance Software: UpdateResult\n");
```

The function body must keep these driver calls and only check `rc`:

```c
rc = sc1777y_get_update_identity(dev, &update_identity);
rc = sc1777y_get_random8(dev, e_rand1);
rc = sc1777y_verify_update_auth(dev, en_e_rand1);
rc = sc1777y_get_random8(dev, e_rand2);
rc = sc1777y_apply_key_update(dev, key_data, sizeof(key_data));
```

Use these local variable names:

```c
struct sc1777y_identity update_identity;
uint8_t e_rand1[8];
uint8_t en_e_rand1[8];
uint8_t e_rand2[8];
uint8_t key_data[4];
```

Populate `en_e_rand1` and `key_data` with `fill_example_bytes()` before the
driver call that consumes them.

- [ ] **Step 4: Run sample build after non-platform flows**

Run:

```sh
env ZEPHYR_BASE=/home/imch/zephyrproject-v4.3.0/zephyr/.worktrees/sc1777y-driver ZEPHYR_TOOLCHAIN_VARIANT=zephyr ZEPHYR_SDK_INSTALL_DIR=/home/imch/zephyr-sdk-0.17.0 /home/imch/zephyrproject-v4.3.0/.venv/bin/python scripts/twister -T samples/drivers/sc1777y -p native_sim --inline-logs -O build/twister_sc1777y_sample_guide_task2
```

Expected: `1 of 1 executed test configurations passed (100.00%)`.

- [ ] **Step 5: Commit**

```sh
git add samples/drivers/sc1777y/src/main.c
git commit -m "samples: document sc1777y sensor update flows"
```

---

### Task 3: 重写 5.3.1、5.3.2、5.3.6 平台准备类流程

**Files:**
- Modify: `samples/drivers/sc1777y/src/main.c`

**Interfaces:**
- Consumes:
  - `fill_example_bytes(uint8_t *buf, size_t len, uint8_t seed)`
  - `fail_step(const char *step, int rc)`
- Produces:
  - Guide-style platform setup flows that only check `rc`

- [ ] **Step 1: Rewrite `run_5_3_1_platform_basic()`**

Add `Prerequisites:` and `Data exchanged:`:

```c
 * Prerequisites:
 * - Platform public key, AK, and IV material come from Platform or Platform
 *   configuration.
 *
 * Data exchanged:
 * - Terminal local operation: VersionInfo[64], Serial[8], Random[len]
 * - Platform -> Terminal: PlatformPublicKey[64], AK[16], IV[16]
```

Use logs:

```c
printf("[5.3.1] Platform basic instructions\n");
printf("  Prerequisite: Platform public key, AK[16], and IV[16] are available\n");
printf("  Terminal local operation: VersionInfo[64], Serial[8], Random[16]\n");
printf("  Platform -> Terminal: PlatformPublicKey[64], AK[16], IV[16]\n");
```

Keep these calls and only check `rc`:

```c
rc = sc1777y_get_version_info(dev, &version_info);
rc = sc1777y_get_serial(dev, serial);
rc = sc1777y_get_random(dev, random16, sizeof(random16));
rc = sc1777y_import_platform_public_key(dev, platform_public_key);
rc = sc1777y_import_ak(dev, ak);
rc = sc1777y_import_iv(dev, iv);
```

- [ ] **Step 2: Rewrite `run_5_3_2_certificate_request()`**

Add `Prerequisites:`:

```c
 * Prerequisites:
 * - Terminal should generate or confirm the local SM2 key pair before
 *   generating CSR data.
 * - Regenerating the key pair overwrites the old key pair and requires
 *   certificate re-enrollment.
 *
 * Data exchanged:
 * - Platform -> Terminal: certificate enrollment request
 * - Terminal -> Platform: Serial[8], CSR[len]
```

Use logs:

```c
printf("[5.3.2] Certificate request\n");
printf("  Prerequisite: local SM2 key pair exists before CSR generation\n");
printf("  Prerequisite: regenerating SM2 key pair requires certificate re-enrollment\n");
printf("  Platform -> Terminal: certificate enrollment request\n");
printf("  Terminal -> Platform: Serial[8], CSR[len]\n");
```

Keep these calls and only check `rc`:

```c
rc = sc1777y_generate_sm2_keypair(dev);
rc = sc1777y_get_serial(dev, serial);
rc = sc1777y_generate_cert_request(dev, SC1777Y_CERT_REQUEST_FORMAT_2, subject,
				   sizeof(subject), csr, sizeof(csr), &csr_len);
```

Do not check `csr_len` against emulator-specific prefix bytes.

- [ ] **Step 3: Rewrite `run_5_3_6_platform_type()`**

Add `Prerequisites:`:

```c
 * Prerequisites:
 * - Platform type should be selected before the 5.3.3 auth response step.
 *
 * Data exchanged:
 * - Terminal local configuration: PlatformType
```

Use logs:

```c
printf("[5.3.6] Platform type selection\n");
printf("  Prerequisite: select PlatformType before 5.3.3 auth response\n");
printf("  Terminal local configuration: PlatformType\n");
```

Keep calls and only check `rc`:

```c
rc = sc1777y_set_platform_type(dev, SC1777Y_PLATFORM_NANRUI);
rc = sc1777y_get_platform_type(dev, &platform_type);
```

Do not compare `platform_type` to a fixed expected value in the sample.

- [ ] **Step 4: Run sample build after platform setup flows**

Run:

```sh
env ZEPHYR_BASE=/home/imch/zephyrproject-v4.3.0/zephyr/.worktrees/sc1777y-driver ZEPHYR_TOOLCHAIN_VARIANT=zephyr ZEPHYR_SDK_INSTALL_DIR=/home/imch/zephyr-sdk-0.17.0 /home/imch/zephyrproject-v4.3.0/.venv/bin/python scripts/twister -T samples/drivers/sc1777y -p native_sim --inline-logs -O build/twister_sc1777y_sample_guide_task3
```

Expected: `1 of 1 executed test configurations passed (100.00%)`.

- [ ] **Step 5: Commit**

```sh
git add samples/drivers/sc1777y/src/main.c
git commit -m "samples: document sc1777y platform setup flows"
```

---

### Task 4: 重写 5.3.3、5.3.4、5.3.5 平台报文流程

**Files:**
- Modify: `samples/drivers/sc1777y/src/main.c`

**Interfaces:**
- Consumes:
  - `fill_example_bytes(uint8_t *buf, size_t len, uint8_t seed)`
  - `fail_step(const char *step, int rc)`
- Produces:
  - Platform message comments preserving PDF message format
  - Runtime logs that show field names and lengths

- [ ] **Step 1: Rewrite `run_5_3_3_session_negotiation()` comment**

The block comment must include:

```c
 * Prerequisites:
 * - Before session begin, Terminal certificate exists.
 * - Before session begin, Platform public key has been imported.
 * - Before auth response generation, Platform type has been selected.
 *
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

- [ ] **Step 2: Rewrite `run_5_3_3_session_negotiation()` body**

Use these logs:

```c
printf("[5.3.3] Session negotiation\n");
printf("  Prerequisite: terminal certificate exists\n");
printf("  Prerequisite: platform public key has been imported\n");
printf("  Prerequisite: platform type has been selected before auth response\n");
printf("  Terminal -> Platform: RequestMsg { DATA, RequestSign[64] }\n");
printf("    DATA { Type, SubType, Len, Ver, SN, SIM, ID, Cert1, EnR1[128] }\n");
printf("    Key driver data: EnR1[128], RequestHash[32], RequestSign[64]\n");
printf("  Platform -> Terminal: ResponseMsg { Type, SubType, Len, SN, AuthFactor[32], EnR2[128], ResponseSign[64] }\n");
printf("    Key driver data: AuthFactor[32], EnR2[128], ResponseHash[32], ResponseSign[64]\n");
printf("  Terminal -> Platform: ConfirmMsg { Type, SubType, Len, SN, AuthResult, DKHash[32] }\n");
printf("    Key driver data: AuthResult, DKHash[32]\n");
```

Keep these calls and only check `rc`:

```c
rc = sc1777y_session_begin(dev, en_r1);
rc = sc1777y_hash(dev, SC1777Y_HASH_REQUEST, request_body, sizeof(request_body),
		  request_hash);
rc = sc1777y_sign_hash(dev, request_hash, request_sign);
rc = sc1777y_hash(dev, SC1777Y_HASH_RESPONSE, response_body, sizeof(response_body),
		  response_hash);
rc = sc1777y_verify_signature(dev, response_hash, response_sign);
rc = sc1777y_generate_auth_response(dev, auth_factor, auth_response);
rc = sc1777y_session_confirm(dev, en_r2, dk_hash);
```

Use local variable names matching the PDF fields:

```c
uint8_t request_body[3] = {1, 2, 3};
uint8_t response_body[3] = {1, 2, 3};
uint8_t en_r1[SC1777Y_SESSION_RANDOM_LEN];
uint8_t en_r2[SC1777Y_SESSION_RANDOM_LEN];
uint8_t request_hash[SC1777Y_HASH_LEN];
uint8_t response_hash[SC1777Y_HASH_LEN];
uint8_t request_sign[SC1777Y_SIGNATURE_LEN];
uint8_t response_sign[SC1777Y_SIGNATURE_LEN];
uint8_t auth_factor[SC1777Y_AUTH_FACTOR_LEN];
uint8_t auth_response[SC1777Y_AUTH_RESPONSE_LEN];
uint8_t dk_hash[SC1777Y_SESSION_DKHASH_LEN];
```

Populate `response_sign`, `auth_factor`, and `en_r2` with
`fill_example_bytes()` before the driver calls that consume them.

- [ ] **Step 3: Rewrite `run_5_3_4_session_key_encryption()`**

The comment must include:

```c
 * Prerequisites:
 * - Session negotiation has succeeded.
 * - Plaintext length satisfies the 16-byte block requirement.
 *
 * Data exchanged:
 * - Terminal -> Platform:
 *   RequestMsg { Type, SubType, Len, IV[16], ResponseData[ciphertext] }
 *   Key driver data: IV[16], DATA[16], ResponseData[ciphertext]
```

Use logs:

```c
printf("[5.3.4] Session-key encryption\n");
printf("  Prerequisite: session negotiation has succeeded\n");
printf("  Prerequisite: plaintext length satisfies the 16-byte block requirement\n");
printf("  Terminal -> Platform: RequestMsg { Type, SubType, Len, IV[16], ResponseData[ciphertext] }\n");
printf("    Key driver data: IV[16], DATA[16], ResponseData[ciphertext]\n");
```

Keep calls and only check `rc`:

```c
rc = sc1777y_get_random(dev, iv, sizeof(iv));
rc = sc1777y_import_iv(dev, iv);
rc = sc1777y_session_encrypt(dev, plaintext, sizeof(plaintext), ciphertext,
			     sizeof(ciphertext), &ciphertext_len);
```

- [ ] **Step 4: Rewrite `run_5_3_5_session_key_decryption()`**

The comment must include:

```c
 * Prerequisites:
 * - Session negotiation has succeeded.
 * - Terminal has parsed IV[16] and RequestData[ciphertext] from Platform's
 *   message.
 *
 * Data exchanged:
 * - Platform -> Terminal:
 *   RequestMsg { Type, SubType, Len, IV[16], RequestData[ciphertext] }
 *   Key driver data: IV[16], RequestData[ciphertext], ResponseData[plaintext]
```

Use logs:

```c
printf("[5.3.5] Session-key decryption\n");
printf("  Prerequisite: session negotiation has succeeded\n");
printf("  Prerequisite: Terminal parsed IV[16] and RequestData[ciphertext]\n");
printf("  Platform -> Terminal: RequestMsg { Type, SubType, Len, IV[16], RequestData[ciphertext] }\n");
printf("    Key driver data: IV[16], RequestData[ciphertext], ResponseData[plaintext]\n");
```

Prepare example Platform message data locally:

```c
uint8_t iv[16];
uint8_t ciphertext[SC1777Y_BLOCK16_MIN_LEN];
uint8_t plaintext[SC1777Y_BLOCK16_MIN_LEN];
size_t plaintext_len = 0U;

fill_example_bytes(iv, sizeof(iv), 0xA0);
fill_example_bytes(ciphertext, sizeof(ciphertext), 0xB0);
```

Keep calls and only check `rc`:

```c
rc = sc1777y_import_iv(dev, iv);
rc = sc1777y_session_decrypt(dev, ciphertext, sizeof(ciphertext), plaintext,
			     sizeof(plaintext), &plaintext_len);
```

Do not compare decrypted plaintext with a fixed emulator value.

- [ ] **Step 5: Run sample build after platform message flows**

Run:

```sh
env ZEPHYR_BASE=/home/imch/zephyrproject-v4.3.0/zephyr/.worktrees/sc1777y-driver ZEPHYR_TOOLCHAIN_VARIANT=zephyr ZEPHYR_SDK_INSTALL_DIR=/home/imch/zephyr-sdk-0.17.0 /home/imch/zephyrproject-v4.3.0/.venv/bin/python scripts/twister -T samples/drivers/sc1777y -p native_sim --inline-logs -O build/twister_sc1777y_sample_guide_task4
```

Expected: `1 of 1 executed test configurations passed (100.00%)`.

- [ ] **Step 6: Commit**

```sh
git add samples/drivers/sc1777y/src/main.c
git commit -m "samples: document sc1777y platform message flows"
```

---

### Task 5: 样例元数据、最终清理和验证

**Files:**
- Modify: `samples/drivers/sc1777y/src/main.c`
- Modify: `samples/drivers/sc1777y/sample.yaml`

**Interfaces:**
- Consumes: guide-style sample from Tasks 1-4
- Produces: verified final sample guide

- [ ] **Step 1: Update sample name**

In `samples/drivers/sc1777y/sample.yaml`, change:

```yaml
sample:
  name: SC1777Y security chip sample
```

to:

```yaml
sample:
  name: SC1777Y driver flow guide
```

Keep the harness unchanged:

```yaml
harness_config:
  type: one_line
  regex:
    - "SC1777Y sample PASS"
```

- [ ] **Step 2: Delete remaining test-style helper code**

Confirm these functions no longer exist in `samples/drivers/sc1777y/src/main.c`:

```c
fail_check
expect_equal
expect_sequence
fill_xor
expect_default_identity
expect_default_serial
```

Run:

```sh
rg -n "fail_check|expect_equal|expect_sequence|fill_xor|expect_default_identity|expect_default_serial" samples/drivers/sc1777y/src/main.c
```

Expected: no matches.

- [ ] **Step 3: Confirm user-facing comments avoid forbidden phrasing**

Run:

```sh
rg -n "No driver call|security chip sample|Sensor-to-security-chip|Terminal-to-security-chip" samples/drivers/sc1777y samples/drivers/sc1777y/sample.yaml
```

Expected: no matches.

- [ ] **Step 4: Check whitespace and line length**

Run:

```sh
git diff --check
awk 'length($0) > 100 { print FNR ":" length($0) ":" $0 }' samples/drivers/sc1777y/src/main.c
```

Expected:

- `git diff --check` prints no output.
- Long lines are either absent or only unavoidable `printf` protocol-format lines. If long `printf` lines exist, split adjacent string literals.

- [ ] **Step 5: Run sample twister**

Run:

```sh
env ZEPHYR_BASE=/home/imch/zephyrproject-v4.3.0/zephyr/.worktrees/sc1777y-driver ZEPHYR_TOOLCHAIN_VARIANT=zephyr ZEPHYR_SDK_INSTALL_DIR=/home/imch/zephyr-sdk-0.17.0 /home/imch/zephyrproject-v4.3.0/.venv/bin/python scripts/twister -T samples/drivers/sc1777y -p native_sim --inline-logs -O build/twister_sc1777y_sample_guide_final
```

Expected:

```text
1 of 1 executed test configurations passed (100.00%)
1 of 1 executed test cases passed (100.00%)
```

- [ ] **Step 6: Run driver tests**

Run:

```sh
env ZEPHYR_BASE=/home/imch/zephyrproject-v4.3.0/zephyr/.worktrees/sc1777y-driver ZEPHYR_TOOLCHAIN_VARIANT=zephyr ZEPHYR_SDK_INSTALL_DIR=/home/imch/zephyr-sdk-0.17.0 /home/imch/zephyrproject-v4.3.0/.venv/bin/python scripts/twister -T tests/drivers/misc/sc1777y -p native_sim --inline-logs -O build/twister_sc1777y_driver_sample_guide_final
```

Expected:

```text
1 of 1 executed test configurations passed (100.00%)
83 of 83 executed test cases passed (100.00%)
```

- [ ] **Step 7: Commit**

```sh
git add samples/drivers/sc1777y/src/main.c samples/drivers/sc1777y/sample.yaml
git commit -m "samples: make sc1777y sample a flow guide"
```

---

## Self-Review Notes

- Spec coverage:
  - 样例职责边界由 Task 1 和 Task 5 覆盖。
  - 非平台字段交换由 Task 2 覆盖。
  - 平台准备流程和前置条件由 Task 3 覆盖。
  - 平台报文格式、`Key driver data`、会话协商和会话密钥流程由 Task 4 覆盖。
  - `sample.yaml` harness 和最终验证由 Task 5 覆盖。
- Placeholder scan:
  - 本计划不包含未完成占位标记。
  - 每个代码修改步骤都给出目标代码或明确替换内容。
- Type consistency:
  - 所有驱动调用使用当前 `include/zephyr/drivers/misc/sc1777y.h` 中已有 API。
  - `SC1777Y_*_LEN` 常量均来自现有公共头文件。
