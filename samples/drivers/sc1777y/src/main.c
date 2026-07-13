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
	printf("%s 失败: %d\n", step, rc);
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
 * 5.1.1 身份认证流程
 *
 * 流程图：
 * +----------+        Rand1[4]        +--------+
 * |  终端    |----------------------->| 传感器 |
 * |          |                        |        |
 * |          |<-----------------------|        |
 * +----------+ sensorEsamID[8],       +--------+
 *              Version[4], enRand1[8]
 *
 * 前置条件：
 * - 传感器与终端进行业务数据交换前，应先完成身份认证。
 *
 * 交换数据：
 * - 终端 -> 传感器：Rand1[4]
 * - 传感器 -> 终端：sensorEsamID[8], Version[4], enRand1[8]
 * - 终端 -> 传感器：AuthResult
 *
 * 说明：
 * 终端产生 Rand1[4] 并交给传感器。传感器读取自身身份信息，并用内部
 * 操作把 Rand1[4] 加密成 enRand1[8] 后返回。终端使用传感器身份和
 * enRand1[8] 完成验证，再把认证结果交给传感器。
 */
static int run_5_1_1_identity_auth(const struct device *dev)
{
	uint8_t rand1[4];
	struct sc1777y_identity sensor_identity;
	uint8_t en_rand1[8];
	uint8_t rand1_verify[4];
	int rc;

	printf("[5.1.1] 身份认证流程\n");
	printf("  前置条件：业务数据交换前先完成身份认证\n");
	printf("  终端 -> 传感器：Rand1[4]\n");

	/* 流程步骤 1，终端内部操作：对安全芯片取 4 字节随机数 Rand1[4]。 */
	rc = sc1777y_get_random4(dev, rand1);
	if (rc != 0) {
		return fail_step("5.1.1 get_random4", rc);
	}

	/*
	 * 流程步骤 2，终端把 Rand1[4] 交给传感器。
	 */

	printf("  传感器 -> 终端：sensorEsamID[8], Version[4], enRand1[8]\n");

	/* 流程步骤 3，传感器内部操作：读取 sensorEsamID[8] 和 Version[4]。 */
	rc = sc1777y_get_sensor_identity(dev, &sensor_identity);
	if (rc != 0) {
		return fail_step("5.1.1 get_sensor_identity", rc);
	}

	/* 流程步骤 4，传感器内部操作：对安全芯片加密 Rand1[4]，得到 enRand1[8]。 */
	rc = sc1777y_encrypt_sensor_challenge(dev, rand1, en_rand1);
	if (rc != 0) {
		return fail_step("5.1.1 encrypt_sensor_challenge", rc);
	}

	/*
	 * 流程步骤 5，传感器把 sensorEsamID[8]、Version[4]、enRand1[8]
	 * 交给终端。
	 */

	/* 流程步骤 6，终端内部操作：对安全芯片验证 sensorEsamID[8] 和 enRand1[8]。 */
	rc = sc1777y_verify_sensor_auth(dev, SC1777Y_SENSOR_NEW, sensor_identity.serial,
					en_rand1, rand1_verify);
	if (rc != 0) {
		return fail_step("5.1.1 verify_sensor_auth", rc);
	}

	printf("  终端 -> 传感器：AuthResult\n");
	printf("[5.1.1] PASS\n");
	return 0;
}

