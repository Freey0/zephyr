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

static uint8_t random_buf[16];
static struct sc1777y_identity sensor_identity;
static uint8_t sensor_challenge[8];
static uint8_t sensor_rand[4];
static uint8_t sensor_block[8];
static uint8_t sensor_out[16];
static struct sc1777y_identity update_identity;
static uint8_t version_expected[SC1777Y_VERSION_INFO_LEN];
static struct sc1777y_version_info version_info;
static uint8_t serial[SC1777Y_SERIAL_LEN];
static uint8_t platform_key[SC1777Y_PLATFORM_PUBLIC_KEY_LEN];
static uint8_t ak[SC1777Y_AK_LEN];
static uint8_t iv[SC1777Y_IV_LEN];
static uint8_t cert_request[128];
static size_t cert_request_len;
static uint8_t session_random[SC1777Y_SESSION_RANDOM_LEN];
static uint8_t hash_input[3];
static uint8_t hash_out[SC1777Y_HASH_LEN];
static uint8_t hash_expected[SC1777Y_HASH_LEN];
static uint8_t signature[SC1777Y_SIGNATURE_LEN];
static uint8_t signature_expected[SC1777Y_SIGNATURE_LEN];
static uint8_t auth_factor[SC1777Y_AUTH_FACTOR_LEN];
static uint8_t auth_response[SC1777Y_AUTH_RESPONSE_LEN];
static uint8_t auth_response_expected[SC1777Y_AUTH_RESPONSE_LEN];
static uint8_t session_peer_random[SC1777Y_SESSION_RANDOM_LEN];
static uint8_t dkhash[SC1777Y_SESSION_DKHASH_LEN];
static uint8_t dkhash_expected[SC1777Y_SESSION_DKHASH_LEN];
static uint8_t session_block[SC1777Y_BLOCK16_MIN_LEN];
static uint8_t session_out[SC1777Y_BLOCK16_MIN_LEN];
static uint8_t session_expected[SC1777Y_BLOCK16_MIN_LEN];
static uint8_t key_update_data[4];
static enum sc1777y_platform_type platform_type;

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

