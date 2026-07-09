/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_MISC_SC1777Y_H_
#define ZEPHYR_INCLUDE_DRIVERS_MISC_SC1777Y_H_

#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SC1777Y_MAX_DATA_LEN 2048U
#define SC1777Y_MAX_FRAME_LEN (SC1777Y_MAX_DATA_LEN + 8U)
#define SC1777Y_SERIAL_LEN 8U
#define SC1777Y_KEY_VERSION_LEN 4U
#define SC1777Y_IDENTITY_LEN (SC1777Y_SERIAL_LEN + SC1777Y_KEY_VERSION_LEN)
#define SC1777Y_VERSION_INFO_LEN 64U
#define SC1777Y_PLATFORM_PUBLIC_KEY_LEN 64U
#define SC1777Y_AK_LEN 16U
#define SC1777Y_IV_LEN 16U
#define SC1777Y_HASH_LEN 32U
#define SC1777Y_SIGNATURE_LEN 64U
#define SC1777Y_AUTH_FACTOR_LEN 32U
#define SC1777Y_AUTH_RESPONSE_LEN 146U
#define SC1777Y_SESSION_RANDOM_LEN 128U
#define SC1777Y_SESSION_DKHASH_LEN 32U
#define SC1777Y_BLOCK16_MIN_LEN 16U

struct sc1777y_command {
	uint8_t cla;
	uint8_t ins;
	uint8_t p1;
	uint8_t p2;
	const uint8_t *data;
	size_t data_len;
};

struct sc1777y_status {
	uint8_t sw1;
	uint8_t sw2;
};

enum sc1777y_sensor_type {
	SC1777Y_SENSOR_LEGACY = 0,
	SC1777Y_SENSOR_NEW = 1,
};

enum sc1777y_platform_type {
	SC1777Y_PLATFORM_UNSET = 0,
	SC1777Y_PLATFORM_NANRUI = 1,
	SC1777Y_PLATFORM_WANGAN = 2,
};

enum sc1777y_cert_request_format {
	SC1777Y_CERT_REQUEST_FORMAT_1 = 0,
	SC1777Y_CERT_REQUEST_FORMAT_2 = 1,
};

enum sc1777y_hash_target {
	SC1777Y_HASH_REQUEST = 0,
	SC1777Y_HASH_RESPONSE = 1,
};

struct sc1777y_identity {
	uint8_t serial[SC1777Y_SERIAL_LEN];
	uint8_t key_version[SC1777Y_KEY_VERSION_LEN];
};

struct sc1777y_version_info {
	uint8_t bytes[SC1777Y_VERSION_INFO_LEN];
};

int sc1777y_command(const struct device *dev, const struct sc1777y_command *cmd, uint8_t *out,
		    size_t out_size, size_t *out_len, struct sc1777y_status *status);

/***** Sensor user operations *****/
int sc1777y_get_sensor_identity(const struct device *dev, struct sc1777y_identity *identity);
int sc1777y_encrypt_sensor_challenge(const struct device *dev, const uint8_t rand4[4],
				     uint8_t encrypted8[8]);
int sc1777y_sensor_encrypt(const struct device *dev, const uint8_t *in, size_t in_len,
			   uint8_t *out, size_t out_size, size_t *out_len);
int sc1777y_sensor_decrypt_from_terminal(const struct device *dev, const uint8_t *in,
					 size_t in_len, uint8_t *out, size_t out_size,
					 size_t *out_len);

/***** Terminal user operations *****/

/***** 5.1.1 Identity authentication flow *****/
int sc1777y_get_random(const struct device *dev, uint8_t *out, size_t len);
int sc1777y_get_random4(const struct device *dev, uint8_t rand4[4]);
int sc1777y_verify_sensor_auth(const struct device *dev, enum sc1777y_sensor_type type,
			       const uint8_t sensor_id[8], const uint8_t encrypted8[8],
			       uint8_t rand4[4]);

