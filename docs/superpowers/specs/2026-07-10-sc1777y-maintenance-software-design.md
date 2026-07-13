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
- 在调用芯片更新前后持久化最小、非敏感事务记录，避免终端或桌面重启后重复应用。
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
- 应用结果与独立验证结果分开建模；芯片明确返回成功但没有文档定义的验证能力时，
  结果为 `APPLIED + NOT_SUPPORTED`，不能表述为“已验证”。
- 终端事务持久化由 `sc1777y_mgmt` 管理组负责；sample 只装配 Settings 后端和 flash
  分区，driver 与 emulator 均不感知持久化。
- `sc1777y_apply_key_update()` 首次调用前必须成功保存 `APPLYING` 提交屏障；保存失败时
  不得调用芯片。重启读到 `APPLYING` 时恢复为 `OUTCOME_UNKNOWN`。
- Settings 和桌面恢复日志只保存非敏感事务元数据，不保存 ERand、`enERand1`、KeyData、
  staging buffer 或原始协议载荷。

## 总体架构

```text
PySide6 桌面维护软件
  ├─ MainWindow / 引导式页面
  ├─ MaintenanceWorkflow
  ├─ RecoveryJournal（非敏感事务恢复日志）
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
       ├─ Settings 事务记录与启动恢复
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
└── src/
    ├── sc1777y_mgmt.c              CBOR handler、事务状态机和驱动调用
    └── sc1777y_mgmt_persistence.c  Settings 记录、提交屏障和启动恢复

samples/drivers/sc1777y_maintenance/
├── CMakeLists.txt
├── prj.conf
├── sample.yaml
├── README.rst
├── boards/native_sim.overlay       uart1 PTY + SC1777Y emulator + Settings 分区选择
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
│   │   ├── recovery_journal.py
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
  "independent_verification": false,
  "simulation": true | false
}
```

`simulation` 由 `CONFIG_SC1777Y_MAINTENANCE_SIMULATION` 决定。仿真 sample 设为 true，
真实终端配置必须为 false。首版 driver 和 emulator 都没有资料定义的独立更新验证方法，
因此 `independent_verification` 固定为 false。

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

更新后的 diagnostics 只用于刷新展示，不构成独立验证。资料没有定义 apply 后
`Version[4]` 的预期变化规则，因此 Version 相同或不同都不能把
`verification_result` 设置为 `VERIFIED` 或 `MISMATCH`；diagnostics 刷新失败也不能覆盖
已经取得的应用结果。

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

终端读取身份后生成 `ERand1[8]`。如果 RAM 中已有未结束事务，返回 `BUSY`。如果最近的
持久化结果为未解决的 `OUTCOME_UNKNOWN`，返回管理组错误 `OUTCOME_UNKNOWN`，不得创建
事务或覆盖记录。`APPLIED`、`FAILED` 等已闭合记录允许开始新事务，但在新事务到达
commit 前仍保留；新的 `APPLYING` 提交屏障通过一次 `settings_save_one()` 覆盖旧值。
因此在认证、上传或 commit 校验阶段重启时，终端仍保留上一条已闭合应用结果，而不会出现
删除旧记录后的空白窗口。

事务 ID 只用于关联、去重和防止旧请求串入新流程，不替代 USBKey 认证。运行时事务 ID
由 Zephyr `sys_rand_get()` 填充 8 字节；测试通过固定随机源获得可重复值。

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
`sc1777y_apply_key_update()`。调用顺序是强制的提交屏障：

1. 以单个 Settings 值保存该事务的 `APPLYING` 记录。
2. 只有保存成功后，才把 RAM 状态推进到 `APPLYING` 并首次调用
   `sc1777y_apply_key_update()`。
3. 芯片返回后，以 `APPLIED`、`FAILED` 或 `OUTCOME_UNKNOWN` 覆盖同一个 Settings 值；
   明确的最终业务响应必须等该记录保存成功后才能发送。

第一步保存失败返回 `PERSISTENCE`，且芯片调用次数必须为 0。最终记录保存失败时，虽然
本次调用可能已经取得芯片响应，但终端无法保证重启后仍能恢复这个结论，因此对外按
`OUTCOME_UNKNOWN` 处理，RAM 中记录 `error_code=PERSISTENCE` 并尽力保存该状态；即使
再次保存失败，原有 `APPLYING` 记录仍会在下次启动时恢复为未知。记录更新不得先删除旧
记录再写新记录。

明确成功：

```text
{
  "transaction_id": bstr[8],
  "apply_result": 1,
  "verification_result": 3
}
```

这里的组合是 `APPLIED + NOT_SUPPORTED`：它精确表示芯片明确返回
`UpdateResult=true`，但当前没有资料定义的独立验证方法。

芯片给出明确拒绝状态：

```text
{
  "transaction_id": bstr[8],
  "apply_result": 2,
  "verification_result": 0,
  "reason": uint
}
```

`APPLYING` 屏障保存成功后，driver 返回值固定映射如下：