/*
 * 5.1.2 业务数据流程
 *
 * 流程图：
 *   传感器向终端发送业务数据：
 * +--------+      Data1/enData1      +----------+
 * | 传感器 |------------------------>|   终端   |
 * | 用户   |                         |  用户    |
 * +--------+                         +----------+
 *
 *   终端向传感器发送业务数据：
 * +----------+      Data2/enData2      +--------+
 * |   终端   |------------------------>| 传感器 |
 * |  用户    |                         | 用户   |
 * +----------+                         +--------+
 *
 * 前置条件：
 * - 终端处理传感器数据前，应维护传感器设备地址与 sensorEsamID[8] 的对应关系。
 *
 * 交换数据：
 * - 传感器 -> 终端：enData1[8]
 * - 终端 -> 传感器：enData2[8]
 *
 * 说明：
 * 发送方先完成加密，再把密文交给接收方；接收方收到后再解密。终端侧
 * 对传感器数据做加解密时，必须使用与该传感器匹配的 sensorEsamID。
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

	printf("[5.1.2] 业务数据流程\n");
	printf("  前置条件：终端维护传感器地址与 sensorEsamID[8] 的对应关系\n");
	printf("  传感器 -> 终端：Data1[8] 加密为 enData1[8]\n");

	fill_example_bytes(data1, sizeof(data1), 0x10);

	/* 传感器到终端步骤 1，传感器内部操作：对安全芯片加密 Data1[8]。 */
	rc = sc1777y_sensor_encrypt(dev, data1, sizeof(data1), en_data1,
				    sizeof(en_data1), &out_len);
	if (rc != 0) {
		return fail_step("5.1.2 sensor_encrypt", rc);
	}

	/*
	 * 传感器到终端步骤 2，传感器把 enData1[8] 交给终端。
	 */

	/* 传感器到终端步骤 3，终端内部操作：对安全芯片解密 enData1[8]。 */
	rc = sc1777y_terminal_decrypt_sensor(dev, SC1777Y_SENSOR_NEW, sensor_esam_id,
					     en_data1, out_len, data1_plain,
					     sizeof(data1_plain), &out_len);
	if (rc != 0) {
		return fail_step("5.1.2 terminal_decrypt_sensor", rc);
	}

	printf("  终端 -> 传感器：Data2[8] 加密为 enData2[8]\n");
	fill_example_bytes(data2, sizeof(data2), 0x20);

	/* 终端到传感器步骤 1，终端内部操作：对安全芯片加密 Data2[8]。 */
	rc = sc1777y_terminal_encrypt_sensor(dev, SC1777Y_SENSOR_NEW, sensor_esam_id,
					     data2, sizeof(data2), en_data2,
					     sizeof(en_data2), &out_len);
	if (rc != 0) {
		return fail_step("5.1.2 terminal_encrypt_sensor", rc);
	}

	/*
	 * 终端到传感器步骤 2，终端把 enData2[8] 交给传感器。
	 */

	/* 终端到传感器步骤 3，传感器内部操作：对安全芯片解密 enData2[8]。 */
	rc = sc1777y_sensor_decrypt_from_terminal(dev, en_data2, out_len, data2_plain,
						  sizeof(data2_plain), &out_len);
	if (rc != 0) {
		return fail_step("5.1.2 sensor_decrypt_from_terminal", rc);
	}

	printf("[5.1.2] PASS\n");
	return 0;
}

/*
 * 5.2.1 密钥更新/恢复流程
 *
 * 流程图：
 * +----------------------+                               +----------+
 * |      维护软件        |------------------------------>|   终端   |
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
 * 前置条件：
 * - 维护软件持有更新/恢复 USBKey，并已加载配套接口库。
 *
 * 交换数据：
 * - 维护软件 -> 终端：identity request
 * - 终端 -> 维护软件：EsamID[8], Version[4], ERand1[8]
 * - 维护软件 -> 终端：enERand1[8]
 * - 终端 -> 维护软件：AuthResult, ERand2[8]
 * - 维护软件 -> 终端：KeyData[len]
 * - 终端 -> 维护软件：UpdateResult
 *
 * 说明：
 * 终端提供身份信息和随机数材料。维护软件负责更新策略、认证密文和
 * KeyData 生成。终端验证维护软件返回的密文随机数，通过后应用
 * KeyData 更新包。
 */
