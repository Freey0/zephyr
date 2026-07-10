# SC1777Y 桌面维护软件与交互 Sample 设计

日期：2026-07-10

状态：设计已在对话中逐节确认并完成自审，等待用户书面规格复核

## 背景

仓库已经包含 SC1777Y SPI 驱动、确定性 emulator、驱动测试，以及按资料第 5 章
组织的流程指南 sample。现有 `samples/drivers/sc1777y` 在单个进程中说明维护软件、
终端、传感器和平台之间的数据流，但它不提供真正的主机与终端通信。

本设计新增一个 Windows/Linux 桌面维护软件和一个 Zephyr 终端 sample，让两端通过
MCUmgr Simple Management Protocol（SMP）串口传输完成真实的请求/响应交互。当前没有
真实 USBKey，也没有运行真实 SC1777Y 芯片的终端，因此第一阶段采用双端仿真：桌面端
使用 USBKey 模拟后端，终端端通过 `native_sim` 使用现有 SC1777Y emulator。

双端软件按真实产品边界实现。以后接入硬件时只替换 USBKey、SC1777Y 和串口端点，
不改 GUI、SMP 业务协议或维护状态机。

## 目标

- 提供跨 Windows/Linux 的 PySide6 桌面维护软件。
- 通过同一个 SMP Serial 适配器支持当前 PTY 和未来真实 UART。
- 提供 SC1777Y 版本、身份、密钥版本、平台类型等只读诊断。
- 严格按现有 sample 的 5.2.1 密钥更新/恢复流程完成端到端交互。
- 使用可替换的 USBKey 后端；当前实现确定性模拟后端。
- 使用可复用的 SC1777Y MCUmgr 管理组；sample 只负责仿真环境装配。
- 对最长 2048 字节的 `KeyData` 实现有界、可校验的分块传输。
- 对认证失败、更新失败、断线和结果未知提供明确且安全的恢复行为。
- 通过驱动、管理组、桌面核心、GUI 和 PTY 端到端测试验证实现。

## 非目标

- 不实现真实 USBKey 厂商 DLL/SO 适配器。缺少硬件、SDK、ABI 和密码测试向量时，
  不能假装实现真实认证或密钥材料生成。
- 不新增真实 SC1777Y 板级 overlay，也不声称验证真实 SPI 时序或密码行为。
- 不实现终端固件升级、MCUboot、DFU、批量设备维护或远程维护。
- 不向桌面端开放 `sc1777y_command()`、CLA/INS、LRC 或其他原始 APDU 调试入口。
- 不在首版制作或签名 Windows/Linux 安装包；首版通过 Python 项目入口运行。
- 不实现通用插件市场或任意命令总线。USBKey 接口只覆盖 5.2.1 所需能力。

## 已确认的技术决策

- 传输协议：MCUmgr/SMP 自定义管理组。
- 当前物理端点：`native_sim` 的独立 PTY UART。
- 未来物理端点：真实终端 UART，经 USB-UART 转换器表现为 `/dev/tty*` 或 `COM*`。
- GUI：PySide6 Qt Widgets。
- 操作方式：引导式维护流程，不提供任意载荷编辑面板。
- 主机库：Python 3.10、`smpclient[serial]`、PySide6。
- 设备侧载荷：SMP v2 CBOR map，业务失败使用显式结果或管理组错误。
- KeyData：按设备声明的块大小顺序上传，提交前校验总长度和 CRC32。

## 总体架构

```text
PySide6 桌面维护软件
  ├─ MainWindow / 引导式页面
  ├─ MaintenanceWorkflow
  ├─ UsbKeyBackend
  │    ├─ SimulatedUsbKeyBackend（当前）
  │    └─ 厂商 SDK 适配器（硬件阶段使用相同接口）
  └─ Sc1777ySmpClient
       └─ SMPSerialTransport
            ├─ /dev/pts/N（当前）
            ├─ /dev/ttyUSB*（未来 Linux UART）
            └─ COM*（未来 Windows UART）

Zephyr 终端
  └─ SC1777Y MCUmgr 管理组
       ├─ 诊断 handler
       ├─ 5.2.1 事务状态机
       ├─ KeyData staging buffer
       └─ SC1777Y 公共语义 API
            ├─ SC1777Y emulator（当前）
            └─ 真实 SPI 芯片（未来）
```