| `sc1777y_apply_key_update()` 返回值 | state / 两条结果轴 | reason | status `error_code` | commit 对外结果 |
|---|---|---|---|---|
| `0` | `APPLIED / APPLIED + NOT_SUPPORTED` | null | null | 正常成功响应 |
| `-EACCES` | `FAILED / REJECTED + NOT_RUN` | `UPDATE_REJECTED` | null | 正常明确拒绝响应 |
| `-ENOTSUP` | `FAILED / REJECTED + NOT_RUN` | `UPDATE_NOT_SUPPORTED` | null | 正常明确不支持响应 |
| `-ETIMEDOUT` | `OUTCOME_UNKNOWN / UNKNOWN + NOT_SUPPORTED` | null | `DEVICE_TIMEOUT` | 管理组 `OUTCOME_UNKNOWN` |
| `-EIO`、`-EBADMSG`、`-ENOMEM` 或其他 SPI/响应错误 | `OUTCOME_UNKNOWN / UNKNOWN + NOT_SUPPORTED` | null | `DEVICE_IO` | 管理组 `OUTCOME_UNKNOWN` |
| 其他未预期负 errno | `OUTCOME_UNKNOWN / UNKNOWN + NOT_SUPPORTED` | null | `UNKNOWN` | 管理组 `OUTCOME_UNKNOWN` |

