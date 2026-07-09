/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/misc/sc1777y.h>

#define SC1777Y_NODE DT_ALIAS(sc1777y_0)

static int fail_step(const char *step, int rc)
{
	printf("%s failed: %d\n", step, rc);
	printf("SC1777Y sample FAIL\n");
	return 1;
}

static int fail_check(const char *step)
{
	printf("%s check failed\n", step);
	printf("SC1777Y sample FAIL\n");
	return 1;
}

static void fill_example_bytes(uint8_t *buf, size_t len, uint8_t seed)
{
	for (size_t i = 0; i < len; i++) {
		buf[i] = seed + (uint8_t)i;
	}
}

static void fill_sequence(uint8_t *buf, size_t len, uint8_t start)
{
	for (size_t i = 0; i < len; i++) {
		buf[i] = start + (uint8_t)i;
	}
}

static void fill_xor(uint8_t *dst, const uint8_t *src, size_t len, uint8_t mask)
{
	for (size_t i = 0; i < len; i++) {
		dst[i] = src[i] ^ mask;
	}
}

static bool expect_equal(const uint8_t *actual, const uint8_t *expected, size_t len)
{
	return memcmp(actual, expected, len) == 0;
}

static bool expect_sequence(const uint8_t *actual, size_t len, uint8_t start)
{
	for (size_t i = 0; i < len; i++) {
		if (actual[i] != (uint8_t)(start + i)) {
			return false;
		}
	}

	return true;
}

static bool expect_default_serial(const uint8_t value[SC1777Y_SERIAL_LEN])
{
	static const uint8_t expected_serial[SC1777Y_SERIAL_LEN] = {
		'S', 'C', 23, 119, 0, 0, 0, 1
	};

	return expect_equal(value, expected_serial, sizeof(expected_serial));
}

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

/*
 * 5.1.2 Business data flow
 *
 * Flow:
 *   Sensor sends business data to Terminal:
 * +--------+      Data1/enData1      +----------+
 * | Sensor |------------------------>| Terminal |
 * | user   |                         | user     |
 * +--------+                         +----------+
 *
 *   Terminal sends business data to Sensor:
 * +----------+      Data2/enData2      +--------+
 * | Terminal |------------------------>| Sensor |
 * | user     |                         | user   |
 * +----------+                         +--------+
 *
 * Prerequisites:
 * - Terminal should maintain the mapping between Sensor device address and
 *   sensorEsamID[8] before handling Sensor data.
 *
 * Data exchanged:
 * - Sensor -> Terminal: enData1[8]
 * - Terminal -> Sensor: enData2[8]
 *
 * Guide:
 * The sender encrypts before sending.  The receiver decrypts after receiving.
 * Terminal-side operations that target Sensor data must use the matching
 * sensorEsamID.
 */
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

/*
 * 5.2.1 Key update/recovery flow
 *
 * Flow:
 * +----------------------+                               +----------+
 * | Maintenance Software |------------------------------>| Terminal |
 * |                      | identity request              |          |
 * |                      |<------------------------------|          |
 * |                      | EsamID[8], Version[4],        |          |
 * |                      | ERand1[8]                     |          |
 * |                      |------------------------------>|          |
 * |                      | enERand1[8]                   |          |
 * |                      |<------------------------------|          |
 * |                      | AuthResult, ERand2[8]         |          |
 * |                      |------------------------------>|          |
 * |                      | KeyData[len]                  |          |
 * |                      |<------------------------------|          |
 * |                      | UpdateResult                  |          |
 * +----------------------+------------------------------>|          |
 *                                                        +----------+
 *
 * Guide:
 * Terminal exposes identity and random material. Maintenance Software owns
 * update policy, encrypted authentication data, and KeyData generation.
 * Terminal verifies the encrypted random value and applies the KeyData package.
 */