桌面维护软件是主动方和流程编排者。终端 sample 是被动响应的 SMP 服务端。终端不生成
`enERand1` 或 `KeyData`；这两项始终由桌面端 USBKey 后端生成。桌面端不知道 SPI、
SC1777Y APDU 或驱动内部状态字。

## 仓库布局

```text
include/zephyr/mgmt/mcumgr/grp/sc1777y_mgmt/
└── sc1777y_mgmt.h                  命令 ID、管理组 ID、错误码和公共常量

subsys/mgmt/mcumgr/grp/sc1777y_mgmt/
├── CMakeLists.txt
├── Kconfig
└── src/sc1777y_mgmt.c              CBOR handler、事务状态机和驱动调用

samples/drivers/sc1777y_maintenance/
├── CMakeLists.txt
├── prj.conf
├── sample.yaml
├── README.rst
├── boards/native_sim.overlay       uart1 PTY + SC1777Y emulator
├── src/main.c
└── pytest/test_maintenance.py      PTY 端到端测试

scripts/sc1777y_maintenance/
├── pyproject.toml
├── README.md
├── src/sc1777y_maintenance/
│   ├── __main__.py
│   ├── domain/
│   │   ├── errors.py
│   │   ├── models.py
│   │   └── states.py
│   ├── application/
│   │   └── workflow.py
│   ├── interfaces.py
│   ├── protocol/
│   │   └── sc1777y_group.py
│   ├── adapters/
│   │   ├── serial_ports.py
│   │   ├── smp_terminal.py
│   │   └── usbkey_simulated.py
│   └── ui/
│       ├── main_window.py
│       ├── diagnostics_page.py
│       ├── update_wizard.py
│       └── log_model.py
└── tests/
    ├── unit/
    └── gui/

tests/subsys/mgmt/mcumgr/sc1777y_mgmt/
├── CMakeLists.txt
├── prj.conf
├── testcase.yaml
├── boards/native_sim.overlay
└── src/

doc/services/device_mgmt/smp_groups/smp_group_64.rst
```

现有 `samples/drivers/sc1777y` 保持说明书式流程指南定位，不转成服务端，也不被桌面软件
直接启动。

## 终端 MCUmgr 管理组

### 注册与设备绑定

管理组 ID 固定为 `MGMT_GROUP_ID_PERUSER`，当前 Zephyr 值为 64。命令使用 SMP v2。
管理组通过 `MCUMGR_HANDLER_DEFINE()` 在系统初始化阶段注册，并从 `sc1777y-0` alias
取得设备。管理组只调用以下公共语义 API：

- `sc1777y_get_update_identity()`
- `sc1777y_get_random8()`
- `sc1777y_verify_update_auth()`
- `sc1777y_apply_key_update()`
- `sc1777y_get_version_info()`
- `sc1777y_get_platform_type()`

管理组实现不能构造 CLA/INS、SC1777Y 帧或 LRC。

### 命令表

| ID | 名称 | SMP 操作 | 作用 |
|---:|---|---|---|
| 0 | `capabilities` | READ | 查询协议版本、缓冲区限制和仿真标志 |
| 1 | `diagnostics` | READ | 查询终端和 SC1777Y 只读信息 |
| 2 | `update_begin` | WRITE | 创建事务并返回身份与 `ERand1` |
| 3 | `authenticate` | WRITE | 验证 `enERand1` 并返回 `AuthResult`、`ERand2` |
| 4 | `upload_chunk` | WRITE | 顺序上传一块 KeyData |
| 5 | `commit` | WRITE | 完整性校验并执行一次密钥更新 |
| 6 | `status` | READ | 查询事务阶段、下一偏移和最终结果 |
| 7 | `abort` | WRITE | 在提交前取消事务并清理材料 |

### CBOR 契约

所有固定长度二进制字段使用 CBOR byte string，不使用十六进制文本。所有整数使用 CBOR
unsigned integer。字段名使用小写 snake_case。未知字段由 zcbor 跳过；必填字段缺失、
重复、类型错误或长度错误返回 `MGMT_ERR_EINVAL`。

