# SC1777Y 安全接入与 MQTT 自定义传输设计

## 1. 背景

终端需要通过 SC1777Y 安全芯片接入主站安全接入端点。终端只建立一条 TCP
连接、只使用一个主站端口：TCP 建立后先在该连接上完成三步会话协商，协商成功
后再发送 MQTT CONNECT。此后的 MQTT 字节流经过 SC1777Y 会话密钥加解密，
主站解密后将原始 MQTT 字节透明转发给后端 MQTT Broker。

仓库已经包含 SC1777Y SPI 驱动、SPI emulator 和芯片语义 API。本设计在其上增加
一个独立安全通道库，并用薄适配器接入 Zephyr MQTT custom transport。

设计依据：

- 《状态监测终端安全模块产品说明书 V1.0（SC1777Y）》第 5.3 节。
- 《采集类终端安全接入》第 3.3 节。
- 《终端与服务端交互日志》中的请求、应答、确认和加密通信样例。
- 《设备上传报文》中的两个 MQTT PUBLISH 样例。
- Zephyr `CONFIG_MQTT_LIB_CUSTOM_TRANSPORT` 接口。

## 2. 已确认的产品边界

### 2.1 本次范围

- 只实现终端侧。
- 实现安全接入协议、SC1777Y 调用、TCP 安全通道和 MQTT custom transport。
- 终端连接主站安全接入端点，不直接连接后端 MQTT Broker。
- 运行期使用预置终端证书、平台公钥、SIM 和设备 ID。
- native_sim 通过 TAP 接口运行完整 Zephyr TCP/IP 网络栈。
- 端到端测试是默认必跑门禁。
- 主站侧由宿主机脚本模拟安全接入网关；网关脚本连接本地真实 Mosquitto Broker
  并透明转发，不实现 BrokerStub。
- native_sim 的 SC1777Y emulator 和主站脚本使用配套的确定性密码模型。

### 2.2 非目标

- 不实现可部署的主站安全网关。
- 不实现首次设备初始化、密钥对生成、证书申请、证书导入或证书持久化。
- 不在 native_sim 中实现真实 SM1、SM2、SM3 或其他国密算法。
- 不修改 MQTT 报文格式，也不只加密 MQTT PUBLISH payload。
- 不在安全通道库内部实现自动重连、后台线程或连接池。
- 不让终端建立到后端 MQTT Broker 的第二条连接。

真实密码互操作由后续“真实 SC1777Y + 测试主站”硬件验收覆盖。

## 3. 总体架构

```text
终端应用
  |
  v
Zephyr MQTT
  |  原始 MQTT 字节流
  v
MQTT custom transport 薄适配器
  |  connect/send/recv/close
  v
SC1777Y 安全通道库
  |- 会话协商状态机
  |- 安全协议帧编解码
  |- 填充、分片和接收缓存
  |- 直接调用 SC1777Y 公共驱动 API
  `- Zephyr socket I/O
  |
  v
Zephyr TCP/IP + native_sim TAP
  |
  | 唯一 TCP 连接、唯一主站端口
  v
SecurityGatewayPeer（宿主机测试脚本）
  |- 终结确定性安全协议
  `- 透明转发解密后的 MQTT 字节
  |
  | 宿主机内部明文 TCP
  v
本地真实 Mosquitto Broker
```

生产环境中，`SecurityGatewayPeer` 对应实际主站安全接入网关，Mosquitto 对应主站
后端 MQTT 服务。终端对主站内部的第二段连接不可见。

## 4. 代码边界

建议新增：