`-EACCES` 和 `-ENOTSUP` 只有在当前 driver 已经收到对应芯片状态字时才是明确拒绝；如果
driver 的 errno interface 将来改变，必须同步修改本表和协议测试。所有其他错误都发生在
已经越过持久化屏障之后，管理组无法仅凭 errno 证明命令是否发出，因此保守进入
`OUTCOME_UNKNOWN`。桌面端只能查询 `status`，不能自动再次 commit。最终记录保存失败时，
本表结果被 `UNKNOWN + NOT_SUPPORTED`、`error_code=PERSISTENCE` 覆盖。

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
  "apply_result": uint,
  "verification_result": uint,
  "reason": uint | null,
  "error_code": uint | null
}
```

`reason` 只用于 `AUTH_FAILED` 和明确的更新拒绝，其余状态为 null。`error_code` 使用本管理组
错误码：明确业务成功/拒绝时为 null，`OUTCOME_UNKNOWN` 时保存导致未知的具体错误类别；
如果只能确认“结果未知”而没有更具体类别，则为 `OUTCOME_UNKNOWN`。

事务进入结束状态并清理敏感材料后，不再占用活动事务槽。管理组分别维护最多一个 RAM
活动/提交前结果和一个最近的 commit 侧 Settings 记录；status 先按 transaction ID 匹配
RAM 记录，再匹配持久化记录。因此使用相应 ID 仍可查询 `AUTH_FAILED`、`APPLIED`、
`FAILED`、`OUTCOME_UNKNOWN` 或 `EXPIRED`，而不是把它伪装成 `IDLE`。

终端在 RAM 中保留最近事务的完整非敏感状态，并在 Settings 中保留最近一次进入芯片应用
阶段的最小恢复记录。认证、上传、取消和超时等尚未调用芯片的状态不持久化，重启后可以
安全回到 `IDLE`。`APPLYING`、`APPLIED`、`FAILED` 和 `OUTCOME_UNKNOWN` 按下一节规则
跨重启恢复。

#### 事务持久化

持久化属于 `sc1777y_mgmt`，sample 只提供 Settings 后端。管理组使用单一 key
`sc1777y_mgmt/txn`。schema 1 固定为以下 24 字节、little-endian 格式，不直接保存 C
struct：

```text
offset  size  field
0       4     magic = ASCII "S177"
4       1     schema_version = 1
5       8     transaction_id
13      1     state: APPLYING/APPLIED/FAILED/OUTCOME_UNKNOWN
14      1     apply_result
15      1     verification_result
16      1     reason，0 表示 null
17      1     error_code，0 表示 null
18      2     total_len，little-endian
20      4     IEEE CRC32，覆盖 offset 0..19，little-endian
```

记录不得包含 ERand、`enERand1`、KeyData、staging buffer、原始 CBOR 或 SC1777Y 原始帧。
整个记录通过一次 `settings_save_one()` 保存；状态转换通过覆盖同一个值完成。Settings/NVS
启用数据 CRC，记录自身也带 magic、schema 和 CRC，以便拒绝可见的截断、版本不兼容和
内容损坏。

持久化 adapter 的 interface 必须满足：保存成功返回时新值已经可恢复；覆盖已有记录时，
保存失败或保存中复位后的读取结果只能是旧的完整有效值、新的完整有效值或显式读取错误，
不能静默变成“key 不存在”。首次写入在返回成功前被中断时允许 key 仍不存在，因为调用方
尚未越过提交屏障、不得调用芯片；一旦首次保存成功返回，后续复位必须读到新值或显式错误。
当前 Settings/NVS 通过先写 data、后写 metadata 满足该契约。未来真实板更换 Settings
backend 时必须通过相同恢复测试，否则不受本设计支持。

在开放任何可改变状态的管理命令前，管理组必须成功调用 `settings_subsys_init()`，然后用
`settings_load_one()` 读取该单一记录。这里不使用 `settings_load_subtree()` 判断读取成功，
因为后者不会传播所有 backend load 错误。启动恢复规则为：

- key 不存在：没有历史 commit 侧记录，进入 `IDLE`；
- `APPLYING`：恢复为 `OUTCOME_UNKNOWN` 并尽力覆盖保存，禁止新更新；
- `APPLIED`、`FAILED` 或 `OUTCOME_UNKNOWN`：恢复对应最近结果，原 transaction ID 可查询；
- Settings 初始化/读取失败，或记录长度、magic、schema、CRC、枚举组合非法：进入持久化
  故障锁定，更新命令返回 `PERSISTENCE`，不能把故障当成“无记录”。

从上述 commit 侧记录恢复时，`auth_result` 固定可推导为 true，`next_offset` 从
`total_len` 恢复；reason 和 error_code 使用记录中的值。由 `APPLYING` 恢复出的结果使用
`apply_result=UNKNOWN`、`verification_result=NOT_SUPPORTED` 和
`error_code=OUTCOME_UNKNOWN`。

只读 capabilities 和 diagnostics 在持久化故障时仍可用。status 和更新命令必须先返回
`PERSISTENCE`，不能先按 transaction ID 返回 `TRANSACTION_NOT_FOUND`；唯一例外是 RAM 中
已有匹配且尚未进入 `APPLYING` 的事务，此时允许 abort 清理，因为该操作不写 Settings、
也不可能撤回芯片命令。单 key + NVS 的保证范围是复位和写入中断：最终记录覆盖被中断时
保留旧 `APPLYING`。它不宣称能区分任意介质位损坏与从未写入；真实产品如要求覆盖该故障
模型，需要另行设计双槽 generation 或 provisioning marker，不属于当前双端仿真首版。

#### abort

请求只包含 `transaction_id`。在 `APPLYING` 前允许取消；终端清理挑战、认证结果和
staging buffer。进入 `APPLYING` 后返回 `INVALID_STATE`，因为提交屏障已经越过，命令可能
即将发出或已经发出，abort 无法安全撤回。

### 业务结果值与状态值

`auth_result=false` 和 `apply_result=REJECTED` 的 `reason` 使用以下固定值：

| 值 | 名称 | 使用位置 |
|---:|---|---|
| 1 | `AUTH_REJECTED` | 芯片明确拒绝 `enERand1` |
| 2 | `UPDATE_REJECTED` | 芯片明确拒绝 KeyData |
| 3 | `UPDATE_NOT_SUPPORTED` | 芯片明确报告不支持更新命令 |

正常成功不携带 reason。超时、I/O、协议和状态错误不复用业务 reason，而使用管理组错误。

`apply_result` 使用以下固定值：

| 值 | 名称 | 含义 |
|---:|---|---|
| 0 | `NONE` | 尚未调用芯片或尚无最终应用结果 |
| 1 | `APPLIED` | 芯片明确返回 `UpdateResult=true`，且终端已持久化该结果 |
| 2 | `REJECTED` | 芯片明确拒绝更新，包括 `UpdateResult=false` 或命令不支持 |
| 3 | `UNKNOWN` | 终端无法提供可持久恢复的最终结论，包括响应不足或终态保存失败 |

`verification_result` 使用以下固定值：

| 值 | 名称 | 含义 |
|---:|---|---|
| 0 | `NOT_RUN` | 尚未执行或不适用 |
| 1 | `VERIFIED` | 独立证据确认预期状态 |
| 2 | `MISMATCH` | 独立检查明确不符合预期 |
| 3 | `NOT_SUPPORTED` | 当前终端/芯片没有资料定义的独立验证能力 |
| 4 | `UNKNOWN` | 支持独立验证，但本次无法得出结论 |

首版只允许以下组合；收到其他组合视为畸形响应或损坏的持久化记录：

| `state` | `auth_result` | `apply_result` | `verification_result` | `reason` | `error_code` |
|---|---|---|---|---|---|
| `IDLE` | null | `NONE` | `NOT_RUN` | null | null |
| `CHALLENGE_ISSUED` | null | `NONE` | `NOT_RUN` | null | null |
| `AUTHENTICATED` | true | `NONE` | `NOT_RUN` | null | null |
| `RECEIVING_KEY_DATA` | true | `NONE` | `NOT_RUN` | null | null |
| `APPLYING` | true | `NONE` | `NOT_RUN` | null | null |
| `APPLIED` | true | `APPLIED` | `NOT_SUPPORTED` | null | null |
| `AUTH_FAILED` | false | `NONE` | `NOT_RUN` | `AUTH_REJECTED` | null |
| `FAILED` | true | `REJECTED` | `NOT_RUN` | `UPDATE_REJECTED` 或 `UPDATE_NOT_SUPPORTED` | null |
| `OUTCOME_UNKNOWN` | true | `UNKNOWN` | `NOT_SUPPORTED` | null | 具体原因错误码 |
| `EXPIRED` | null 或 true | `NONE` | `NOT_RUN` | null | `TRANSACTION_EXPIRED` |

`NOT_SUPPORTED` 作为验证结果不是管理组错误。尤其不能因为缺少独立验证能力，把芯片已经
明确返回成功的 commit 改成 `NOT_SUPPORTED` 错误或失败。

公共 C 名称必须带枚举域前缀，避免 state、apply result、verification result 和管理组错误
中的同名值冲突，例如 `SC1777Y_TXN_STATE_APPLIED`、
`SC1777Y_APPLY_RESULT_APPLIED`、`SC1777Y_VERIFY_RESULT_NOT_SUPPORTED` 和
`SC1777Y_MGMT_ERR_NOT_SUPPORTED`。CBOR wire value 仍使用上表固定整数。

`status.state` 使用以下固定值：

| 值 | 名称 | 含义 |
|---:|---|---|
| 0 | `IDLE` | 没有活动事务 |
| 1 | `CHALLENGE_ISSUED` | 已返回 ERand1，等待 enERand1 |
| 2 | `AUTHENTICATED` | AuthResult 为真，等待 KeyData |
| 3 | `RECEIVING_KEY_DATA` | 正在接收分块 |
| 4 | `APPLYING` | 已持久化提交屏障，芯片调用即将开始或已经开始 |
| 5 | `APPLIED` | 芯片明确返回 UpdateResult 为真，但不隐含独立验证 |
| 6 | `AUTH_FAILED` | 最近事务认证被拒绝 |
| 7 | `FAILED` | 芯片明确拒绝或报告不支持更新 |
| 8 | `OUTCOME_UNKNOWN` | 无法提供可持久恢复的 apply 结论 |
| 9 | `EXPIRED` | 最近事务已超时 |

RAM 中的结束状态只保留事务 ID、状态、AuthResult、两条结果轴、reason、已接收长度和
错误类别，不保留 ERand 或 KeyData。完成上传后 `next_offset` 保留为 `total_len`；未进入
上传的结束状态为 0。Settings 中只保存上一节定义的更小记录。

### 事务状态机

```text
IDLE
  └─ update_begin ─> CHALLENGE_ISSUED
       ├─ authenticate(false) ─> AUTH_FAILED
       └─ authenticate(true)  ─> AUTHENTICATED
            └─ upload_chunk ─> RECEIVING_KEY_DATA
                 └─ commit 长度/CRC 校验成功
                      ├─ 保存 APPLYING 失败 ─> PERSISTENCE；不调用芯片
                      └─ 保存 APPLYING 成功 ─> APPLYING ─> 调用芯片一次
                           ├─ 明确成功且保存终态成功 ─> APPLIED
                           ├─ 明确拒绝且保存终态成功 ─> FAILED
                           └─ 调用结果不确定或终态保存失败 ─> OUTCOME_UNKNOWN