#### capabilities

请求：空 map。

成功响应：

```text
{
  "protocol_version": 1,
  "max_key_data": 2048,
  "max_chunk": 192,
  "simulation": true | false
}
```

`simulation` 由 `CONFIG_SC1777Y_MAINTENANCE_SIMULATION` 决定。仿真 sample 设为 true，
真实终端配置必须为 false。

#### diagnostics

请求：空 map。

成功响应：

```text
{
  "protocol_version": 1,
  "app_version": tstr,
  "esam_id": bstr[8],
  "version": bstr[4],
  "chip_version": bstr[64],
  "platform_type": uint,
  "simulation": true | false
}
```

`app_version` 使用构建时 `BUILD_VERSION`；构建系统未提供该宏时使用
`KERNEL_VERSION_STRING`。GUI 对 `chip_version` 同时提供十六进制视图和可打印 ASCII
视图，不假设 64 字节内容一定是文本。

`version` 对应现有 5.2.1 流程中的 `Version[4]`，驱动结构字段名为
`sc1777y_identity.key_version`。GUI 显示为“密钥版本”。`platform_type` 沿用公共枚举值：
0 未设置、1 南瑞、2 网安。

#### update_begin

请求：空 map。

成功响应：

```text
{
  "transaction_id": bstr[8],
  "esam_id": bstr[8],
  "version": bstr[4],
  "erand1": bstr[8],
  "expires_in_ms": 60000
}
```

终端读取身份后生成 `ERand1[8]`。如果已有未结束事务，返回 `BUSY`。事务 ID 只用于
关联、去重和防止旧请求串入新流程，不替代 USBKey 认证。运行时事务 ID 由 Zephyr
`sys_rand_get()` 填充 8 字节；测试通过固定随机源获得可重复值。新事务会替换上一条已
结束的非敏感结果记录。

#### authenticate

请求：

```text
{
  "transaction_id": bstr[8],
  "encrypted_erand1": bstr[8]
}
```

终端调用 `sc1777y_verify_update_auth()`。该驱动 API 没有独立的 AuthResult 输出；返回
0 表示芯片验证成功，`-EACCES` 表示验证拒绝。终端必须把这个语义转换为显式字段。

验证成功后，终端才调用 `sc1777y_get_random8()` 生成 `ERand2[8]`：

```text
{
  "transaction_id": bstr[8],
  "auth_result": true,
  "erand2": bstr[8]
}
```

验证拒绝时不生成、不返回 `ERand2`：

```text
{
  "transaction_id": bstr[8],
  "auth_result": false,
  "reason": uint
}
```

认证失败结束并清理当前事务。桌面端必须重新执行 `update_begin`，不能复用 ERand1。
芯片超时或 SPI I/O 故障使用管理组错误，不伪装成普通认证拒绝。

#### upload_chunk

请求：

```text
{
  "transaction_id": bstr[8],
  "offset": uint,
  "total_len": uint,
  "data": bstr[1..192]
}
```

成功响应：

```text
{
  "transaction_id": bstr[8],
  "next_offset": uint
}
```

约束：

- `total_len` 范围为 1..2048，并在同一事务中保持不变。
- 正常块的 `offset` 必须等于当前 `next_offset`。
- 已接收范围内的完全相同重复块作为幂等重发接受，并返回现有 `next_offset`。
- 重叠但内容不同、跳跃偏移、越过总长度或认证前上传均被拒绝。
- staging buffer 固定为 2048 字节，不进行无界动态分配。

192 字节块大小使单个 CBOR 请求保持在 Zephyr 默认 384 字节 SMP 接收 net_buf 内。
客户端同时遵守 `capabilities.max_chunk` 和 `smpclient` 计算出的传输上限。

#### commit

请求：

```text
{
  "transaction_id": bstr[8],
  "total_len": uint,
  "crc32": uint
}
```

终端检查已接收长度和整个 KeyData 的 IEEE CRC32。校验成功后，对一个事务最多调用一次
`sc1777y_apply_key_update()`。

明确成功：

```text
{
  "transaction_id": bstr[8],
  "update_result": true
}
```