static int run_5_2_1_key_update(const struct device *dev)
{
	struct sc1777y_identity update_identity;
	uint8_t e_rand1[8];
	uint8_t en_e_rand1[8];
	uint8_t e_rand2[8];
	uint8_t key_data[4];
	int rc;

	printf("[5.2.1] Key update/recovery\n");
	printf("  Prerequisite: Maintenance Software has update/recovery USBKey and interface library\n");
	printf("  Maintenance Software -> Terminal: identity request\n");
	printf("  Terminal -> Maintenance Software: EsamID[8], Version[4], ERand1[8]\n");
	printf("  Maintenance Software -> Terminal: enERand1[8]\n");
	printf("  Terminal -> Maintenance Software: AuthResult, ERand2[8]\n");
	printf("  Maintenance Software -> Terminal: KeyData[len]\n");
	printf("  Terminal -> Maintenance Software: UpdateResult\n");

	rc = sc1777y_get_update_identity(dev, &update_identity);
	if (rc != 0) {
		return fail_step("5.2.1 get_update_identity", rc);
	}

	rc = sc1777y_get_random8(dev, e_rand1);
	if (rc != 0) {
		return fail_step("5.2.1 get_random8 auth", rc);
	}

	fill_example_bytes(en_e_rand1, sizeof(en_e_rand1), 0x30);
	rc = sc1777y_verify_update_auth(dev, en_e_rand1);
	if (rc != 0) {
		return fail_step("5.2.1 verify_update_auth", rc);
	}

	rc = sc1777y_get_random8(dev, e_rand2);
	if (rc != 0) {
		return fail_step("5.2.1 get_random8 package", rc);
	}

	fill_example_bytes(key_data, sizeof(key_data), 0x40);
	rc = sc1777y_apply_key_update(dev, key_data, sizeof(key_data));
	if (rc != 0) {
		return fail_step("5.2.1 apply_key_update", rc);
	}

	printf("[5.2.1] PASS\n");
	return 0;
}

/*
 * 5.3.1 Platform basic instructions
 *
 * Flow:
 * +----------+                         +----------+
 * | Terminal |<----------------------->| Platform |
 * | user     |  public key, AK, IV,    | user     |
 * |          |  version, serial, rand  |          |
 * +----------+                         +----------+
 *
 * Guide:
 * Terminal reads local version, serial, and random bytes.  Platform provides
 * the public key, AK, and IV material that Terminal imports before platform
 * access workflows.
 */
static int run_5_3_1_platform_basic(const struct device *dev)
{
	struct sc1777y_version_info version_info;
	uint8_t version_expected[SC1777Y_VERSION_INFO_LEN];
	uint8_t serial[SC1777Y_SERIAL_LEN];
	uint8_t random_buf[16];
	uint8_t platform_key[SC1777Y_PLATFORM_PUBLIC_KEY_LEN];
	uint8_t ak[SC1777Y_AK_LEN];
	uint8_t iv[SC1777Y_IV_LEN];
	int rc;

	/* Flow item 1, Terminal internal security-chip operation: read version information. */
	rc = sc1777y_get_version_info(dev, &version_info);
	if (rc != 0) {
		return fail_step("5.3.1 get_version_info", rc);
	}
	fill_sequence(version_expected, sizeof(version_expected), 48);
	if (!expect_equal(version_info.bytes, version_expected, sizeof(version_expected))) {
		return fail_check("5.3.1 get_version_info");
	}

	/* Flow item 2, Terminal internal security-chip operation: read unique chip serial. */
	rc = sc1777y_get_serial(dev, serial);
	if (rc != 0) {
		return fail_step("5.3.1 get_serial", rc);
	}
	if (!expect_default_serial(serial)) {
		return fail_check("5.3.1 get_serial");
	}

	/* Flow item 3, Terminal internal security-chip operation: generate caller-sized random bytes. */
	rc = sc1777y_get_random(dev, random_buf, sizeof(random_buf));
	if (rc != 0) {
		return fail_step("5.3.1 get_random", rc);
	}
	if (!expect_sequence(random_buf, sizeof(random_buf), 160)) {
		return fail_check("5.3.1 get_random");
	}

	/*
	 * Flow item 4, Platform-to-Terminal handoff: provide Platform public key.
	 * No driver call is needed for this user-to-user message.
	 */
	fill_sequence(platform_key, sizeof(platform_key), 16);

	/* Flow item 5, Terminal internal security-chip operation: import Platform public key. */
	rc = sc1777y_import_platform_public_key(dev, platform_key);
	if (rc != 0) {
		return fail_step("5.3.1 import_platform_public_key", rc);
	}

	/*
	 * Flow item 6, Platform-to-Terminal handoff: provide symmetric key AK.
	 * No driver call is needed for this user-to-user message.
	 */
	fill_sequence(ak, sizeof(ak), 32);

	/* Flow item 7, Terminal internal security-chip operation: import symmetric key AK. */
	rc = sc1777y_import_ak(dev, ak);
	if (rc != 0) {
		return fail_step("5.3.1 import_ak", rc);
	}

	/*
	 * Flow item 8, Platform-to-Terminal handoff: provide IV material.
	 * No driver call is needed for this user-to-user message.
	 */
	fill_sequence(iv, sizeof(iv), 64);

	/* Flow item 9, Terminal internal security-chip operation: import IV. */
	rc = sc1777y_import_iv(dev, iv);
	if (rc != 0) {
		return fail_step("5.3.1 import_iv", rc);
	}

	printf("5.3.1 platform basic PASS\n");
	return 0;
}