```text
include/zephyr/net/sc1777y_secure_channel.h
subsys/net/lib/sc1777y_secure_channel/
|- CMakeLists.txt
|- Kconfig
|- secure_channel.c
|- io.h
|- protocol.c
|- handshake.c
|- record.c
|- socket_io.c
`- mqtt_transport.c
```

职责如下：

- `secure_channel.c`：公共生命周期、状态和同步控制。
- `io.h`：子系统私有字节流后端接口；生产绑定 socket，测试绑定脚本后端。
- `protocol.c`：Type、Subtype、Len 和固定字段的编解码，不访问 socket 或芯片。
- `handshake.c`：三步会话协商及 SC1777Y 会话类 API 调用。
- `record.c`：填充、去填充、分片、IV 和会话数据加解密。
- `socket_io.c`：socket 创建、绑定 TAP 接口、连接、完整发送和增量接收。
- `mqtt_transport.c`：Zephyr MQTT custom transport 五个入口的薄映射。

建议 Kconfig：

- `CONFIG_SC1777Y_SECURE_CHANNEL`：启用安全通道核心，依赖 SC1777Y。
- `CONFIG_SC1777Y_SECURE_CHANNEL_SOCKET`：启用生产 socket 后端，依赖
  SC1777Y_SECURE_CHANNEL 和 NET_SOCKETS。
- `CONFIG_SC1777Y_SECURE_CHANNEL_MQTT`：启用 MQTT 薄适配，依赖
  SC1777Y_SECURE_CHANNEL_SOCKET、MQTT_LIB 和 MQTT_LIB_CUSTOM_TRANSPORT。

安全协议核心不进入现有 SC1777Y 驱动。驱动继续只负责芯片命令和 SPI 传输。
`handshake.c` 和 `record.c` 直接包含 `zephyr/drivers/misc/sc1777y.h` 并调用公共驱动
API，不增加密码操作 vtable，也不用软件密码实现替代芯片。可替换的私有接口只限于
底层连续字节流，以便在没有真实网络的 ztest 中精确注入拆包、粘包、超时和断开。

## 5. 公共接口

公共 API 采用同步、无后台线程、无动态内存的模型：

```c
int sc1777y_secure_channel_init(
	struct sc1777y_secure_channel *channel,
	const struct sc1777y_secure_channel_config *config);

int sc1777y_secure_channel_connect(
	struct sc1777y_secure_channel *channel);

int sc1777y_secure_channel_send(
	struct sc1777y_secure_channel *channel,
	const uint8_t *data,
	size_t len);

int sc1777y_secure_channel_recv(
	struct sc1777y_secure_channel *channel,
	uint8_t *data,
	size_t size,
	bool shall_block);

int sc1777y_secure_channel_close(
	struct sc1777y_secure_channel *channel);
```

`sc1777y_secure_channel_config` 包含：

- SC1777Y `struct device`。
- 安全接入端点地址和端口。
- 可选的网络接口名，用于 `SO_BINDTODEVICE`。
- 连接、协商和 I/O 超时。
- 终端证书指针和长度。
- 64 字节平台公钥。
- 已按协议格式准备好的 16 字节 SIM 字段。
- 已按协议格式准备好的 18 字节设备 ID 字段。
- 安全接入平台类型。
- 调用者提供的协议工作缓冲区和接收明文缓存。

公共长度上限由芯片 2048 字节命令数据限制推导：

```text
SC1777Y_SECURE_MAX_CERTIFICATE_LEN   = 1878
SC1777Y_SECURE_MAX_HANDSHAKE_LEN     = 2112
SC1777Y_SECURE_MAX_PLAINTEXT_CHUNK   = 2047
SC1777Y_SECURE_MAX_CIPHERTEXT_LEN    = 2048
SC1777Y_SECURE_MAX_RECORD_LEN        = 2068
```

请求签名之前的主体长度为 `170+n`，因此证书最大 1878 字节；完整请求再加 64
字节签名，最大 2112 字节。安全记录由 4 字节头、16 字节 IV 和最多 2048 字节
密文组成，最大 2068 字节。实现可以复用不同阶段的工作缓冲区，但必须保留独立的
接收明文缓存，且所有容量在初始化时校验。

配置中的端点是终端唯一可见的主站安全接入端点。MQTT `client->broker` 使用同一
端点；适配初始化函数负责校验两者一致，防止配置漂移。

通道状态：

```text
DISCONNECTED
  -> TCP_CONNECTED
  -> NEGOTIATING
  -> ESTABLISHED
  -> FAILED 或 CLOSED
```

只有 `ESTABLISHED` 状态允许传输 MQTT 数据。

## 6. 单连接建立顺序

Zephyr MQTT 的 `mqtt_connect()` 先调用 custom transport connect，再编码和发送
MQTT CONNECT。因此安全通道插入顺序为：

```text
mqtt_connect()
  -> mqtt_client_custom_transport_connect()
  -> sc1777y_secure_channel_connect()
  -> 建立唯一 TCP socket
  -> 在该 socket 上完成三步安全协商
  -> 返回 mqtt_connect()
  -> MQTT 库通过同一 socket 发送加密后的 MQTT CONNECT