芯片给出明确拒绝状态：

```text
{
  "transaction_id": bstr[8],
  "update_result": false,
  "reason": uint
}
```

调用发出后出现超时、SPI I/O 或响应校验错误时，终端不能证明芯片是否已经应用更新，
事务进入 `OUTCOME_UNKNOWN`。桌面端只能查询 `status`，不能自动再次 commit。

#### status

请求：

```text
{
  "transaction_id": bstr[8]
}
```

成功响应至少包含：

```text
{
  "transaction_id": bstr[8],
  "state": uint,
  "next_offset": uint,
  "auth_result": true | false | null,
  "update_result": true | false | null
}
```

活动状态在事务结束和敏感材料清理后回到 `IDLE`，但终端另保留最近一个事务的 ID 和
非敏感结果记录，直到新事务开始或终端重启。因此 status 仍能查询刚结束的认证失败、
更新成功、明确失败或结果未知。终端重启后无法确认旧事务结果时，桌面端显示“结果
未知”。首版不把事务结果持久化到 flash。

#### abort

请求只包含 `transaction_id`。在 `APPLYING` 前允许取消；终端清理挑战、认证结果和
staging buffer。进入 `APPLYING` 后返回 `INVALID_STATE`，因为取消不能撤回已经发给芯片
的命令。

### 业务结果值与状态值

`auth_result=false` 和 `update_result=false` 的 `reason` 使用以下固定值：

| 值 | 名称 | 使用位置 |
|---:|---|---|
| 1 | `AUTH_REJECTED` | 芯片明确拒绝 `enERand1` |
| 2 | `UPDATE_REJECTED` | 芯片明确拒绝 KeyData |

正常成功不携带 reason。超时、I/O、协议和状态错误不复用业务 reason，而使用管理组错误。

`status.state` 使用以下固定值：

| 值 | 名称 | 含义 |
|---:|---|---|
| 0 | `IDLE` | 没有活动事务 |
| 1 | `CHALLENGE_ISSUED` | 已返回 ERand1，等待 enERand1 |
| 2 | `AUTHENTICATED` | AuthResult 为真，等待 KeyData |
| 3 | `RECEIVING_KEY_DATA` | 正在接收分块 |
| 4 | `APPLYING` | 已开始调用芯片更新 |
| 5 | `APPLIED` | UpdateResult 为真 |
| 6 | `AUTH_FAILED` | 最近事务认证被拒绝 |
| 7 | `FAILED` | 最近事务明确更新失败 |
| 8 | `OUTCOME_UNKNOWN` | 最近事务结果无法确认 |
| 9 | `EXPIRED` | 最近事务已超时 |

结束状态只保留事务 ID、状态、AuthResult、UpdateResult、已接收长度和错误类别，不保留
ERand 或 KeyData。完成上传后 `next_offset` 保留为 `total_len`；未进入上传的结束状态为 0。

### 事务状态机

```text
IDLE
  └─ update_begin ─> CHALLENGE_ISSUED
       ├─ authenticate(false) ─> AUTH_FAILED ─> IDLE
       └─ authenticate(true)  ─> AUTHENTICATED
            └─ upload_chunk ─> RECEIVING_KEY_DATA
                 └─ commit ─> APPLYING
                      ├─ 明确成功 ─> APPLIED
                      ├─ 明确拒绝 ─> FAILED
                      └─ 结果不确定 ─> OUTCOME_UNKNOWN
```

管理组使用 mutex 保护全局事务；同时只允许一个活动事务。事务在 60 秒无活动后过期并
清理。所有敏感缓冲区使用不会被编译器消除的显式清零方式处理。

### 管理组错误

SMP v2 管理组错误码：