BOOT
  ├─ 无记录 ─> IDLE
  ├─ APPLYING ─> OUTCOME_UNKNOWN
  ├─ APPLIED / FAILED / OUTCOME_UNKNOWN ─> 恢复最近结果
  └─ 初始化、读取或记录校验失败 ─> PERSISTENCE_FAULT；禁止更新
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
| 9 | `DEVICE_TIMEOUT` | 等待芯片响应超时；apply 屏障后发生时结果未知 |
| 10 | `DEVICE_IO` | SPI 或响应校验错误；apply 屏障后发生时结果未知 |
| 11 | `NOT_SUPPORTED` | 芯片或终端不支持操作 |
| 12 | `OUTCOME_UNKNOWN` | 更新命令结果无法确认 |
| 13 | `PERSISTENCE` | Settings 初始化、读取或提交屏障写入失败 |

参数结构错误仍返回 MCUmgr 通用 `MGMT_ERR_EINVAL`。如果启用原始 SMP v1 支持，管理组
提供 `.mg_translate_error`，将上述错误映射为最接近的 MCUmgr 通用错误。保存 `APPLYING`
失败使用 `PERSISTENCE`，因为可以证明芯片尚未调用；芯片调用后的结果无法确认或最终记录
保存失败使用 `OUTCOME_UNKNOWN`，并在 status 的 `error_code` 中保留具体错误类别。

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
   │<─ apply_result,             │
   │   verification_result ──────│
```

`AuthResult=false` 时不返回 ERand2，桌面端不调用 USBKey 的 KeyData 生成能力，也不发送
任何 KeyData。当终端能够持久化并返回明确结果时，`apply_result` 精确映射芯片的
UpdateResult 语义；首版明确成功返回
`APPLIED + NOT_SUPPORTED`。随后重新读取 diagnostics 只刷新展示，不改变两条结果轴。

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
                     key_data_crc32: int) -> CommitResult: ...
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
5. 引导进度：身份与 ERand1、USBKey 认证、AuthResult 与 ERand2、KeyData 和芯片应用。
6. 结果页：分别显示 apply 与 verification 两条结果轴。`APPLIED + NOT_SUPPORTED` 的固定
   文案为“芯片报告更新成功，未独立验证”，它是可以关闭的最终结果，不允许重复 commit；
   `REJECTED + NOT_RUN` 按 reason 显示“芯片拒绝更新”或“芯片不支持更新命令”；
   `UNKNOWN + NOT_SUPPORTED` 显示结果未知并阻止新更新。
7. 日志抽屉：显示时间、事务 ID、阶段、长度、结果和错误码。

默认串口参数为 115200、8 数据位、无校验、1 停止位。端口与波特率可配置。

### 桌面状态

连接状态与维护状态分开建模：

```text
连接：DISCONNECTED -> CONNECTING -> READY
                         └───────> ERROR / LOST

维护：IDLE -> PREFLIGHT -> BEGIN -> AUTHENTICATING -> PACKAGING
             -> UPLOADING -> APPLYING -> REFRESHING_DIAGNOSTICS -> RESULT
                                   └──> RECOVERING ── status ─────> RESULT