static int run_5_2_1_key_update(const struct device *dev)
{
	struct sc1777y_identity update_identity;
	uint8_t e_rand1[8];
	uint8_t en_e_rand1[8];
	uint8_t e_rand2[8];
	uint8_t key_data[4];
	int rc;

	printf("[5.2.1] 密钥更新/恢复流程\n");
	printf("  前置条件：维护软件持有更新/恢复 USBKey，并已加载接口库\n");
	printf("  维护软件 -> 终端：identity request\n");
	printf("  终端 -> 维护软件：EsamID[8], Version[4], ERand1[8]\n");
	printf("  维护软件 -> 终端：enERand1[8]\n");
	printf("  终端 -> 维护软件：AuthResult, ERand2[8]\n");
	printf("  维护软件 -> 终端：KeyData[len]\n");
	printf("  终端 -> 维护软件：UpdateResult\n");

	/* 流程步骤 1，维护软件向终端请求身份信息。 */

	/* 流程步骤 2，终端内部操作：对安全芯片读取 EsamID[8] 和 Version[4]。 */
	rc = sc1777y_get_update_identity(dev, &update_identity);
	if (rc != 0) {
		return fail_step("5.2.1 get_update_identity", rc);
	}

	/* 流程步骤 3，终端内部操作：对安全芯片取 8 字节随机数 ERand1[8]。 */
	rc = sc1777y_get_random8(dev, e_rand1);
	if (rc != 0) {
		return fail_step("5.2.1 get_random8 auth", rc);
	}

	/* 流程步骤 4，终端把 EsamID[8]、Version[4]、ERand1[8] 交给维护软件。 */

	/* 流程步骤 5，维护软件生成 enERand1[8] 并交给终端。 */
	fill_example_bytes(en_e_rand1, sizeof(en_e_rand1), 0x30);

	/* 流程步骤 6，终端内部操作：对安全芯片验证 enERand1[8]。 */
	rc = sc1777y_verify_update_auth(dev, en_e_rand1);
	if (rc != 0) {
		return fail_step("5.2.1 verify_update_auth", rc);
	}

	/* 流程步骤 7，终端内部操作：对安全芯片取 8 字节随机数 ERand2[8]。 */
	rc = sc1777y_get_random8(dev, e_rand2);
	if (rc != 0) {
		return fail_step("5.2.1 get_random8 package", rc);
	}

	/* 流程步骤 8，终端把 AuthResult 和 ERand2[8] 交给维护软件。 */

	/* 流程步骤 9，维护软件生成 KeyData[len] 并交给终端。 */
	fill_example_bytes(key_data, sizeof(key_data), 0x40);

	/* 流程步骤 10，终端内部操作：对安全芯片写入 KeyData[len]。 */
	rc = sc1777y_apply_key_update(dev, key_data, sizeof(key_data));
	if (rc != 0) {
		return fail_step("5.2.1 apply_key_update", rc);
	}

	/* 流程步骤 11，终端把 UpdateResult 交给维护软件。 */

	printf("[5.2.1] PASS\n");
	return 0;
}

/*
 * 5.3.1 平台基础指令流程
 *
 * 流程图：
 * +----------+                         +----------+
 * |   终端   |<----------------------->|   平台   |
 * |  用户    | public key, AK, IV,     |  用户    |
 * |          |  version, serial, rand  |          |
 * +----------+                         +----------+
 *
 * 前置条件：
 * - 平台公钥、AK 和 IV 由平台或平台配置提供。
 *
 * 交换数据：
 * - 终端本地操作：VersionInfo[64], Serial[8], Random[len]
 * - 平台 -> 终端：PlatformPublicKey[64], AK[16], IV[16]
 *
 * 说明：
 * 该流程展示平台相关的基础准备：终端读取版本、序列号和随机数；平台
 * 下发公钥、AK 和 IV，终端再导入到本地安全芯片。
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

	printf("[5.3.1] 平台基础指令流程\n");
	printf("  前置条件：平台公钥、AK[16] 和 IV[16] 已可用\n");
	printf("  终端本地操作：VersionInfo[64], Serial[8], Random[16]\n");
	printf("  平台 -> 终端：PlatformPublicKey[64], AK[16], IV[16]\n");

	/* 流程步骤 1，终端内部操作：对安全芯片读取 VersionInfo[64]。 */
	rc = sc1777y_get_version_info(dev, &version_info);
	if (rc != 0) {
		return fail_step("5.3.1 get_version_info", rc);
	}
	(void)version_info;

	/* 流程步骤 2，终端内部操作：对安全芯片读取 Serial[8]。 */
	rc = sc1777y_get_serial(dev, serial);
	if (rc != 0) {
		return fail_step("5.3.1 get_serial", rc);
	}
	(void)serial;

	/* 流程步骤 3，终端内部操作：对安全芯片读取 Random[16]。 */
	rc = sc1777y_get_random(dev, random16, sizeof(random16));
	if (rc != 0) {
		return fail_step("5.3.1 get_random", rc);
	}
	(void)random16;

	/* 流程步骤 4，平台把 PlatformPublicKey[64] 交给终端。 */
	fill_sequence(platform_key, sizeof(platform_key), 16);

	/* 流程步骤 5，终端内部操作：向安全芯片导入 PlatformPublicKey[64]。 */
	rc = sc1777y_import_platform_public_key(dev, platform_key);
	if (rc != 0) {
		return fail_step("5.3.1 import_platform_public_key", rc);
	}

	/* 流程步骤 6，平台把 AK[16] 交给终端。 */
	fill_sequence(ak, sizeof(ak), 32);

	/* 流程步骤 7，终端内部操作：向安全芯片导入 AK[16]。 */
	rc = sc1777y_import_ak(dev, ak);
	if (rc != 0) {
		return fail_step("5.3.1 import_ak", rc);
	}

	/* 流程步骤 8，平台把 IV[16] 交给终端。 */
	fill_sequence(iv, sizeof(iv), 64);

	/* 流程步骤 9，终端内部操作：向安全芯片导入 IV[16]。 */
	rc = sc1777y_import_iv(dev, iv);
	if (rc != 0) {
		return fail_step("5.3.1 import_iv", rc);
	}

	printf("[5.3.1] PASS\n");
	return 0;
}