| 值 | 名称 | 含义 |
|---:|---|---|
| 0 | `OK` | 成功，正常响应中省略 |
| 1 | `UNKNOWN` | 未分类内部错误 |
| 2 | `INVALID_STATE` | 命令不符合当前状态 |
| 3 | `BUSY` | 已有活动事务 |
| 4 | `TRANSACTION_NOT_FOUND` | 事务 ID 不存在或已经替换 |
| 5 | `TRANSACTION_EXPIRED` | 事务超过 60 秒 |
| 6 | `INVALID_LENGTH` | 总长度或块长度非法 |
| 7 | `OFFSET_MISMATCH` | 分块偏移或重复块内容不一致 |
| 8 | `CRC_MISMATCH` | commit 的整包 CRC32 不匹配 |
| 9 | `DEVICE_TIMEOUT` | 芯片操作在明确未提交阶段超时 |
| 10 | `DEVICE_IO` | SPI 或响应校验错误 |
| 11 | `NOT_SUPPORTED` | 芯片或终端不支持操作 |
| 12 | `OUTCOME_UNKNOWN` | 更新命令结果无法确认 |

参数结构错误仍返回 MCUmgr 通用 `MGMT_ERR_EINVAL`。如果启用原始 SMP v1 支持，管理组
提供 `.mg_translate_error`，将上述错误映射为最接近的 MCUmgr 通用错误。

## 5.2.1 业务数据流

以下顺序以维护软件为视角，与现有 `run_5_2_1_key_update()` 的 11 个步骤一致：

```text
维护软件                         终端
   │── update_begin ────────────>│
   │                             │ 读取 EsamID[8]、Version[4]
   │                             │ 生成 ERand1[8]
   │<─ EsamID, Version, ERand1 ──│
   │                             │
   │  USBKey 生成 enERand1[8]    │
   │                             │
   │── authenticate(enERand1) ──>│
   │                             │ 验证 enERand1[8]
   │                             │ 成功后生成 ERand2[8]
   │<─ AuthResult, ERand2 ───────│
   │                             │
   │  USBKey 根据 ERand2[8]      │
   │  生成 KeyData[len]          │
   │                             │
   │── upload_chunk... ─────────>│
   │── commit ──────────────────>│ 应用 KeyData[len]
   │<─ UpdateResult ─────────────│
```

`AuthResult=false` 时不返回 ERand2，桌面端不调用 USBKey 的 KeyData 生成能力，也不发送
任何 KeyData。成功收到 `UpdateResult` 后，桌面端重新读取 diagnostics。

## SC1777Y emulator 行为

真实密码算法仍然不属于 emulator。emulator 按文档接受合法的
`sc1777y_apply_key_update()` 命令并返回成功状态，但不虚构文档未定义的密钥状态变化，也
不修改模拟 `Version[4]`。后续 `sc1777y_get_update_identity()` 继续返回确定性 fixture
身份，且 `Version[4]` 最后一个字节保持为 `0x00`。该行为只证明软件交互，不代表真实
KeyData 的密码学有效性。

维护认证顺序由 MCUmgr 管理组状态机强制执行，不依赖当前较宽松的 emulator 流程状态。

## Desktop 应用设计

### 分层

PySide6 控件只依赖 `MaintenanceWorkflow`，不直接导入 `smpclient`、操作串口或调用
USBKey。核心接口为：

```python
class TerminalMaintenance(Protocol):
    async def connect(self, endpoint: SerialEndpoint) -> Capabilities: ...
    async def disconnect(self) -> None: ...
    async def read_diagnostics(self) -> Diagnostics: ...
    async def begin_update(self) -> Challenge: ...
    async def authenticate(self, transaction_id: bytes,
                           encrypted_erand1: bytes) -> AuthenticationResult: ...
    async def upload_key_data(self, transaction_id: bytes, key_data: bytes,
                              progress: ProgressCallback) -> None: ...
    async def commit(self, transaction_id: bytes,
                     key_data_crc32: int) -> UpdateResult: ...
    async def status(self, transaction_id: bytes) -> TransactionStatus: ...
    async def abort(self, transaction_id: bytes) -> None: ...

class UsbKeyBackend(Protocol):
    def preflight(self) -> UsbKeyStatus: ...
    def generate_encrypted_erand1(self, esam_id: bytes, version: bytes,
                                  erand1: bytes) -> bytes: ...
    def generate_key_data(self, esam_id: bytes, version: bytes,
                          erand2: bytes) -> bytes: ...
```

真实 USBKey SDK 到来后，其 DLL/SO 适配器实现同一同步接口。SDK 调用由通信工作线程执行，
不进入 GUI 主线程。