```

`REFRESHING_DIAGNOSTICS` 只刷新界面数据，不叫 `VERIFYING`，也不改变 commit 已确定的结果；
刷新失败只显示附加警告。未来如果新增资料定义的独立验证 API，才能产生 `VERIFIED`、
`MISMATCH` 或验证 `UNKNOWN`，并且必须把 `independent_verification` 设为 true、更新合法组合
测试；不兼容的 wire 语义变更还必须提升 `protocol_version`。

`APPLYING` 前断线允许清理后重新开始。`APPLYING` 后断线只把桌面本地状态推进到
`RECOVERING`，不能仅因连接断开改写终端事务；重连后按 status 的 `APPLYING`、明确终态或
`OUTCOME_UNKNOWN` 分支处理。无法重连时界面显示“结果暂未确认”，只提供重新连接和查询
状态。更新执行期间禁止重复启动。窗口在 APPLYING 阶段关闭时必须提示用户结果可能未知，
并先停止新任务、释放串口，再退出。

### 桌面恢复日志

普通滚动日志不能承担恢复职责。桌面端在 `QStandardPaths.AppLocalDataLocation` 下维护一个
独立、非滚动的 `recovery-v1.json`，并通过临时文件、刷盘和原子替换更新。文件保存
`schema_version=1`、EsamID 的 SHA-256 指纹、endpoint hint、transaction ID、阶段、apply
result、verification result 和更新时间；endpoint 只用于提示，重新连接后必须用 EsamID
指纹确认终端身份。文件不得保存完整 EsamID、ERand、`enERand1`、KeyData 或原始 CBOR。

发送 commit 前必须先把阶段原子保存为 `APPLYING`。保存失败时不发送 commit。桌面进程
重新启动后，如发现未闭合记录，先要求连接同一终端并用原 transaction ID 查询 status，
绝不自动重发 commit。终端返回 `APPLIED`、`FAILED` 后可以闭合；能够证明芯片尚未调用的
commit 前错误仍须使用原 transaction ID 成功 abort，才能把恢复日志标记为已闭合。
`OUTCOME_UNKNOWN` 继续保留并阻止从桌面发起新更新。

恢复日志不存在表示没有待恢复事务。读取失败、未知 schema、字段非法或终端 EsamID
不匹配时必须 fail closed，不能把日志当成不存在，也不能向另一台终端查询或发起更新。
闭合日志的原子写入失败时保留未闭合记录并显示恢复警告；重新启动后仍按 status 查询，
不因为本地日志更新失败重复 commit。

同一终端的恢复判定固定为：

| status/错误 | 桌面动作 | 恢复结论 |
|---|---|---|
| `APPLIED` / `FAILED` | 采用终端持久化结果并关闭 journal | 对应两条结果轴 |
| `CHALLENGE_ISSUED` / `AUTHENTICATED` / `RECEIVING_KEY_DATA` | 发送 abort；仅 abort 成功后关闭 journal | `NONE + NOT_RUN` |
| `AUTH_FAILED` / `EXPIRED` | 关闭 journal | `NONE + NOT_RUN` |
| 本次 commit 直接返回提交屏障前的 `PERSISTENCE`、长度或 CRC 错误 | 发送 abort；仅 abort 成功后关闭 journal | `NONE + NOT_RUN` |
| `APPLYING` | 保持 journal 并继续轮询 | 暂无最终结论 |
| `OUTCOME_UNKNOWN` | 保持 journal，阻止新更新 | `UNKNOWN + NOT_SUPPORTED` |
| 恢复查询返回 `PERSISTENCE`、`TRANSACTION_NOT_FOUND`、查询失败或 EsamID 不匹配 | 保持 journal，进入人工恢复 | 不得推断 `NONE` |

`TRANSACTION_NOT_FOUND` 可能表示旧结果已被另一维护流程的 commit 覆盖，不能证明芯片从未
调用。此类人工恢复只能在操作员审阅终端与桌面审计记录后，把 journal 归档为
`ADMINISTRATIVELY_RESET`；归档保留“结果未知”，不能改写成 `NONE`、`APPLIED` 或
`REJECTED`。如果终端自身没有 `OUTCOME_UNKNOWN` 或 `PERSISTENCE_FAULT`，不应为了解除
单个桌面 journal 而擦除终端 flash。任何恢复分支都不自动发送 commit。

上述所有 abort 分支如果 abort 失败、连接中断或返回全局 `PERSISTENCE`，都必须保留
journal 并进入人工恢复，不能因“芯片尚未调用”而遗失仍占用活动槽的 transaction ID。

### 日志与敏感数据

日志存放在 Qt `QStandardPaths.AppLocalDataLocation` 下，采用按大小滚动文件。日志允许记录：

- 时间、终端端点和脱敏 EsamID；
- 事务 ID；
- 阶段、消息类型、块偏移和数据长度；
- AuthResult、apply result、verification result、SMP 错误和 SC1777Y 错误类别。

日志禁止记录完整 ERand1、ERand2、enERand1、KeyData、原始 CBOR 包或未来 USBKey 句柄。
异常对象转为 UI 文本前也经过同一脱敏器。

桌面端对自己持有的 KeyData 使用 `bytearray`，流程结束后原位覆盖再释放。Python 运行时、
CBOR 编码器和串口库可能创建不可控副本，因此桌面端不能承诺进程内物理内存被完全清零；
真实 USBKey 适配器应尽量让密钥运算留在硬件内，并只返回既有流程要求的不透明材料。

## native_sim Sample

sample 使用 `uart0` 作为控制台并映射到 stdin/stdout，启用 `uart1` 并将
`zephyr,uart-mcumgr` 指向 `uart1`。`native_sim` 为 uart1 输出独立的 `/dev/pts/N`。
SC1777Y 节点继续挂在 `spi0`，通过 `sc1777y-0` alias 提供给管理组。

`native_sim` 已提供 2 MiB `flash0`、4 KiB erase block 和 16 KiB
`storage_partition`。sample 不创建新 flash，也不挂载文件系统；overlay 只通过
`zephyr,settings-partition = &storage_partition` 显式选择现有分区，Settings/NVS 直接在
该分区上保存记录。模拟 flash 默认由工作目录中的 `flash.bin` 作为宿主 backing file，也
可以通过 native_sim 的 `--flash=<绝对路径>` 指定隔离文件。

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
CONFIG_FLASH=y
CONFIG_FLASH_MAP=y
CONFIG_NVS=y
CONFIG_NVS_DATA_CRC=y
CONFIG_SETTINGS=y
CONFIG_SETTINGS_NVS=y
CONFIG_SETTINGS_NVS_SECTOR_SIZE_MULT=1
CONFIG_SETTINGS_NVS_SECTOR_COUNT=4
CONFIG_SC1777Y_MGMT=y
CONFIG_SC1777Y_MAINTENANCE_SIMULATION=y
CONFIG_EMUL=y
CONFIG_SPI=y
CONFIG_SPI_EMUL=y
CONFIG_SC1777Y=y
CONFIG_SC1777Y_EMUL=y
CONFIG_UART_NATIVE_PTY_0_ON_STDINOUT=y
```