/*
 * 5.3.2 证书请求流程
 *
 * 流程图：
 * +----------+      certificate request       +----------+
 * |   平台   |------------------------------->|   终端   |
 * |  用户    |                                |  用户    |
 * |          |<-------------------------------|          |
 * +----------+      serial + CSR bytes        +----------+
 *
 * 前置条件：
 * - 终端生成 CSR 前，应先生成或确认本地 SM2 密钥对已经存在。
 * - 重新生成密钥对会覆盖旧密钥对，需要重新申请证书。
 *
 * 交换数据：
 * - 平台 -> 终端：certificate enrollment request
 * - 终端 -> 平台：Serial[8], CSR[len]
 *
 * 说明：
 * 平台发起证书申请。终端在本地安全芯片中生成或确认 SM2 密钥对，
 * 读取序列号，并生成 CSR 数据返回给平台。
 */
static int run_5_3_2_certificate_request(const struct device *dev)
{
	static const uint8_t subject[] = {'C', 'N', '='};
	uint8_t serial[SC1777Y_SERIAL_LEN];
	uint8_t csr[128];
	size_t csr_len;
	int rc;

	printf("[5.3.2] 证书请求流程\n");
	printf("  前置条件：生成 CSR 前，本地 SM2 密钥对已经存在\n");
	printf("  前置条件：重新生成 SM2 密钥对后，需要重新申请证书\n");
	printf("  平台 -> 终端：certificate enrollment request\n");
	printf("  终端 -> 平台：Serial[8], CSR[len]\n");

	/* 流程步骤 1，平台向终端发起证书申请。 */

	/* 流程步骤 2，终端内部操作：在安全芯片中生成 SM2 密钥对。 */
	rc = sc1777y_generate_sm2_keypair(dev);
	if (rc != 0) {
		return fail_step("5.3.2 generate_sm2_keypair", rc);
	}

	/* 流程步骤 3，终端内部操作：对安全芯片读取 Serial[8]。 */
	rc = sc1777y_get_serial(dev, serial);
	if (rc != 0) {
		return fail_step("5.3.2 get_serial", rc);
	}
	(void)serial;

	/* 流程步骤 4，终端内部操作：对安全芯片生成 CSR[len]。 */
	rc = sc1777y_generate_cert_request(dev, SC1777Y_CERT_REQUEST_FORMAT_2, subject,
					   sizeof(subject), csr, sizeof(csr), &csr_len);
	if (rc != 0) {
		return fail_step("5.3.2 generate_cert_request", rc);
	}
	(void)csr;
	(void)csr_len;

	/* 流程步骤 5，终端把 Serial[8] 和 CSR[len] 交给平台。 */

	printf("[5.3.2] PASS\n");
	return 0;
}

/*
 * 5.3.6 平台类型选择流程
 *
 * 流程图：
 * +----------+      selected platform type      +----------+
 * |   终端   |---------------------------------->|   平台   |
 * |  用户    |                                   |  用户    |
 * +----------+                                   +----------+
 *
 * 前置条件：
 * - 在 5.3.3 生成认证响应前，应先完成平台类型选择。
 *
 * 交换数据：
 * - 终端本地配置：PlatformType
 *
 * 说明：
 * 终端把当前对接的平台类型写入安全芯片，并可读取确认。会话协商中生成
 * AuthResponse 前依赖该选择结果。
 */