### smpclient 与线程模型

`smpclient` 是 asyncio 库。桌面端创建一个 `QThread`，在线程中创建并独占一个 asyncio
event loop、`SMPClient` 和串口。GUI 通过 Qt queued signal 提交动作，工作线程通过 signal
返回不可变进度快照、诊断结果和错误。

同一连接只允许一个在途 SMP 请求，并使用 `asyncio.Lock` 串行化。不能在按钮回调中调用
`asyncio.run()`，不能跨线程共享 `SMPClient`，也不引入第二套 PTY transport。

`protocol/sc1777y_group.py` 使用底层 `smp` 包定义自定义 Request、Response、ErrorV1 和
ErrorV2 模型，再绑定到 `smpclient` 的 typed request。GUI 和工作流看不到这些类型。

### USBKey 模拟后端

`SimulatedUsbKeyBackend` 对相同输入生成确定性 `enERand1` 和 KeyData，便于测试重放。
它不宣称实现真实密码算法。工作流只有在终端 capabilities 返回 `simulation=true` 时才允许
选择该后端。真实终端返回 false 时，模拟后端预检失败并禁用“开始更新/恢复”按钮，防止
把测试材料写入真实芯片。

模拟算法固定为：

```text
encrypted_erand1[i] = erand1[i] XOR esam_id[i] XOR version[i mod 4] XOR 0xA5
key_data[i] = erand2[i mod 8] XOR esam_id[i mod 8]
              XOR version[i mod 4] XOR (i mod 256), i = 0..2047
```

该 2048 字节模拟 KeyData 同时覆盖最大长度和多块上传路径。算法只用于可重复的软件测试，
不能作为真实 USBKey 算法或测试向量。

### GUI 页面

主窗口包含以下区域：

1. 连接栏：串口端点、刷新、波特率、连接/断开和状态。
2. 仿真提示：明确显示终端 SC1777Y 后端与 USBKey 后端是否为仿真。
3. 诊断页：终端版本、协议版本、EsamID、密钥版本、芯片版本、平台类型、刷新时间。
4. 更新前确认：显示当前身份、版本、连接状态、USBKey 预检和安全提示。
5. 引导进度：身份与 ERand1、USBKey 认证、AuthResult 与 ERand2、KeyData、UpdateResult。
6. 结果页：成功、明确失败、可重试失败或结果未知。
7. 日志抽屉：显示时间、事务 ID、阶段、长度、结果和错误码。

默认串口参数为 115200、8 数据位、无校验、1 停止位。端口与波特率可配置。

### 桌面状态

连接状态与维护状态分开建模：

```text
连接：DISCONNECTED -> CONNECTING -> READY
                         └───────> ERROR / LOST

维护：IDLE -> PREFLIGHT -> BEGIN -> AUTHENTICATING -> PACKAGING
             -> UPLOADING -> APPLYING -> VERIFYING -> SUCCEEDED
```

`APPLYING` 前断线允许清理后重新开始。`APPLYING` 后断线进入 `OUTCOME_UNKNOWN`，界面只
提供重新连接和查询状态。更新执行期间禁止重复启动。窗口在 APPLYING 阶段关闭时必须提示
用户结果可能未知，并先停止新任务、释放串口，再退出。

### 日志与敏感数据

日志存放在 Qt `QStandardPaths.AppLocalDataLocation` 下，采用按大小滚动文件。日志允许记录：

- 时间、终端端点和脱敏 EsamID；
- 事务 ID；
- 阶段、消息类型、块偏移和数据长度；
- AuthResult、UpdateResult、SMP 错误和 SC1777Y 错误类别。

日志禁止记录完整 ERand1、ERand2、enERand1、KeyData、原始 CBOR 包或未来 USBKey 句柄。
异常对象转为 UI 文本前也经过同一脱敏器。

桌面端对自己持有的 KeyData 使用 `bytearray`，流程结束后原位覆盖再释放。Python 运行时、
CBOR 编码器和串口库可能创建不可控副本，因此桌面端不能承诺进程内物理内存被完全清零；
真实 USBKey 适配器应尽量让密钥运算留在硬件内，并只返回既有流程要求的不透明材料。