`SC1777Y_MGMT` 的 Kconfig 必须 `depends on SETTINGS`，并为应用层 record CRC
`select CRC`。具体 Settings backend 仍由 sample/产品配置选择，管理组不 `select NVS`。

静态单记录方案不需要 `CONFIG_SETTINGS_RUNTIME`。`CONFIG_FLASH_SIMULATOR` 由 native_sim
的 sim-flash devicetree 节点自动启用；`CONFIG_MPU_ALLOW_FLASH_WRITE` 也不是 native_sim
要求。不得启用 `CONFIG_NVS_INIT_BAD_MEMORY_REGION` 自动擦除损坏分区，因为这可能静默
删除尚未解决的事务记录。

`smpclient` 7.3.0 在连接时读取 OS group 的 MCUmgr buffer 参数，因此 sample 只启用该参数
命令，并显式关闭不需要的 echo 和 reset 能力。SC1777Y 管理端不会因此暴露任意 shell、
文件、镜像或复位操作。

未来真实终端复用管理组，删除 emulator 配置，把 SC1777Y alias 指向真实 SPI 节点，把
`zephyr,uart-mcumgr` 指向真实 UART，装配该板的 Settings backend/分区，并保持
simulation=false。记录格式和恢复状态机不随存储装配改变。

## 错误与恢复策略

- 端口不存在、占用、连接超时和意外断开显示独立连接错误，GUI 不冻结。
- diagnostics 是只读操作，可以由用户安全重试。
- authentication 的明确拒绝返回 `auth_result=false`，并销毁事务。
- KeyData 上传响应返回 `next_offset`；响应丢失后通过 status 确认偏移再继续。
- commit 不自动重试。明确成功返回 `APPLIED + NOT_SUPPORTED`，明确拒绝返回
  `REJECTED + NOT_RUN`；不确定错误进入 `OUTCOME_UNKNOWN`。
- 事务默认 60 秒无活动超时。超时、取消、失败或成功后清理敏感材料。
- 终端重启会丢失未持久化的挑战、认证、上传缓冲区和提交前状态；这些状态尚未调用芯片，
  可以安全重新开始。明确的持久化结果跨重启可查询；`APPLYING` 一律恢复为
  `OUTCOME_UNKNOWN`，不能猜测成功或失败。
- Settings 初始化或提交屏障保存失败时 fail closed。调用芯片前的保存失败返回
  `PERSISTENCE` 且不调用芯片；调用芯片后的终态保存失败按 `OUTCOME_UNKNOWN` 处理。
- 未解决的 `OUTCOME_UNKNOWN` 不能被 abort、重启或下一次 `update_begin` 清除。SC1777Y
  管理协议 v1 不提供远程 clear 命令。

### native_sim 受控管理复位

首版只为仿真环境定义人工授权的管理复位，用于解除 `OUTCOME_UNKNOWN` 或
`PERSISTENCE_FAULT`，固定步骤如下：

1. 停止 sample，禁止并发桌面连接。
2. 归档终端 EsamID 指纹、transaction ID、原错误和“结果仍未知”的审计记录。
3. 显式更换或擦除该用例的 native_sim flash backing file。
4. 由操作员把匹配的桌面恢复 journal 标记为 `ADMINISTRATIVELY_RESET` 并归档；不得改写成
   `APPLIED`、`REJECTED` 或 `NONE`。
5. 重新启动 sample，确认 Settings 健康且无待恢复记录后，才允许新更新。

该流程只解除维护锁，不证明此前更新成功或失败。接入真实硬件前必须另行提供经过认证的
维修解除流程，不能复用普通更新 interface，也不能把擦除 Settings 暴露为未认证的 SMP
命令。

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

- emulator 明确返回 apply 成功后，下一次 update identity 仍返回确定性 fixture
  Version[4]，且最后一个字节保持为 `0x00`；该读取不被解释为独立验证。
- 保持现有协议帧和全部公共 API 测试通过。

### 管理组 ztest