```

协商报文不会进入 MQTT RX 缓冲区；MQTT 库只看到协商完成后的解密字节流。

## 7. 会话协商

### 7.1 请求

1. 连接安全接入端点。
2. 调用 `sc1777y_set_platform_type()`。
3. 调用 `sc1777y_import_platform_public_key()`。
4. 调用 `sc1777y_session_begin()` 获取 128 字节 `EnR1`。
5. 从 SC1777Y 随机数生成 16 位 SN，按网络序编码。
6. 组织请求主体：

```text
Type=1 | Subtype=1 | Len=234+n | Ver=0x0100 | SN
| SIM[16] | DeviceID[18] | Certificate[n] | EnR1[128]
```

7. 调用 `sc1777y_hash(SC1777Y_HASH_REQUEST, ...)` 计算请求摘要。
8. 调用 `sc1777y_sign_hash()` 获取 64 字节签名并附加到请求主体。
9. 完整发送请求报文。

请求摘要输入受 SC1777Y 2048 字节数据上限约束。初始化时提前校验证书长度；超限
返回 `-EMSGSIZE`，不得在握手中截断证书。

### 7.2 应答

完整接收固定 230 字节：

```text
Type=1 | Subtype=2 | Len=230 | SN+1
| AuthFactor[32] | EnR2[128] | Signature[64]
```

处理顺序：

1. 校验类型、子类型、网络序长度和 SN+1。
2. 对签名之前的应答主体调用 `sc1777y_hash(SC1777Y_HASH_RESPONSE, ...)`。
3. 调用 `sc1777y_verify_signature()`。
4. 调用 `sc1777y_generate_auth_response()` 生成 146 字节认证结果。
5. 调用 `sc1777y_session_confirm(EnR2)` 获取 32 字节 `DKHash`。

### 7.3 确认

发送固定 184 字节：

```text
Type=1 | Subtype=3 | Len=184 | SN+2
| AuthResult[146] | DKHash[32]
```

资料说明确认成功时主站不返回报文。完整发送后通道进入 `ESTABLISHED`。随后若主站
返回非预期数据或关闭连接，通道转入 `FAILED`。

## 8. 加密记录层

### 8.1 上行

MQTT 数据被视为连续字节流，不假设一次 `write()` 对应一个 MQTT 包。

1. 将任意输入拆为不超过 2047 字节的明文块。
2. 每块追加 `0x80 00...`，始终填充 1 到 16 字节。
3. 调用 `sc1777y_get_random()` 获取 16 字节 IV。
4. 原子执行 `sc1777y_import_iv()` 和 `sc1777y_session_encrypt()`。
5. 发送：

```text
Type=2 | Subtype=0 | Len=20+n | IV[16] | Ciphertext[n]
```

最大 2047 字节明文填充后为 2048 字节，符合芯片上限。更大的 MQTT 包自动跨多个
安全记录；主站去除记录边界后向 Broker 转发连续 MQTT 字节。

### 8.2 下行

接收端维护增量状态：安全头、记录体、已解密明文和明文读取偏移。

1. 收满 4 字节安全记录头。
2. 校验 Type=2、Subtype=0、网络序长度和密文块长度。
3. 缓存直到完整安全记录到达。
4. 原子执行 `sc1777y_import_iv()` 和 `sc1777y_session_decrypt()`。
5. 严格验证 `0x80 00...` 填充。
6. 向 MQTT 返回连续明文。
7. MQTT 缓冲区较小时保留剩余明文，供下一次 `recv()` 使用。

非阻塞读取在没有完整明文时返回 `-EAGAIN`，不泄漏半个安全帧或半次解密结果。
阻塞读取遵守配置的 I/O 超时。

### 8.3 同步

SC1777Y 的“导入 IV”和“加解密”是两个驱动调用，安全通道用密码锁将其组合为
不可交错操作。发送锁、接收锁和密码锁分离；阻塞接收不会阻止 MQTT 发送 PING、
ACK 或其他上行数据。

## 9. MQTT custom transport

薄适配器实现 Zephyr 要求的五个全局符号：

```c
mqtt_client_custom_transport_connect()
mqtt_client_custom_transport_write()
mqtt_client_custom_transport_write_msg()
mqtt_client_custom_transport_read()
mqtt_client_custom_transport_disconnect()
```

适配器通过 `client->transport.custom_transport_data` 获取通道实例。它只转换参数和
返回值，不解析 MQTT、不构造安全报文、不持有会话状态，也不直接调用 SC1777Y。

- `connect()`：建立 socket 并完成安全协商后才返回。
- `write()`：加密并完整发送输入字节，成功返回 0。
- `write_msg()`：按 iovec 顺序发送，Broker 恢复出的字节顺序必须与原 `msghdr`
  完全一致。
- `read()`：返回解密后的 MQTT 字节数、`-EAGAIN`、其他 errno 或对端关闭的 0。
- `disconnect()`：关闭唯一 socket，清空通道状态和缓存。

## 10. 错误和恢复

安全通道采用连接级失败语义。以下错误将状态置为 `FAILED` 并关闭 socket：

- 协商类型、子类型、长度或 SN 错误。
- 平台签名验证或安全认证失败。
- TCP 部分写入后继续发送失败。
- 加密记录长度非法或密文不是 16 字节整数倍。
- 解密填充非法。
- SC1777Y 导入 IV、加密或解密失败。
- 对端关闭连接或 I/O 超时。

错误映射：

- 配置非法：`-EINVAL`
- 证书或摘要输入超限：`-EMSGSIZE`
- 超时：`-ETIMEDOUT`
- 类型、长度、状态机或 SN 错误：`-EPROTO`
- 签名或认证失败：`-EACCES`
- 密文或填充损坏：`-EBADMSG`
- 暂无完整明文：`-EAGAIN`
- socket 错误：保留 Zephyr socket errno
- 对端正常关闭：向 MQTT 返回 0

发生失败后不在原 socket 上恢复。应用重新执行 MQTT 连接流程时创建新 socket、
重新协商会话密钥并清空缓存。库不实现内部自动重连。

附件只列出主站错误码 1 到 24 和 255，没有给出错误报文线格式。本版本不猜测该
格式：协商阶段收到任何非预期报文均返回 `-EPROTO` 并关闭连接。后续获得错误报文
格式后再增加精确错误码解析，不改变成功路径。

关闭或失败时清除 IV、摘要、认证因子、解密明文和协商临时数据。会话密钥始终
保留在 SC1777Y 内部。

## 11. 测试设计

默认测试入口每次运行驱动、协议、适配和 TAP 端到端测试。任何一层失败均返回
非零状态。

### 11.1 安全通道库与 SC1777Y ztest

测试通过安全通道公共 API 驱动会话协商和记录层。芯片路径必须调用真实
`sc1777y.h` 公共驱动 API，并通过现有 SPI emulator 观察命令和注入错误；只对
底层字节流 I/O 使用脚本化测试后端，不用密码 mock 代替 SC1777Y 驱动。

覆盖：

- 三类协商报文和加密报文的逐字节编解码。
- 网络序长度、SN+1、SN+2 和 16 位回绕。
- `0x80 00...` 填充与去填充全部边界。
- 1、15、16、17 和 2047 字节明文。
- 超过单帧限制的自动分片。
- TCP 粘包、拆包和逐字节到达。
- 非阻塞读取只返回完整明文。
- 调用者缓冲区较小时的剩余明文缓存。
- 非法类型、子类型、长度、SN、密文块和填充。
- 任意阶段超时、断开和部分写入。
- 平台类型设置、平台公钥导入和会话协商的芯片调用顺序。
- 每个上行记录执行“随机数、导入 IV、会话加密”。
- 每个下行记录执行“导入 IV、会话解密”。
- 芯片状态错误、响应 LRC 错误和延迟。

附件日志作为黄金报文来源。

确定性模型沿用当前 emulator：会话加解密 XOR `0xA5`，摘要、签名和认证输出固定
序列。测试不声称验证真实密码学。

### 11.2 socket 与 MQTT 适配测试

在已经通过 SC1777Y 驱动路径验证的安全通道库之上覆盖：

- socket 创建、接口绑定、连接、完整发送和增量接收。
- 验证 custom transport 五个入口。
- 验证 `write_msg()` 多 iovec 的连续字节语义。
- 验证 MQTT CONNECT 只会出现在协商确认之后。
- 验证 socket 或安全通道错误能够按 MQTT transport 约定返回。

### 11.3 必跑 TAP 端到端测试

使用 Twister pytest harness。测试入口负责：

1. 检查 `CAP_NET_ADMIN` 或 root、Zephyr net-tools、`mosquitto`、
   `mosquitto_sub` 和 `mosquitto_pub`。
2. 创建唯一 TAP 接口，并确保退出时无条件清理。
3. 用临时配置启动隔离的本地 Mosquitto Broker；禁用持久化并使用独立端口。
4. 启动 `SecurityGatewayPeer`，监听 TAP 主机地址上的安全接入端口。
5. 启动 native_sim 终端。
6. 汇总终端、网关、Mosquitto 客户端工具和 Broker 的断言与日志。
7. 无条件停止进程并清理 TAP 和临时文件。

缺少必需能力或依赖时测试失败，不允许 skip。

`SecurityGatewayPeer` 的职责严格限制为：

- 接收终端唯一 TCP 连接。
- 使用与 SC1777Y emulator 配套的确定性模型完成三步安全协商。
- 建立到本地 Mosquitto 的独立明文 TCP 连接。
- 将终端安全记录解密、去填充后原样写入 Mosquitto socket。
- 将 Mosquitto socket 返回的 MQTT 字节切分、填充、确定性加密后写回终端。
- 记录转发字节和协议事件，供 pytest 断言。

它不解析或模拟 Broker 行为。MQTT 语义验证由真实 Mosquitto 和官方客户端工具
完成：`mosquitto_sub` 订阅终端上行主题以确认 Broker 实际收到报文，
`mosquitto_pub` 向终端订阅主题发布 QoS 1 下行消息。

每次端到端测试覆盖：

1. TAP 上建立唯一 TCP 连接。
2. 三步会话协商。
3. MQTT CONNECT/CONNACK。
4. SUBSCRIBE/SUBACK。
5. 上传附件中的拓扑添加 PUBLISH。
6. 上传附件中的设备数据 PUBLISH。
7. 额外 QoS 1 上行及 PUBACK。
8. `mosquitto_pub` 经真实 Broker 下发 QoS 1 PUBLISH，终端返回 PUBACK。
9. PINGREQ/PINGRESP。
10. 网关主动拆分和合并 TCP 写入，验证增量接收。
11. MQTT DISCONNECT 和正常关闭。
12. 协商失败、损坏填充、连接中断和重新连接场景。

## 12. 实施拆分

整体设计保留在本文档中，实施拆成三个依赖明确的计划，避免一个巨型计划和提交：

### 计划一：SC1777Y 安全通道库

- 帧编解码。
- 会话协商状态机。
- 记录层、缓存、同步和错误模型。
- 直接调用现有 `sc1777y.h` 公共驱动 API。
- 核心 Kconfig 和 CMake 集成。
- 通过 SPI emulator 和脚本化字节流 I/O 测试安全通道公共 API。

交付标准：在不依赖 socket 和 MQTT 的情况下，安全通道公共 API 已通过真实
SC1777Y 驱动路径和确定性字节流测试验证。

### 计划二：Zephyr socket 与 MQTT 集成

- Zephyr socket I/O。
- socket 相关 Kconfig 和 CMake 集成。
- MQTT custom transport 薄适配。
- socket 和 MQTT 适配测试。

交付标准：native_sim 内能够通过真实 MQTT API 驱动安全通道，尚不要求外部 TAP。

### 计划三：必跑端到端门禁

- native_sim 终端测试应用。
- TAP 编排。
- SecurityGatewayPeer。
- 隔离 Mosquitto、`mosquitto_sub` 和 `mosquitto_pub`。
- 双向 MQTT 正常与错误场景。
- 默认统一测试入口和运行文档。

交付标准：单条默认测试命令每次运行全部测试，端到端链路任何失败都会阻止通过。

三个计划依次实施；只有计划三通过，整体功能才视为交付。

## 13. 验收标准

- 终端只建立一条到主站安全接入端点的 TCP 连接。
- 同一 socket 上先完成安全协商，再发送 MQTT CONNECT。
- 协商前不会发送任何 MQTT 数据。
- MQTT custom transport 不包含安全协议或芯片命令细节。
- 任意大小的 MQTT 字节流可以跨安全记录传输，单次芯片输入不超过 2048 字节。
- 下行 TCP 分片不会导致半帧或半次解密结果进入 MQTT。
- 失败连接不会在原 socket 上继续使用。
- native_sim 通过 TAP 使用完整 Zephyr 网络栈。
- SecurityGatewayPeer 透明连接真实本地 Mosquitto，不使用 BrokerStub。
- 真实 Mosquitto 完成 CONNECT、订阅、双向发布、QoS 1 和保活交互。
- 驱动、协议、适配和 TAP 端到端测试全部属于默认必跑门禁。