## native_sim Sample

sample 使用 `uart0` 作为控制台并映射到 stdin/stdout，启用 `uart1` 并将
`zephyr,uart-mcumgr` 指向 `uart1`。`native_sim` 为 uart1 输出独立的 `/dev/pts/N`。
SC1777Y 节点继续挂在 `spi0`，通过 `sc1777y-0` alias 提供给管理组。

关键配置包括：

```text
CONFIG_NET_BUF=y
CONFIG_ZCBOR=y
CONFIG_CRC=y
CONFIG_BASE64=y
CONFIG_MCUMGR=y
CONFIG_MCUMGR_TRANSPORT_UART=y
CONFIG_MCUMGR_GRP_OS=y
CONFIG_MCUMGR_GRP_OS_MCUMGR_PARAMS=y
CONFIG_MCUMGR_GRP_OS_ECHO=n
CONFIG_REBOOT=n
CONFIG_SC1777Y_MGMT=y
CONFIG_SC1777Y_MAINTENANCE_SIMULATION=y
CONFIG_EMUL=y
CONFIG_SPI=y
CONFIG_SPI_EMUL=y
CONFIG_SC1777Y=y
CONFIG_SC1777Y_EMUL=y
CONFIG_UART_NATIVE_PTY_0_ON_STDINOUT=y
```

`smpclient` 7.3.0 在连接时读取 OS group 的 MCUmgr buffer 参数，因此 sample 只启用该参数
命令，并显式关闭不需要的 echo 和 reset 能力。SC1777Y 管理端不会因此暴露任意 shell、
文件、镜像或复位操作。

未来真实终端复用管理组，删除 emulator 配置，把 SC1777Y alias 指向真实 SPI 节点，把
`zephyr,uart-mcumgr` 指向真实 UART，并保持 simulation=false。

## 错误与恢复策略

- 端口不存在、占用、连接超时和意外断开显示独立连接错误，GUI 不冻结。
- diagnostics 是只读操作，可以由用户安全重试。
- authentication 的明确拒绝返回 `auth_result=false`，并销毁事务。
- KeyData 上传响应返回 `next_offset`；响应丢失后通过 status 确认偏移再继续。
- commit 不自动重试。明确拒绝返回 `update_result=false`；不确定错误进入
  `OUTCOME_UNKNOWN`。
- 事务默认 60 秒无活动超时。超时、取消、失败或成功后清理敏感材料。
- 终端重启会丢失 RAM 中的事务结果。此时旧 commit 的结果保持未知，不能猜测成功或失败。

## 依赖与兼容性

首版桌面目标是 Windows 10/11 x86-64，以及使用 glibc 2.34 或更新版本的 Linux x86-64
（包括 Ubuntu 22.04 或更新版本）。运行环境最低为 Python 3.10。应用依赖锁定为：

- `PySide6==6.11.1`
- `smpclient[serial]==7.3.0`
- `smp==4.1.0`
- `pyserial==3.5`

开发依赖包含 pytest、pytest-qt 和 Ruff，并通过 `uv.lock` 固定完整解析结果。升级 smpclient
或 smp 时必须先通过协议 golden test、PTY 集成测试和真机测试。

PySide6 采用 LGPL-3.0-only、GPL 或商业许可。发布二进制产品前必须按实际选用许可完成
法务与分发合规；首版源码运行和测试不制作可分发安装包。

## 测试策略

实现遵循测试驱动开发，生产行为必须先有失败测试。

### SC1777Y 驱动与 emulator

- 验证成功 apply 后，下一次 update identity 仍返回确定性 fixture Version[4]，且最后一个
  字节保持为 `0x00`。
- 保持现有协议帧和全部公共 API 测试通过。

### 管理组 ztest

- capabilities 和 diagnostics 字段、类型、长度与错误路径。
- update_begin 的事务 ID、EsamID、Version、ERand1 和超时。
- authenticate 成功返回 `auth_result=true` 与 ERand2。
- authenticate 拒绝返回 `auth_result=false`，不生成 ERand2，不允许上传。
- 缺字段、错误长度、错误事务和错误状态不调用 SC1777Y API。
- 分块顺序、完全相同重复块、偏移跳跃、总长度变化、越界和 CRC 错误。
- commit 对一个事务最多执行一次 apply。
- 明确 UpdateResult、OUTCOME_UNKNOWN、status、abort 和事务超时。
- errno 到管理组错误的映射以及响应缓冲区不足。
- 每个退出路径清理 staging buffer 和认证材料。