/*
 * 5.3.2 Certificate request flow
 *
 * Flow:
 * +----------+      certificate request       +----------+
 * | Platform |------------------------------->| Terminal |
 * | user     |                                | user     |
 * |          |<-------------------------------|          |
 * +----------+      serial + CSR bytes        +----------+
 *
 * Guide:
 * Platform asks Terminal for certificate enrollment material.  Terminal
 * creates local SM2 key material, reads its serial, generates CSR bytes, and
 * returns serial plus CSR bytes to Platform.
 */
static int run_5_3_2_certificate_request(const struct device *dev)
{
	static const uint8_t subject[] = {'C', 'N', '='};
	static const char cert_prefix[] = "SC1777Y-CERT-REQUEST:";
	uint8_t serial[SC1777Y_SERIAL_LEN];
	uint8_t cert_request[128];
	size_t cert_request_len;
	int rc;

	/*
	 * Flow step 1, Platform-to-Terminal request: ask for certificate
	 * enrollment material. No driver call is needed for this message.
	 */

	/*
	 * Flow step 2, Terminal preparation: reset or prepare the local device
	 * before generating key material. Device readiness represents this in the sample.
	 */

	/* Flow step 3, Terminal internal security-chip operation: generate SM2 keypair. */
	rc = sc1777y_generate_sm2_keypair(dev);
	if (rc != 0) {
		return fail_step("5.3.2 generate_sm2_keypair", rc);
	}

	/* Flow step 4, Terminal internal security-chip operation: read unique chip serial. */
	rc = sc1777y_get_serial(dev, serial);
	if (rc != 0) {
		return fail_step("5.3.2 get_serial", rc);
	}
	if (!expect_default_serial(serial)) {
		return fail_check("5.3.2 get_serial");
	}

	/* Flow step 5, Terminal internal security-chip operation: generate CSR from subject. */
	rc = sc1777y_generate_cert_request(dev, SC1777Y_CERT_REQUEST_FORMAT_2, subject,
					   sizeof(subject), cert_request,
					   sizeof(cert_request), &cert_request_len);
	if (rc != 0) {
		return fail_step("5.3.2 generate_cert_request", rc);
	}
	if (cert_request_len != (sizeof(cert_prefix) - 1U + sizeof(subject)) ||
	    memcmp(cert_request, cert_prefix, sizeof(cert_prefix) - 1U) != 0 ||
	    memcmp(&cert_request[sizeof(cert_prefix) - 1U], subject, sizeof(subject)) != 0) {
		return fail_check("5.3.2 generate_cert_request");
	}

	/*
	 * Flow step 6, Terminal-to-Platform handoff: send serial and CSR bytes.
	 * No driver call is needed for this user-to-user message.
	 */

	printf("5.3.2 certificate request PASS\n");
	return 0;
}

/*
 * 5.3.6 Platform type selection flow
 *
 * Flow:
 * +----------+      selected platform type      +----------+
 * | Terminal |---------------------------------->| Platform |
 * | user     |                                   | user     |
 * +----------+                                   +----------+
 *
 * Guide:
 * Terminal selects which Platform family it will access.  The PDF lists this
 * as 5.3.6, while 5.3.3 step 9 depends on this selection.
 */