static int run_5_3_6_platform_type(const struct device *dev)
{
	enum sc1777y_platform_type platform_type;
	int rc;

	printf("[5.3.6] 平台类型选择流程\n");
	printf("  前置条件：5.3.3 生成 AuthResponse 前已选择 PlatformType\n");
	printf("  终端本地配置：PlatformType\n");

	/* 流程步骤 1，终端内部操作：向安全芯片写入 PlatformType。 */
	rc = sc1777y_set_platform_type(dev, SC1777Y_PLATFORM_NANRUI);
	if (rc != 0) {
		return fail_step("5.3.6 set_platform_type", rc);
	}

	/* 流程步骤 2，终端内部操作：从安全芯片读取 PlatformType 用于确认。 */
	rc = sc1777y_get_platform_type(dev, &platform_type);
	if (rc != 0) {
		return fail_step("5.3.6 get_platform_type", rc);
	}
	(void)platform_type;

	printf("[5.3.6] PASS\n");
	return 0;
}

/*
 * 5.3.3 会话协商流程
 *
 * 流程图：
 * +----------+       RequestMsg        +----------+
 * |   终端   |------------------------>|   平台   |
 * |  用户    |                         |  用户    |
 * |          |<------------------------|          |
 * |          |       ResponseMsg       |          |
 * |          |------------------------>|          |
 * +----------+       ConfirmMsg        +----------+
 *
 * 前置条件：
 * - 发起会话前，终端证书已经存在。
 * - 发起会话前，平台公钥已经导入终端安全芯片。
 * - 生成认证响应前，已经完成平台类型选择。
 *
 * 交换数据：
 * - 终端 -> 平台：
 *   RequestMsg { DATA, RequestSign[64] }
 *   DATA { Type, SubType, Len, Ver, SN, SIM, ID, Cert1, EnR1[128] }
 *   驱动关键数据：EnR1[128], RequestHash[32], RequestSign[64]
 *
 * - 平台 -> 终端：
 *   ResponseMsg { Type, SubType, Len, SN, AuthFactor[32], EnR2[128],
 *                 ResponseSign[64] }
 *   驱动关键数据：AuthFactor[32], EnR2[128], ResponseHash[32],
 *                    ResponseSign[64]
 *
 * - 终端 -> 平台：
 *   ConfirmMsg { Type, SubType, Len, SN, AuthResult, DKHash[32] }
 *   驱动关键数据：AuthResult, DKHash[32]
 *
 * 说明：
 * 终端先生成 EnR1[128]，对 RequestMsg 的 DATA 求杂凑并签名，然后把
 * RequestMsg 交给平台。平台返回 ResponseMsg 后，终端验证平台签名、
 * 生成 AuthResponse，并用 EnR2[128] 完成会话确认，最后把 ConfirmMsg
 * 交给平台。
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

	printf("[5.3.3] 会话协商流程\n");
	printf("  前置条件：终端证书已经存在\n");
	printf("  前置条件：平台公钥已经导入终端安全芯片\n");
	printf("  前置条件：生成 AuthResponse 前已选择 PlatformType\n");
	printf("  终端 -> 平台：RequestMsg { DATA, RequestSign[64] }\n");
	printf("    DATA { Type, SubType, Len, Ver, SN, SIM, ID, Cert1, EnR1[128] }\n");
	printf("    驱动关键数据：EnR1[128], RequestHash[32], RequestSign[64]\n");
	printf("  平台 -> 终端：ResponseMsg { Type, SubType, Len, SN, "
	       "AuthFactor[32], EnR2[128], ResponseSign[64] }\n");
	printf("    驱动关键数据：AuthFactor[32], EnR2[128], ResponseHash[32], "
	       "ResponseSign[64]\n");
	printf("  终端 -> 平台：ConfirmMsg { Type, SubType, Len, SN, AuthResult, DKHash[32] }\n");
	printf("    驱动关键数据：AuthResult, DKHash[32]\n");

	/* 流程步骤 1，终端内部操作：对安全芯片发起会话，得到 EnR1[128]。 */
	rc = sc1777y_session_begin(dev, en_r1);
	if (rc != 0) {
		return fail_step("5.3.3 session_begin", rc);
	}

	/* 流程步骤 2，终端组装 RequestMsg.DATA。 */

	/* 流程步骤 3，终端内部操作：对安全芯片计算 RequestMsg.DATA 的杂凑。 */
	rc = sc1777y_hash(dev, SC1777Y_HASH_REQUEST, request_body, sizeof(request_body),
			  request_hash);
	if (rc != 0) {
		return fail_step("5.3.3 request_hash", rc);
	}

	/* 流程步骤 4，终端内部操作：对安全芯片签名 RequestHash[32]。 */
	rc = sc1777y_sign_hash(dev, request_hash, request_sign);
	if (rc != 0) {
		return fail_step("5.3.3 sign_hash", rc);
	}

	/* 流程步骤 5，终端把 RequestMsg { DATA, RequestSign[64] } 交给平台。 */

	/* 流程步骤 6，平台生成 ResponseMsg 并交给终端。 */
	fill_example_bytes(response_sign, sizeof(response_sign), 0x60);
	fill_example_bytes(auth_factor, sizeof(auth_factor), 0x70);
	fill_example_bytes(en_r2, sizeof(en_r2), 0x80);

	/* 流程步骤 7，终端内部操作：对安全芯片计算 ResponseMsg 的杂凑。 */
	rc = sc1777y_hash(dev, SC1777Y_HASH_RESPONSE, response_body, sizeof(response_body),
			  response_hash);
	if (rc != 0) {
		return fail_step("5.3.3 response_hash", rc);
	}

	/* 流程步骤 8，终端内部操作：对安全芯片验证 ResponseSign[64]。 */
	rc = sc1777y_verify_signature(dev, response_hash, response_sign);
	if (rc != 0) {
		return fail_step("5.3.3 verify_signature", rc);
	}

	/* 流程步骤 9，终端内部操作：对安全芯片生成 AuthResponse。 */
	rc = sc1777y_generate_auth_response(dev, auth_factor, auth_response);
	if (rc != 0) {
		return fail_step("5.3.3 generate_auth_response", rc);
	}

	/* 流程步骤 10，终端内部操作：对安全芯片确认会话，得到 DKHash[32]。 */
	rc = sc1777y_session_confirm(dev, en_r2, dk_hash);
	if (rc != 0) {
		return fail_step("5.3.3 session_confirm", rc);
	}

	/* 流程步骤 11，终端把 ConfirmMsg { AuthResult, DKHash[32] } 交给平台。 */

	printf("[5.3.3] PASS\n");
	return 0;
}