### 桌面协议与核心单元测试

- 自定义请求的 SMP header、group/command ID 和 CBOR golden bytes。
- 响应、SMP v1/v2 错误和畸形响应的 typed 解析。
- 成功流程调用顺序及数据从终端到 USBKey、再回到终端的传递。
- `auth_result=false` 后不调用 `generate_key_data()`，也不上传或 commit。
- 分块大小、进度、断线恢复和 CRC32。
- commit 响应丢失只查询 status，不重复 commit。
- simulation capability 对模拟 USBKey 的安全门控。
- 所有日志和异常文本的敏感字段脱敏。

### GUI 测试

pytest-qt 使用 fake workflow，`QT_QPA_PLATFORM=offscreen`：

- 未连接、连接中、已连接和断线状态下控件启用规则。
- 连接后自动读取并显示诊断。
- 引导步骤、进度、取消边界和重复点击防护。
- AuthResult、UpdateResult、错误和结果未知页面。
- 后台任务执行期间 Qt 事件循环仍可处理 UI 事件。
- 窗口关闭时正确停止工作线程并释放串口。

### PTY 端到端测试

Twister pytest harness 启动 `native_sim`，从控制台匹配 uart1 的 `/dev/pts/N`，然后使用
真正的桌面 `Sc1777ySmpClient` 连接该端点。覆盖：

- capabilities 与 diagnostics；
- 模拟 USBKey 的完整 5.2.1 流程；
- AuthResult 和 UpdateResult；
- 2048 字节 KeyData 分块；
- 成功后重新读取 diagnostics，Version[4] 保持文档约束的确定性值；
- 认证失败后没有 KeyData 命令；
- 重连与 status 查询。

PTY 端到端仅在 Linux 执行。纯 Python 核心和 offscreen GUI 测试在 Windows/Linux CI
执行。真实硬件到位后增加相同测试向真实 UART、真实 USBKey 和真实 SC1777Y 的适配层，
不复制业务测试。

## 验收标准

- `samples/drivers/sc1777y_maintenance` 在 native_sim 上构建并创建独立 SMP PTY。
- 桌面应用在 Linux 上通过该 PTY 连接、读取诊断并完成引导式更新。
- 桌面代码使用相同串口适配器接受 Windows `COM*` 和 Linux `/dev/tty*` 端点。
- 业务数据流与现有 5.2.1 sample 一致，响应明确包含 AuthResult、ERand2 和 UpdateResult。
- AuthResult=false 时没有 ERand2、KeyData 生成、上传或 commit。
- KeyData 最大 2048 字节，分块传输、偏移和整包 CRC32 都受到验证。
- commit 不会因客户端重试而对同一事务调用两次芯片更新。
- 更新成功后可以重新读取诊断，模拟 Version[4] 不因 apply 被修改，且最后一个字节保持为
  `0x00`。
- GUI 在所有串口、USBKey 和更新操作期间保持响应。
- 仿真 USBKey 不能连接 simulation=false 的终端执行更新。
- 日志不包含 ERand、enERand1、KeyData 或原始敏感包。
- 新增测试通过，现有 `tests/drivers/misc/sc1777y` 和
  `samples/drivers/sc1777y` 回归通过。

## 参考

- `samples/drivers/sc1777y/src/main.c`：现有 5.2.1 业务顺序。
- `include/zephyr/drivers/misc/sc1777y.h`：SC1777Y 公共语义 API。
- `tests/subsys/mgmt/mcumgr/handler_demo`：Zephyr 自定义 MCUmgr 管理组模式。
- `boards/native/native_sim/doc/index.rst`：native_sim PTY UART 行为。
- https://docs.zephyrproject.org/latest/services/device_mgmt/mcumgr_handlers.html
- https://github.com/intercreate/smpclient
- https://doc.qt.io/qtforpython-6/