static int run_5_3_6_platform_type(const struct device *dev)
{
	enum sc1777y_platform_type platform_type;
	int rc;

	/*
	 * Flow item 1, Terminal configuration: choose the target Platform family.
	 * No driver call is needed until the selected type is applied.
	 */

	/* Flow item 2, Terminal internal security-chip operation: set platform type. */
	rc = sc1777y_set_platform_type(dev, SC1777Y_PLATFORM_NANRUI);
	if (rc != 0) {
		return fail_step("5.3.6 set_platform_type", rc);
	}

	/* Flow item 3, Terminal internal security-chip operation: read platform type. */
	rc = sc1777y_get_platform_type(dev, &platform_type);
	if (rc != 0) {
		return fail_step("5.3.6 get_platform_type", rc);
	}
	if (platform_type != SC1777Y_PLATFORM_NANRUI) {
		return fail_check("5.3.6 get_platform_type");
	}

	/*
	 * Flow item 4, Terminal configuration result: use this Platform family
	 * for later session-authentication operations.
	 */

	printf("5.3.6 platform type PASS\n");
	return 0;
}

/*
 * 5.3.3 Session negotiation flow
 *
 * Flow:
 * +----------+       RequestMsg        +----------+
 * | Terminal |------------------------>| Platform |
 * | user     |                         | user     |
 * |          |<------------------------|          |
 * |          |       ResponseMsg       |          |
 * |          |------------------------>|          |
 * +----------+       ConfirmMsg        +----------+
 *
 * Guide:
 * Terminal builds request and confirmation messages.  Platform verifies the
 * request and returns AuthFactor, EnR2, and ResponseSign.  Terminal verifies
 * the response and confirms the session.
 */
static int run_5_3_3_session_negotiation(const struct device *dev)
{
	uint8_t hash_input[3] = {1, 2, 3};
	uint8_t session_random[SC1777Y_SESSION_RANDOM_LEN];
	uint8_t session_peer_random[SC1777Y_SESSION_RANDOM_LEN];
	uint8_t request_hash[SC1777Y_HASH_LEN];
	uint8_t response_hash[SC1777Y_HASH_LEN];
	uint8_t hash_expected[SC1777Y_HASH_LEN];
	uint8_t signature[SC1777Y_SIGNATURE_LEN];
	uint8_t signature_expected[SC1777Y_SIGNATURE_LEN];
	uint8_t auth_factor[SC1777Y_AUTH_FACTOR_LEN];
	uint8_t auth_response[SC1777Y_AUTH_RESPONSE_LEN];
	uint8_t auth_response_expected[SC1777Y_AUTH_RESPONSE_LEN];
	uint8_t dkhash[SC1777Y_SESSION_DKHASH_LEN];
	uint8_t dkhash_expected[SC1777Y_SESSION_DKHASH_LEN];
	int rc;

	/* Flow step 1, Terminal internal security-chip operation: begin session and get EnR1. */
	rc = sc1777y_session_begin(dev, session_random);
	if (rc != 0) {
		return fail_step("5.3.3 session_begin", rc);
	}
	if (!expect_sequence(session_random, sizeof(session_random), 192)) {
		return fail_check("5.3.3 session_begin");
	}

	/* Flow step 2, Terminal internal security-chip operation: hash request message body. */
	rc = sc1777y_hash(dev, SC1777Y_HASH_REQUEST, hash_input, sizeof(hash_input),
			  request_hash);
	if (rc != 0) {
		return fail_step("5.3.3 request_hash", rc);
	}
	fill_sequence(hash_expected, sizeof(hash_expected), 208);
	if (!expect_equal(request_hash, hash_expected, sizeof(hash_expected))) {
		return fail_check("5.3.3 request_hash");
	}

	/* Flow step 3, Terminal internal security-chip operation: sign request hash. */
	rc = sc1777y_sign_hash(dev, request_hash, signature);
	if (rc != 0) {
		return fail_step("5.3.3 sign_hash", rc);
	}
	fill_sequence(signature_expected, sizeof(signature_expected), 224);
	if (!expect_equal(signature, signature_expected, sizeof(signature_expected))) {
		return fail_check("5.3.3 sign_hash");
	}

	/*
	 * Flow step 4, Terminal-to-Platform handoff: build RequestMsg and send it.
	 * No driver call is needed for this user-to-user message.
	 */

	/*
	 * Flow step 5, Platform operation: parse and verify RequestMsg, then
	 * return ResponseMsg. No Terminal driver call is needed here.
	 */

	/*
	 * Flow step 6, Terminal operation: receive and parse ResponseMsg before
	 * checking it. No driver call is needed for parsing the message fields.
	 */

	/* Flow step 7, Terminal internal security-chip operation: hash Platform response body. */
	rc = sc1777y_hash(dev, SC1777Y_HASH_RESPONSE, hash_input, sizeof(hash_input),
			  response_hash);
	if (rc != 0) {
		return fail_step("5.3.3 response_hash", rc);
	}
	if (!expect_equal(response_hash, hash_expected, sizeof(hash_expected))) {
		return fail_check("5.3.3 response_hash");
	}

	/* Flow step 8, Terminal internal security-chip operation: verify Platform signature. */
	rc = sc1777y_verify_signature(dev, response_hash, signature);
	if (rc != 0) {
		return fail_step("5.3.3 verify_signature", rc);
	}

	fill_sequence(auth_factor, sizeof(auth_factor), 81);

	/* Flow step 9, Terminal internal security-chip operation: generate auth response data. */
	rc = sc1777y_generate_auth_response(dev, auth_factor, auth_response);
	if (rc != 0) {
		return fail_step("5.3.3 generate_auth_response", rc);
	}
	fill_sequence(auth_response_expected, sizeof(auth_response_expected), 112);
	if (!expect_equal(auth_response, auth_response_expected,
			  sizeof(auth_response_expected))) {
		return fail_check("5.3.3 generate_auth_response");
	}

	fill_sequence(session_peer_random, sizeof(session_peer_random), 113);

	/* Flow step 10, Terminal internal security-chip operation: confirm EnR2 and get DKHash. */
	rc = sc1777y_session_confirm(dev, session_peer_random, dkhash);
	if (rc != 0) {
		return fail_step("5.3.3 session_confirm", rc);
	}
	fill_sequence(dkhash_expected, sizeof(dkhash_expected), 144);
	if (!expect_equal(dkhash, dkhash_expected, sizeof(dkhash_expected))) {
		return fail_check("5.3.3 session_confirm");
	}

	/*
	 * Flow step 11, Terminal-to-Platform handoff: build ConfirmMsg and send it.
	 * No driver call is needed for this user-to-user message.
	 */

	/*
	 * Flow step 12, Platform operation: parse and verify ConfirmMsg.
	 * No Terminal driver call is needed here.
	 */

	printf("5.3.3 session negotiation PASS\n");
	return 0;
}