/*
 * 5.3.4 会话密钥加密流程
 *
 * 流程图：
 * +----------+      IV + ciphertext      +----------+
 * |   终端   |-------------------------->|   平台   |
 * |  用户    |                           |  用户    |
 * +----------+                           +----------+
 *
 * 前置条件：
 * - 会话协商已经成功。
 * - 待加密明文长度满足 16 字节分组要求。
 *
 * 交换数据：
 * - 终端 -> 平台：
 *   RequestMsg { Type, SubType, Len, IV[16], ResponseData[ciphertext] }
 *   驱动关键数据：IV[16], DATA[16], ResponseData[ciphertext]
 *
 * 说明：
 * 终端生成 IV[16] 并导入安全芯片，对 DATA 做会话密钥加密后，将
 * RequestMsg 交给平台。
 */
static int run_5_3_4_session_key_encryption(const struct device *dev)
{
	uint8_t iv[16];
	uint8_t plaintext[SC1777Y_BLOCK16_MIN_LEN];
	uint8_t ciphertext[SC1777Y_BLOCK16_MIN_LEN];
	size_t ciphertext_len = 0U;
	int rc;

	printf("[5.3.4] 会话密钥加密流程\n");
	printf("  前置条件：会话协商已经成功\n");
	printf("  前置条件：明文长度满足 16 字节分组要求\n");
	printf("  终端 -> 平台：RequestMsg { Type, SubType, Len, "
	       "IV[16], ResponseData[ciphertext] }\n");
	printf("    驱动关键数据：IV[16], DATA[16], ResponseData[ciphertext]\n");

	/* 流程步骤 1，终端内部操作：对安全芯片取随机 IV[16]。 */
	rc = sc1777y_get_random(dev, iv, sizeof(iv));
	if (rc != 0) {
		return fail_step("5.3.4 get_random", rc);
	}

	/* 流程步骤 2，终端内部操作：向安全芯片导入 IV[16]。 */
	rc = sc1777y_import_iv(dev, iv);
	if (rc != 0) {
		return fail_step("5.3.4 import_iv", rc);
	}

	/* 流程步骤 3，终端准备待加密 DATA[16]。 */
	fill_example_bytes(plaintext, sizeof(plaintext), 0x90);

	/* 流程步骤 4，终端内部操作：对安全芯片加密 DATA[16]。 */
	rc = sc1777y_session_encrypt(dev, plaintext, sizeof(plaintext), ciphertext,
				     sizeof(ciphertext), &ciphertext_len);
	if (rc != 0) {
		return fail_step("5.3.4 session_encrypt", rc);
	}
	(void)ciphertext_len;

	/* 流程步骤 5，终端把 RequestMsg { IV[16], ResponseData[ciphertext] } 交给平台。 */

	printf("[5.3.4] PASS\n");
	return 0;
}

