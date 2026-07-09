/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>

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
 * Prerequisites:
 * - Platform public key, AK, and IV material come from Platform or Platform
 *   configuration.
 *
 * Data exchanged:
 * - Terminal local operation: VersionInfo[64], Serial[8], Random[len]
 * - Platform -> Terminal: PlatformPublicKey[64], AK[16], IV[16]
 */
static int run_5_3_1_platform_basic(const struct device *dev)
{
	struct sc1777y_version_info version_info;
	uint8_t serial[SC1777Y_SERIAL_LEN];
	uint8_t random16[16];
	uint8_t platform_key[SC1777Y_PLATFORM_PUBLIC_KEY_LEN];
	uint8_t ak[SC1777Y_AK_LEN];
	uint8_t iv[SC1777Y_IV_LEN];
	int rc;

	printf("[5.3.1] Platform basic instructions\n");
	printf("  Prerequisite: Platform public key, AK[16], and IV[16] are available\n");
	printf("  Terminal local operation: VersionInfo[64], Serial[8], Random[16]\n");
	printf("  Platform -> Terminal: PlatformPublicKey[64], AK[16], IV[16]\n");

	rc = sc1777y_get_version_info(dev, &version_info);
	if (rc != 0) {
		return fail_step("5.3.1 get_version_info", rc);
	}
	(void)version_info;

	rc = sc1777y_get_serial(dev, serial);
	if (rc != 0) {
		return fail_step("5.3.1 get_serial", rc);
	}
	(void)serial;

	rc = sc1777y_get_random(dev, random16, sizeof(random16));
	if (rc != 0) {
		return fail_step("5.3.1 get_random", rc);
	}
	(void)random16;

	fill_sequence(platform_key, sizeof(platform_key), 16);

	rc = sc1777y_import_platform_public_key(dev, platform_key);
	if (rc != 0) {
		return fail_step("5.3.1 import_platform_public_key", rc);
	}

	fill_sequence(ak, sizeof(ak), 32);

	rc = sc1777y_import_ak(dev, ak);
	if (rc != 0) {
		return fail_step("5.3.1 import_ak", rc);
	}

	fill_sequence(iv, sizeof(iv), 64);

	rc = sc1777y_import_iv(dev, iv);
	if (rc != 0) {
		return fail_step("5.3.1 import_iv", rc);
	}

	printf("[5.3.1] PASS\n");
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
 * Prerequisites:
 * - Terminal should generate or confirm the local SM2 key pair before
 *   generating CSR data.
 * - Regenerating the key pair overwrites the old key pair and requires
 *   certificate re-enrollment.
 *
 * Data exchanged:
 * - Platform -> Terminal: certificate enrollment request
 * - Terminal -> Platform: Serial[8], CSR[len]
 */
static int run_5_3_2_certificate_request(const struct device *dev)
{
	static const uint8_t subject[] = {'C', 'N', '='};
	uint8_t serial[SC1777Y_SERIAL_LEN];
	uint8_t csr[128];
	size_t csr_len;
	int rc;

	printf("[5.3.2] Certificate request\n");
	printf("  Prerequisite: local SM2 key pair exists before CSR generation\n");
	printf("  Prerequisite: regenerating SM2 key pair requires certificate re-enrollment\n");
	printf("  Platform -> Terminal: certificate enrollment request\n");
	printf("  Terminal -> Platform: Serial[8], CSR[len]\n");

	rc = sc1777y_generate_sm2_keypair(dev);
	if (rc != 0) {
		return fail_step("5.3.2 generate_sm2_keypair", rc);
	}

	rc = sc1777y_get_serial(dev, serial);
	if (rc != 0) {
		return fail_step("5.3.2 get_serial", rc);
	}
	(void)serial;

	rc = sc1777y_generate_cert_request(dev, SC1777Y_CERT_REQUEST_FORMAT_2, subject,
					   sizeof(subject), csr, sizeof(csr), &csr_len);
	if (rc != 0) {
		return fail_step("5.3.2 generate_cert_request", rc);
	}
	(void)csr;
	(void)csr_len;

	printf("[5.3.2] PASS\n");
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
 * Prerequisites:
 * - Platform type should be selected before the 5.3.3 auth response step.
 *
 * Data exchanged:
 * - Terminal local configuration: PlatformType
 */
static int run_5_3_6_platform_type(const struct device *dev)
{
	enum sc1777y_platform_type platform_type;
	int rc;

	printf("[5.3.6] Platform type selection\n");
	printf("  Prerequisite: select PlatformType before 5.3.3 auth response\n");
	printf("  Terminal local configuration: PlatformType\n");

	rc = sc1777y_set_platform_type(dev, SC1777Y_PLATFORM_NANRUI);
	if (rc != 0) {
		return fail_step("5.3.6 set_platform_type", rc);
	}

	rc = sc1777y_get_platform_type(dev, &platform_type);
	if (rc != 0) {
		return fail_step("5.3.6 get_platform_type", rc);
	}
	(void)platform_type;

	printf("[5.3.6] PASS\n");
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
 */
static int run_5_3_3_session_negotiation(const struct device *dev)
{
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
	int rc;

	printf("[5.3.3] Session negotiation\n");
	printf("  Prerequisite: terminal certificate exists\n");
	printf("  Prerequisite: platform public key has been imported\n");
	printf("  Prerequisite: platform type has been selected before auth response\n");
	printf("  Terminal -> Platform: RequestMsg { DATA, RequestSign[64] }\n");
	printf("    DATA { Type, SubType, Len, Ver, SN, SIM, ID, Cert1, EnR1[128] }\n");
	printf("    Key driver data: EnR1[128], RequestHash[32], RequestSign[64]\n");
	printf("  Platform -> Terminal: ResponseMsg { Type, SubType, Len, SN, "
	       "AuthFactor[32], EnR2[128], ResponseSign[64] }\n");
	printf("    Key driver data: AuthFactor[32], EnR2[128], ResponseHash[32], ResponseSign[64]\n");
	printf("  Terminal -> Platform: ConfirmMsg { Type, SubType, Len, SN, AuthResult, DKHash[32] }\n");
	printf("    Key driver data: AuthResult, DKHash[32]\n");

	rc = sc1777y_session_begin(dev, en_r1);
	if (rc != 0) {
		return fail_step("5.3.3 session_begin", rc);
	}

	rc = sc1777y_hash(dev, SC1777Y_HASH_REQUEST, request_body, sizeof(request_body),
			  request_hash);
	if (rc != 0) {
		return fail_step("5.3.3 request_hash", rc);
	}

	rc = sc1777y_sign_hash(dev, request_hash, request_sign);
	if (rc != 0) {
		return fail_step("5.3.3 sign_hash", rc);
	}

	fill_example_bytes(response_sign, sizeof(response_sign), 0x60);
	fill_example_bytes(auth_factor, sizeof(auth_factor), 0x70);
	fill_example_bytes(en_r2, sizeof(en_r2), 0x80);

	rc = sc1777y_hash(dev, SC1777Y_HASH_RESPONSE, response_body, sizeof(response_body),
			  response_hash);
	if (rc != 0) {
		return fail_step("5.3.3 response_hash", rc);
	}

	rc = sc1777y_verify_signature(dev, response_hash, response_sign);
	if (rc != 0) {
		return fail_step("5.3.3 verify_signature", rc);
	}

	rc = sc1777y_generate_auth_response(dev, auth_factor, auth_response);
	if (rc != 0) {
		return fail_step("5.3.3 generate_auth_response", rc);
	}

	rc = sc1777y_session_confirm(dev, en_r2, dk_hash);
	if (rc != 0) {
		return fail_step("5.3.3 session_confirm", rc);
	}

	printf("[5.3.3] PASS\n");
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
 * Prerequisites:
 * - Session negotiation has succeeded.
 * - Plaintext length satisfies the 16-byte block requirement.
 *
 * Data exchanged:
 * - Terminal -> Platform:
 *   RequestMsg { Type, SubType, Len, IV[16], ResponseData[ciphertext] }
 *   Key driver data: IV[16], DATA[16], ResponseData[ciphertext]
 */
static int run_5_3_4_session_key_encryption(const struct device *dev)
{
	uint8_t iv[16];
	uint8_t plaintext[SC1777Y_BLOCK16_MIN_LEN];
	uint8_t ciphertext[SC1777Y_BLOCK16_MIN_LEN];
	size_t ciphertext_len = 0U;
	int rc;

	printf("[5.3.4] Session-key encryption\n");
	printf("  Prerequisite: session negotiation has succeeded\n");
	printf("  Prerequisite: plaintext length satisfies the 16-byte block requirement\n");
	printf("  Terminal -> Platform: RequestMsg { Type, SubType, Len, "
	       "IV[16], ResponseData[ciphertext] }\n");
	printf("    Key driver data: IV[16], DATA[16], ResponseData[ciphertext]\n");

	rc = sc1777y_get_random(dev, iv, sizeof(iv));
	if (rc != 0) {
		return fail_step("5.3.4 get_random", rc);
	}

	rc = sc1777y_import_iv(dev, iv);
	if (rc != 0) {
		return fail_step("5.3.4 import_iv", rc);
	}

	fill_example_bytes(plaintext, sizeof(plaintext), 0x90);

	rc = sc1777y_session_encrypt(dev, plaintext, sizeof(plaintext), ciphertext,
				     sizeof(ciphertext), &ciphertext_len);
	if (rc != 0) {
		return fail_step("5.3.4 session_encrypt", rc);
	}
	(void)ciphertext_len;

	printf("[5.3.4] PASS\n");
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
 * Prerequisites:
 * - Session negotiation has succeeded.
 * - Terminal has parsed IV[16] and RequestData[ciphertext] from Platform's
 *   message.
 *
 * Data exchanged:
 * - Platform -> Terminal:
 *   RequestMsg { Type, SubType, Len, IV[16], RequestData[ciphertext] }
 *   Key driver data: IV[16], RequestData[ciphertext], ResponseData[plaintext]
 */
static int run_5_3_5_session_key_decryption(const struct device *dev)
{
	uint8_t iv[16];
	uint8_t ciphertext[SC1777Y_BLOCK16_MIN_LEN];
	uint8_t plaintext[SC1777Y_BLOCK16_MIN_LEN];
	size_t plaintext_len = 0U;
	int rc;

	printf("[5.3.5] Session-key decryption\n");
	printf("  Prerequisite: session negotiation has succeeded\n");
	printf("  Prerequisite: Terminal parsed IV[16] and RequestData[ciphertext]\n");
	printf("  Platform -> Terminal: RequestMsg { Type, SubType, Len, "
	       "IV[16], RequestData[ciphertext] }\n");
	printf("    Key driver data: IV[16], RequestData[ciphertext], ResponseData[plaintext]\n");

	fill_example_bytes(iv, sizeof(iv), 0xA0);
	fill_example_bytes(ciphertext, sizeof(ciphertext), 0xB0);

	rc = sc1777y_import_iv(dev, iv);
	if (rc != 0) {
		return fail_step("5.3.5 import_iv", rc);
	}

	rc = sc1777y_session_decrypt(dev, ciphertext, sizeof(ciphertext), plaintext,
				     sizeof(plaintext), &plaintext_len);
	if (rc != 0) {
		return fail_step("5.3.5 session_decrypt", rc);
	}
	(void)plaintext_len;

	printf("[5.3.5] PASS\n");
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