/*
 * 5.3.4 Session-key encryption flow
 *
 * Flow:
 * +----------+      IV + ciphertext      +----------+
 * | Terminal |-------------------------->| Platform |
 * | user     |                           | user     |
 * +----------+                           +----------+
 *
 * Guide:
 * Terminal prepares plaintext, generates IV material, encrypts session data,
 * and sends IV plus ciphertext to Platform.
 */
static int run_5_3_4_session_key_encryption(const struct device *dev)
{
	uint8_t random_buf[16];
	uint8_t session_block[SC1777Y_BLOCK16_MIN_LEN];
	uint8_t session_out[SC1777Y_BLOCK16_MIN_LEN];
	uint8_t session_expected[SC1777Y_BLOCK16_MIN_LEN];
	size_t out_len = 0U;
	int rc;

	/* Flow step 1, Terminal internal security-chip operation: generate 16-byte IV random. */
	rc = sc1777y_get_random(dev, random_buf, sizeof(random_buf));
	if (rc != 0) {
		return fail_step("5.3.4 get_random", rc);
	}
	if (!expect_sequence(random_buf, sizeof(random_buf), 160)) {
		return fail_check("5.3.4 get_random");
	}

	/* Flow step 2, Terminal internal security-chip operation: import IV random. */
	rc = sc1777y_import_iv(dev, random_buf);
	if (rc != 0) {
		return fail_step("5.3.4 import_iv", rc);
	}

	session_block[0] = 1;
	session_block[1] = 17;
	session_block[2] = 33;
	session_block[3] = 49;
	session_block[4] = 65;
	session_block[5] = 81;
	session_block[6] = 97;
	session_block[7] = 113;
	session_block[8] = 129;
	session_block[9] = 145;
	session_block[10] = 161;
	session_block[11] = 177;
	session_block[12] = 193;
	session_block[13] = 209;
	session_block[14] = 225;
	session_block[15] = 241;

	/* Flow step 3, Terminal internal security-chip operation: encrypt session payload. */
	rc = sc1777y_session_encrypt(dev, session_block, sizeof(session_block), session_out,
				     sizeof(session_out), &out_len);
	if (rc != 0) {
		return fail_step("5.3.4 session_encrypt", rc);
	}
	fill_xor(session_expected, session_block, sizeof(session_block), 165);
	if (out_len != sizeof(session_block) ||
	    !expect_equal(session_out, session_expected, sizeof(session_expected))) {
		return fail_check("5.3.4 session_encrypt");
	}

	/*
	 * Flow step 4, Terminal-to-Platform handoff: build encrypted RequestMsg
	 * with IV and ciphertext. No driver call is needed for this message.
	 */

	/*
	 * Flow step 5, Platform operation: receive, parse, verify, and process
	 * the encrypted RequestMsg. No Terminal driver call is needed here.
	 */

	printf("5.3.4 session-key encryption PASS\n");
	return 0;
}