/*
 * 5.3.5 会话密钥解密流程
 *
 * 流程图：
 * +----------+      IV + ciphertext      +----------+
 * |   平台   |-------------------------->|   终端   |
 * |  用户    |                           |  用户    |
 * +----------+                           +----------+
 *
 * 前置条件：
 * - 会话协商已经成功。
 * - 终端已经从平台报文中解析出 IV[16] 和 RequestData[ciphertext]。
 *
 * 交换数据：
 * - 平台 -> 终端：
 *   RequestMsg { Type, SubType, Len, IV[16], RequestData[ciphertext] }
 *   驱动关键数据：IV[16], RequestData[ciphertext], ResponseData[plaintext]
 *
 * 说明：
 * 平台把带 IV 和密文的 RequestMsg 交给终端。终端导入 IV，再使用会话
 * 密钥解密 RequestData，得到 ResponseData[plaintext]。
 */
static int run_5_3_5_session_key_decryption(const struct device *dev)
{
	uint8_t iv[16];
	uint8_t ciphertext[SC1777Y_BLOCK16_MIN_LEN];
	uint8_t plaintext[SC1777Y_BLOCK16_MIN_LEN];
	size_t plaintext_len = 0U;
	int rc;

	printf("[5.3.5] 会话密钥解密流程\n");
	printf("  前置条件：会话协商已经成功\n");
	printf("  前置条件：终端已经解析 IV[16] 和 RequestData[ciphertext]\n");
	printf("  平台 -> 终端：RequestMsg { Type, SubType, Len, "
	       "IV[16], RequestData[ciphertext] }\n");
	printf("    驱动关键数据：IV[16], RequestData[ciphertext], ResponseData[plaintext]\n");

	/* 流程步骤 1，平台生成 RequestMsg 并交给终端。 */
	fill_example_bytes(iv, sizeof(iv), 0xA0);
	fill_example_bytes(ciphertext, sizeof(ciphertext), 0xB0);

	/* 流程步骤 2，终端内部操作：向安全芯片导入 IV[16]。 */
	rc = sc1777y_import_iv(dev, iv);
	if (rc != 0) {
		return fail_step("5.3.5 import_iv", rc);
	}

	/* 流程步骤 3，终端内部操作：对安全芯片解密 RequestData[ciphertext]。 */
	rc = sc1777y_session_decrypt(dev, ciphertext, sizeof(ciphertext), plaintext,
				     sizeof(plaintext), &plaintext_len);
	if (rc != 0) {
		return fail_step("5.3.5 session_decrypt", rc);
	}
	(void)plaintext_len;

	/* 流程步骤 4，终端得到 ResponseData[plaintext]，供后续业务处理。 */

	printf("[5.3.5] PASS\n");
	return 0;
}

int main(void)
{
	const struct device *dev = DEVICE_DT_GET(SC1777Y_NODE);

	if (!device_is_ready(dev)) {
		printf("SC1777Y 设备未就绪\n");
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