- capabilities 和 diagnostics 字段、类型、长度与错误路径。
- update_begin 的事务 ID、EsamID、Version、ERand1 和超时。
- authenticate 成功返回 `auth_result=true` 与 ERand2。
- authenticate 拒绝返回 `auth_result=false`，不生成 ERand2，不允许上传。
- 缺字段、错误长度、错误事务和错误状态不调用 SC1777Y API。
- 分块顺序、完全相同重复块、偏移跳跃、总长度变化、越界和 CRC 错误。
- commit 对一个事务最多执行一次 apply。
- commit 明确成功返回 `APPLIED + NOT_SUPPORTED`，明确拒绝返回 `REJECTED + NOT_RUN`；
  非法结果组合被拒绝。
- Settings 记录的显式序列化、schema、长度、magic、CRC 和枚举组合校验。
- 持久化 adapter 覆盖已有记录中断后只能读到旧完整值、新完整值或显式错误；首次保存中断
  可以无 key，但必须发生在 apply 调用前。
- 保存 `APPLYING` 失败时 apply 调用次数为 0；测试观察到 `APPLYING` 已保存后，才允许
  apply 调用次数变为 1。
- 新事务在 commit 屏障前不删除旧 durable result；到达屏障后用新 `APPLYING` 单值覆盖。
- 启动加载 `APPLYING` 映射为 `OUTCOME_UNKNOWN`；`APPLIED`、`FAILED` 和
  `OUTCOME_UNKNOWN` 跨重启可查询。
- 未解决的 `OUTCOME_UNKNOWN` 拒绝 `update_begin`，且不能被 abort 覆盖。
- 最终记录保存失败时对外为 `OUTCOME_UNKNOWN`，原 `APPLYING` 仍可用于下次恢复。
- Settings 初始化、读取错误以及损坏或未知 schema 记录均 fail closed。
- 持久化故障时 capabilities/diagnostics 可用，status 和更新命令返回 `PERSISTENCE`；仅匹配
  RAM 提交前事务的 abort 仍可成功清理。
- 持久化记录不包含挑战值、KeyData、staging buffer 或原始协议包。
- status、abort、事务超时和 reason/error_code 的恢复语义。
- apply driver 每一种 errno 同时断言 state、apply result、verification result、reason、
  error_code 和顶层管理组响应；另覆盖响应缓冲区不足。
- 每个退出路径清理 staging buffer 和认证材料。

### 桌面协议与核心单元测试

- 自定义请求的 SMP header、group/command ID 和 CBOR golden bytes。
- 响应、SMP v1/v2 错误和畸形响应的 typed 解析。
- apply/verification 枚举数值、合法组合和非法组合的协议 golden test。
- 成功流程调用顺序及数据从终端到 USBKey、再回到终端的传递。
- `auth_result=false` 后不调用 `generate_key_data()`，也不上传或 commit。
- 分块大小、进度、断线恢复和 CRC32。
- commit 响应丢失、桌面重启和恢复日志存在时只查询 status，不重复 commit。
- commit 前恢复日志保存失败时不发送 commit；闭合结果后正确关闭记录，未知结果继续保留。
- commit 屏障前的长度、CRC 或 `PERSISTENCE` 错误必须先 abort；abort 失败时保留 journal。
- 恢复日志损坏、未知 schema、闭合写入失败和终端 EsamID 不匹配时 fail closed。
- 恢复查询返回提交前状态、`EXPIRED`、`TRANSACTION_NOT_FOUND`、`APPLYING` 和各终态时，
  严格执行恢复判定矩阵。
- `TRANSACTION_NOT_FOUND`、查询失败和恢复期 `PERSISTENCE` 一律进入人工恢复，不能推断
  `NONE`；受控管理复位同时归档 UNKNOWN journal 并标记 `ADMINISTRATIVELY_RESET`。
- diagnostics 刷新和固定 Version[4] 不改变 verification result；刷新失败不覆盖已确定的
  apply result。
- simulation capability 对模拟 USBKey 的安全门控。
- 普通日志、恢复日志和异常文本的敏感字段脱敏。

### GUI 测试

pytest-qt 使用 fake workflow，`QT_QPA_PLATFORM=offscreen`：

- 未连接、连接中、已连接和断线状态下控件启用规则。
- 连接后自动读取并显示诊断。
- 引导步骤、进度、取消边界和重复点击防护。
- AuthResult、错误和结果未知页面。
- `APPLIED + NOT_SUPPORTED` 精确显示“芯片报告更新成功，未独立验证”，且不出现“已验证”
  或重试 commit 操作。
- `UPDATE_REJECTED` 与 `UPDATE_NOT_SUPPORTED` 显示不同的明确失败文案。
- 启动发现未闭合恢复日志时进入查询恢复流程并禁止新更新。
- 受控管理复位必须经过显式操作员确认，且结果页仍保留“此前结果未知”。
- 后台任务执行期间 Qt 事件循环仍可处理 UI 事件。
- 窗口关闭时正确停止工作线程并释放串口。

### PTY 端到端测试

Twister pytest harness 启动 `native_sim`，从控制台匹配 uart1 的 `/dev/pts/N`，然后使用
真正的桌面 `Sc1777ySmpClient` 连接该端点。覆盖：