/***** 5.1.2 Business data flow *****/
int sc1777y_terminal_decrypt_sensor(const struct device *dev, enum sc1777y_sensor_type type,
				    const uint8_t sensor_id[8], const uint8_t *in,
				    size_t in_len, uint8_t *out, size_t out_size,
				    size_t *out_len);
int sc1777y_terminal_encrypt_sensor(const struct device *dev, enum sc1777y_sensor_type type,
				    const uint8_t sensor_id[8], const uint8_t *in,
				    size_t in_len, uint8_t *out, size_t out_size,
				    size_t *out_len);

/***** 5.2.1 Key update/recovery flow *****/
int sc1777y_get_update_identity(const struct device *dev, struct sc1777y_identity *identity);
int sc1777y_get_random8(const struct device *dev, uint8_t rand8[8]);
int sc1777y_verify_update_auth(const struct device *dev, const uint8_t encrypted8[8]);
int sc1777y_apply_key_update(const struct device *dev, const uint8_t *key_data,
			     size_t key_data_len);

/***** 5.3.1 Platform basic instructions *****/
int sc1777y_get_version_info(const struct device *dev, struct sc1777y_version_info *version);
int sc1777y_get_serial(const struct device *dev, uint8_t serial[SC1777Y_SERIAL_LEN]);
int sc1777y_import_platform_public_key(const struct device *dev,
				       const uint8_t key64[SC1777Y_PLATFORM_PUBLIC_KEY_LEN]);
int sc1777y_import_ak(const struct device *dev, const uint8_t ak16[SC1777Y_AK_LEN]);
int sc1777y_import_iv(const struct device *dev, const uint8_t iv16[SC1777Y_IV_LEN]);

/***** 5.3.2 Certificate request flow *****/
int sc1777y_generate_sm2_keypair(const struct device *dev);
int sc1777y_generate_cert_request(const struct device *dev,
				  enum sc1777y_cert_request_format format,
				  const uint8_t *subject, size_t subject_len,
				  uint8_t *out, size_t out_size, size_t *out_len);

/***** 5.3.3 Session negotiation flow *****/
int sc1777y_session_begin(const struct device *dev, uint8_t en_r1[SC1777Y_SESSION_RANDOM_LEN]);
int sc1777y_hash(const struct device *dev, enum sc1777y_hash_target target,
		 const uint8_t *data, size_t len, uint8_t hash32[SC1777Y_HASH_LEN]);
int sc1777y_sign_hash(const struct device *dev, const uint8_t hash32[SC1777Y_HASH_LEN],
		      uint8_t signature64[SC1777Y_SIGNATURE_LEN]);
int sc1777y_verify_signature(const struct device *dev, const uint8_t hash32[SC1777Y_HASH_LEN],
			     const uint8_t signature64[SC1777Y_SIGNATURE_LEN]);
int sc1777y_generate_auth_response(const struct device *dev,
				   const uint8_t factor32[SC1777Y_AUTH_FACTOR_LEN],
				   uint8_t response146[SC1777Y_AUTH_RESPONSE_LEN]);
int sc1777y_session_confirm(const struct device *dev,
			    const uint8_t en_r2_128[SC1777Y_SESSION_RANDOM_LEN],
			    uint8_t dkhash32[SC1777Y_SESSION_DKHASH_LEN]);

/***** 5.3.4 Session-key encryption flow *****/
int sc1777y_session_encrypt(const struct device *dev, const uint8_t *in, size_t in_len,
			    uint8_t *out, size_t out_size, size_t *out_len);

/***** 5.3.5 Session-key decryption flow *****/
int sc1777y_session_decrypt(const struct device *dev, const uint8_t *in, size_t in_len,
			    uint8_t *out, size_t out_size, size_t *out_len);

/***** 5.3.6 Platform type selection flow *****/
int sc1777y_set_platform_type(const struct device *dev, enum sc1777y_platform_type type);
int sc1777y_get_platform_type(const struct device *dev, enum sc1777y_platform_type *type);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_MISC_SC1777Y_H_ */