/*
 * 5.3.5 Session-key decryption flow
 *
 * Flow:
 * +----------+      IV + ciphertext      +----------+
 * | Platform |-------------------------->| Terminal |
 * | user     |                           | user     |
 * +----------+                           +----------+
 *
 * Guide:
 * Platform sends IV plus ciphertext.  Terminal parses the incoming message,
 * imports IV material, and decrypts the session payload.
 */
static int run_5_3_5_session_key_decryption(const struct device *dev)
{
	uint8_t random_buf[16];
	uint8_t session_block[SC1777Y_BLOCK16_MIN_LEN];
	uint8_t session_out[SC1777Y_BLOCK16_MIN_LEN];
	uint8_t session_expected[SC1777Y_BLOCK16_MIN_LEN];
	size_t out_len = 0U;
	int rc;

	fill_sequence(random_buf, sizeof(random_buf), 160);
	session_block[0] = 1;
	session_block[1] = 17;
	session_block[2] = 33;
	session_block[3] = 49;
	session_block[4] = 65;
	session_block[5] = 81;
	session_block[6] = 97;
	session_block[7] = 113;
	session_block[8] = 129;
	session_block[9] = 145;
	session_block[10] = 161;
	session_block[11] = 177;
	session_block[12] = 193;
	session_block[13] = 209;
	session_block[14] = 225;
	session_block[15] = 241;
	fill_xor(session_out, session_block, sizeof(session_block), 165);

	/*
	 * Flow step 1, Platform-to-Terminal handoff: build encrypted RequestMsg
	 * with IV and ciphertext, then send it. No Terminal driver call is needed.
	 */

	/*
	 * Flow step 2, Terminal operation: receive and parse encrypted RequestMsg
	 * to obtain IV and ciphertext. No driver call is needed for parsing.
	 */

	/* Flow step 3, Terminal internal security-chip operation: import received IV. */
	rc = sc1777y_import_iv(dev, random_buf);
	if (rc != 0) {
		return fail_step("5.3.5 import_iv", rc);
	}

	/* Flow step 4, Terminal internal security-chip operation: decrypt received ciphertext. */
	rc = sc1777y_session_decrypt(dev, session_out, sizeof(session_out), session_expected,
				     sizeof(session_expected), &out_len);
	if (rc != 0) {
		return fail_step("5.3.5 session_decrypt", rc);
	}
	if (out_len != sizeof(session_block) ||
	    !expect_equal(session_expected, session_block, sizeof(session_block))) {
		return fail_check("5.3.5 session_decrypt");
	}

	printf("5.3.5 session-key decryption PASS\n");
	return 0;
}

int main(void)
{
	const struct device *dev = DEVICE_DT_GET(SC1777Y_NODE);

	if (!device_is_ready(dev)) {
		printf("SC1777Y device not ready\n");
		printf("SC1777Y sample FAIL\n");
		return 1;
	}

	if (run_5_1_1_identity_auth(dev) != 0) {
		return 1;
	}
	if (run_5_1_2_business_data(dev) != 0) {
		return 1;
	}
	if (run_5_2_1_key_update(dev) != 0) {
		return 1;
	}
	if (run_5_3_1_platform_basic(dev) != 0) {
		return 1;
	}
	if (run_5_3_2_certificate_request(dev) != 0) {
		return 1;
	}
	if (run_5_3_6_platform_type(dev) != 0) {
		return 1;
	}
	if (run_5_3_3_session_negotiation(dev) != 0) {
		return 1;
	}
	if (run_5_3_4_session_key_encryption(dev) != 0) {
		return 1;
	}
	if (run_5_3_5_session_key_decryption(dev) != 0) {
		return 1;
	}

	printf("SC1777Y sample PASS\n");
	return 0;
}