- capabilities 与 diagnostics；
- 模拟 USBKey 的完整 5.2.1 流程；
- AuthResult，以及 `APPLIED + NOT_SUPPORTED` 结果组合；
- 2048 字节 KeyData 分块；
- 成功后重新读取 diagnostics，Version[4] 保持文档约束的确定性值，结果仍为
  `APPLIED + NOT_SUPPORTED`；
- 认证失败后没有 KeyData 命令；
- 重连与 status 查询；
- 每个持久化用例使用首次启动前不存在的唯一绝对 `--flash=<path>`，避免并行测试共享默认
  `flash.bin`；
- 仅测试配置启用管理组内部阻塞钩子：`settings_save_one(APPLYING)` 成功后、调用 driver 前
  阻塞。测试确认第一进程已阻塞后用 SIGKILL 终止；钩子位于 management/sample 测试装配，
  不修改 emulator；
- 第二进程复用同一 flash 文件，且不得使用 `--flash_erase`、`--flash_rm` 或
  `--flash_in_ram`。status 返回 `OUTCOME_UNKNOWN`，`update_begin` 被拒绝；
- 对上述 UNKNOWN 执行受控管理复位，确认 flash 与桌面 journal 协调归档后才恢复
  `update_begin`，且审计结果仍为未知；
- `APPLIED` 使用相同 flash 文件跨进程恢复；`FAILED` 的恢复由管理组 ztest 覆盖；
- 原始 flash backing file 不包含模拟 ERand、`enERand1` 或 KeyData fixture。

PTY 端到端仅在 Linux 执行。纯 Python 核心和 offscreen GUI 测试在 Windows/Linux CI
执行。上述 SIGKILL 用例只验证 `APPLYING` 已持久化后的 native_sim 进程重启；Settings
覆盖写入中断由管理组的 backend fault injection 测试验证。两者都不代表宿主机断电刷盘
保证。真实硬件到位后增加相同测试向真实 UART、真实 USBKey 和真实 SC1777Y 的适配层，
不复制业务测试。

## 验收标准

- `samples/drivers/sc1777y_maintenance` 在 native_sim 上构建并创建独立 SMP PTY。
- 桌面应用在 Linux 上通过该 PTY 连接、读取诊断并完成引导式更新。
- 桌面代码使用相同串口适配器接受 Windows `COM*` 和 Linux `/dev/tty*` 端点。
- 业务数据流与现有 5.2.1 sample 一致，响应明确包含 AuthResult、ERand2，以及区分芯片
  明确成功、明确拒绝、不支持和未知的 apply result。
- AuthResult=false 时没有 ERand2、KeyData 生成、上传或 commit。
- KeyData 最大 2048 字节，分块传输、偏移和整包 CRC32 都受到验证。
- commit 不会因客户端重试而对同一事务调用两次芯片更新。
- sample 使用 native_sim 已有模拟 flash 的 `storage_partition` 和 Settings/NVS，不挂载
  文件系统，也不改变 driver/emulator 行为。
- 调用 SC1777Y apply 前已成功持久化 `APPLYING`；提交屏障保存失败时芯片调用次数为 0。
- 终端重启后明确结果仍可查询，`APPLYING` 恢复为 `OUTCOME_UNKNOWN`，未解决的未知结果
  阻止新 `update_begin`。
- 桌面重启发现未闭合恢复日志时只查询同一终端的 status，不自动重发 commit。
- `TRANSACTION_NOT_FOUND` 不被解释为“未调用芯片”；native_sim 受控管理复位必须同时归档
  terminal 记录和桌面 journal，且不改写此前未知结果。
- emulator 完整流程结果为 `APPLIED + NOT_SUPPORTED`，GUI 显示“芯片报告更新成功，未独立验证”。
- 芯片明确返回 apply 成功后可以重新读取诊断，模拟 Version[4] 不因 apply 被修改，且最后
  一个字节保持为 `0x00`；该读取不升级为 `VERIFIED`。
- GUI 在所有串口、USBKey 和更新操作期间保持响应。
- 仿真 USBKey 不能连接 simulation=false 的终端执行更新。
- Settings 记录、普通日志和桌面恢复日志均不包含 ERand、enERand1、KeyData 或原始敏感包。
- 新增测试通过，现有 `tests/drivers/misc/sc1777y` 和
  `samples/drivers/sc1777y` 回归通过。

## 参考

- `samples/drivers/sc1777y/src/main.c`：现有 5.2.1 业务顺序。
- `include/zephyr/drivers/misc/sc1777y.h`：SC1777Y 公共语义 API。
- `drivers/misc/sc1777y/sc1777y.c`：SC1777Y 状态字、errno 和 apply 调用语义。
- `tests/subsys/mgmt/mcumgr/handler_demo`：Zephyr 自定义 MCUmgr 管理组模式。
- `boards/native/native_sim/native_sim.dts`：native_sim 模拟 flash 和 `storage_partition`。
- `boards/native/native_sim/doc/index.rst`：native_sim PTY UART 和 flash backing file 行为。
- `subsys/settings/src/settings_store.c`：单值 Settings 加载、保存和错误传播。
- `subsys/settings/src/settings_nvs.c`：Settings/NVS 分区选择和 backend 初始化。
- `doc/services/storage/nvs/nvs.rst`：NVS 写入与掉电恢复行为。
- https://docs.zephyrproject.org/latest/services/device_mgmt/mcumgr_handlers.html
- https://github.com/intercreate/smpclient
- https://doc.qt.io/qtforpython-6/