int main(void)
{
	const struct device *dev = DEVICE_DT_GET(SC1777Y_NODE);
	static const uint8_t expected_serial[SC1777Y_SERIAL_LEN] = {'S', 'C', 23, 119, 0, 0, 0, 1};
	static const uint8_t expected_key_version[SC1777Y_KEY_VERSION_LEN] = {1, 2, 3, 0};
	static const uint8_t expected_sensor_challenge[8] = {192, 193, 194, 195, 196, 197, 198, 199};
	static const uint8_t expected_sensor_rand[4] = {208, 209, 210, 211};
	static const uint8_t subject[] = {'C', 'N', '='};
	static const char cert_prefix[] = "SC1777Y-CERT-REQUEST:";
	int rc;

	if (!device_is_ready(dev)) {
		printf("SC1777Y device not ready\n");
		printf("SC1777Y sample FAIL\n");
		return 1;
	}

	rc = sc1777y_get_random(dev, random_buf, sizeof(random_buf));
	if (rc != 0) {
		return fail_step("get_random", rc);
	}
	if (!expect_sequence(random_buf, sizeof(random_buf), 160)) {
		return fail_check("get_random");
	}

	rc = sc1777y_get_sensor_identity(dev, &sensor_identity);
	if (rc != 0) {
		return fail_step("get_sensor_identity", rc);
	}
	if (!expect_equal(sensor_identity.serial, expected_serial, sizeof(expected_serial)) ||
	    !expect_equal(sensor_identity.key_version, expected_key_version,
			  sizeof(expected_key_version))) {
		return fail_check("get_sensor_identity");
	}

	sensor_rand[0] = 17;
	sensor_rand[1] = 34;
	sensor_rand[2] = 51;
	sensor_rand[3] = 68;
	rc = sc1777y_encrypt_sensor_challenge(dev, sensor_rand, sensor_challenge);
	if (rc != 0) {
		return fail_step("encrypt_sensor_challenge", rc);
	}
	if (!expect_equal(sensor_challenge, expected_sensor_challenge,
			  sizeof(expected_sensor_challenge))) {
		return fail_check("encrypt_sensor_challenge");
	}

	rc = sc1777y_verify_sensor_auth(dev, SC1777Y_SENSOR_NEW, sensor_identity.serial,
					    sensor_challenge, sensor_rand);
	if (rc != 0) {
		return fail_step("verify_sensor_auth", rc);
	}
	if (!expect_equal(sensor_rand, expected_sensor_rand, sizeof(expected_sensor_rand))) {
		return fail_check("verify_sensor_auth");
	}

	sensor_block[0] = 16;
	sensor_block[1] = 32;
	sensor_block[2] = 48;
	sensor_block[3] = 64;
	sensor_block[4] = 80;
	sensor_block[5] = 96;
	sensor_block[6] = 112;
	sensor_block[7] = 128;

	size_t out_len = 0;

	rc = sc1777y_sensor_encrypt(dev, sensor_block, sizeof(sensor_block), sensor_out,
				    sizeof(sensor_out), &out_len);
	if (rc != 0) {
		return fail_step("sensor_encrypt", rc);
	}
	fill_xor(sensor_challenge, sensor_block, sizeof(sensor_block), 90);
	if (out_len != sizeof(sensor_block) ||
	    !expect_equal(sensor_out, sensor_challenge, sizeof(sensor_block))) {
		return fail_check("sensor_encrypt");
	}

	rc = sc1777y_sensor_decrypt_from_terminal(dev, sensor_out, out_len, sensor_challenge,
					      sizeof(sensor_challenge), &out_len);
	if (rc != 0) {
		return fail_step("sensor_decrypt_from_terminal", rc);
	}
	if (out_len != sizeof(sensor_block) ||
	    !expect_equal(sensor_challenge, sensor_block, sizeof(sensor_block))) {
		return fail_check("sensor_decrypt_from_terminal");
	}

	rc = sc1777y_terminal_encrypt_sensor(dev, SC1777Y_SENSOR_NEW, sensor_identity.serial,
						 sensor_block, sizeof(sensor_block), sensor_out,
						 sizeof(sensor_out), &out_len);
	if (rc != 0) {
		return fail_step("terminal_encrypt_sensor", rc);
	}
	fill_xor(sensor_challenge, sensor_block, sizeof(sensor_block), 90);
	if (out_len != sizeof(sensor_block) ||
	    !expect_equal(sensor_out, sensor_challenge, sizeof(sensor_block))) {
		return fail_check("terminal_encrypt_sensor");
	}

	rc = sc1777y_terminal_decrypt_sensor(dev, SC1777Y_SENSOR_NEW, sensor_identity.serial,
						 sensor_out, out_len, sensor_challenge,
						 sizeof(sensor_challenge), &out_len);
	if (rc != 0) {
		return fail_step("terminal_decrypt_sensor", rc);
	}
	if (out_len != sizeof(sensor_block) ||
	    !expect_equal(sensor_challenge, sensor_block, sizeof(sensor_block))) {
		return fail_check("terminal_decrypt_sensor");
	}

	rc = sc1777y_get_update_identity(dev, &update_identity);
	if (rc != 0) {
		return fail_step("get_update_identity", rc);
	}
	if (!expect_equal(update_identity.serial, expected_serial, sizeof(expected_serial)) ||
	    !expect_equal(update_identity.key_version, expected_key_version,
			  sizeof(expected_key_version))) {
		return fail_check("get_update_identity");
	}

	rc = sc1777y_verify_update_auth(dev, sensor_challenge);
	if (rc != 0) {
		return fail_step("verify_update_auth", rc);
	}

	key_update_data[0] = 16;
	key_update_data[1] = 32;
	key_update_data[2] = 48;
	key_update_data[3] = 64;
	rc = sc1777y_apply_key_update(dev, key_update_data, sizeof(key_update_data));
	if (rc != 0) {
		return fail_step("apply_key_update", rc);
	}

	rc = sc1777y_get_version_info(dev, &version_info);
	if (rc != 0) {
		return fail_step("get_version_info", rc);
	}
	fill_sequence(version_expected, sizeof(version_expected), 48);
	if (!expect_equal(version_info.bytes, version_expected, sizeof(version_expected))) {
		return fail_check("get_version_info");
	}

	rc = sc1777y_get_serial(dev, serial);
	if (rc != 0) {
		return fail_step("get_serial", rc);
	}
	if (!expect_equal(serial, expected_serial, sizeof(expected_serial))) {
		return fail_check("get_serial");
	}

	fill_sequence(platform_key, sizeof(platform_key), 16);
	rc = sc1777y_import_platform_public_key(dev, platform_key);
	if (rc != 0) {
		return fail_step("import_platform_public_key", rc);
	}

	fill_sequence(ak, sizeof(ak), 32);
	rc = sc1777y_import_ak(dev, ak);
	if (rc != 0) {
		return fail_step("import_ak", rc);
	}

	fill_sequence(iv, sizeof(iv), 64);
	rc = sc1777y_import_iv(dev, iv);
	if (rc != 0) {
		return fail_step("import_iv", rc);
	}

	rc = sc1777y_set_platform_type(dev, SC1777Y_PLATFORM_NANRUI);
	if (rc != 0) {
		return fail_step("set_platform_type", rc);
	}

	rc = sc1777y_get_platform_type(dev, &platform_type);
	if (rc != 0) {
		return fail_step("get_platform_type", rc);
	}
	if (platform_type != SC1777Y_PLATFORM_NANRUI) {
		return fail_check("get_platform_type");
	}

	rc = sc1777y_generate_sm2_keypair(dev);
	if (rc != 0) {
		return fail_step("generate_sm2_keypair", rc);
	}

	rc = sc1777y_generate_cert_request(dev, SC1777Y_CERT_REQUEST_FORMAT_2, subject,
					       sizeof(subject), cert_request,
					       sizeof(cert_request), &cert_request_len);
	if (rc != 0) {
		return fail_step("generate_cert_request", rc);
	}
	if (cert_request_len != (sizeof(cert_prefix) - 1U + sizeof(subject)) ||
	    memcmp(cert_request, cert_prefix, sizeof(cert_prefix) - 1U) != 0 ||
	    memcmp(&cert_request[sizeof(cert_prefix) - 1U], subject, sizeof(subject)) != 0) {
		return fail_check("generate_cert_request");
	}

	rc = sc1777y_session_begin(dev, session_random);
	if (rc != 0) {
		return fail_step("session_begin", rc);
	}
	if (!expect_sequence(session_random, sizeof(session_random), 192)) {
		return fail_check("session_begin");
	}

	hash_input[0] = 1;
	hash_input[1] = 2;
	hash_input[2] = 3;
	rc = sc1777y_hash(dev, SC1777Y_HASH_RESPONSE, hash_input, sizeof(hash_input), hash_out);
	if (rc != 0) {
		return fail_step("hash", rc);
	}
	fill_sequence(hash_expected, sizeof(hash_expected), 208);
	if (!expect_equal(hash_out, hash_expected, sizeof(hash_expected))) {
		return fail_check("hash");
	}

	rc = sc1777y_sign_hash(dev, hash_out, signature);
	if (rc != 0) {
		return fail_step("sign_hash", rc);
	}
	fill_sequence(signature_expected, sizeof(signature_expected), 224);
	if (!expect_equal(signature, signature_expected, sizeof(signature_expected))) {
		return fail_check("sign_hash");
	}

	rc = sc1777y_verify_signature(dev, hash_out, signature);
	if (rc != 0) {
		return fail_step("verify_signature", rc);
	}

	fill_sequence(auth_factor, sizeof(auth_factor), 81);
	rc = sc1777y_generate_auth_response(dev, auth_factor, auth_response);
	if (rc != 0) {
		return fail_step("generate_auth_response", rc);
	}
	fill_sequence(auth_response_expected, sizeof(auth_response_expected), 112);
	if (!expect_equal(auth_response, auth_response_expected,
			  sizeof(auth_response_expected))) {
		return fail_check("generate_auth_response");
	}

	fill_sequence(session_peer_random, sizeof(session_peer_random), 113);
	rc = sc1777y_session_confirm(dev, session_peer_random, dkhash);
	if (rc != 0) {
		return fail_step("session_confirm", rc);
	}
	fill_sequence(dkhash_expected, sizeof(dkhash_expected), 144);
	if (!expect_equal(dkhash, dkhash_expected, sizeof(dkhash_expected))) {
		return fail_check("session_confirm");
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

	rc = sc1777y_session_encrypt(dev, session_block, sizeof(session_block), session_out,
				     sizeof(session_out), &out_len);
	if (rc != 0) {
		return fail_step("session_encrypt", rc);
	}
	fill_xor(session_expected, session_block, sizeof(session_block), 165);
	if (out_len != sizeof(session_block) ||
	    !expect_equal(session_out, session_expected, sizeof(session_expected))) {
		return fail_check("session_encrypt");
	}

	rc = sc1777y_session_decrypt(dev, session_out, out_len, session_expected,
				     sizeof(session_expected), &out_len);
	if (rc != 0) {
		return fail_step("session_decrypt", rc);
	}
	if (out_len != sizeof(session_block) ||
	    !expect_equal(session_expected, session_block, sizeof(session_block))) {
		return fail_check("session_decrypt");
	}

	printf("SC1777Y sample PASS\n");
	return 0;
}
