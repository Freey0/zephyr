# SC1777Y 样例说明书化设计

日期：2026-07-09

## 目标

将 `samples/drivers/sc1777y` 调整为面向新用户的说明书式样例。样例的重点是演示如何按 PDF 第 5 章的交互流程使用 SC1777Y 驱动，并清晰说明传感器、维护软件、平台、终端之间交换了哪些数据。

样例不负责验证模拟器的固定返回值、APDU 帧内容或错误路径。这些验证继续由 `tests/drivers/misc/sc1777y` 承担。

## 样例职责

样例应该做到：

- 以可执行代码展示第 5 章的交互流程。
- 在每个流程前说明 PDF 中的重要前置条件。
- 说明哪个用户向哪个用户发送哪些字段。
- 说明每个驱动 API 产生、消费或校验哪些字段。
- 只检查每个驱动 API 的返回值是否成功。
- 保留 `SC1777Y sample PASS`，让现有 console harness 仍能判断样例执行成功。

样例不应该做：

- 使用 `expect_equal` 比较模拟器固定数据。
- 使用 `expect_sequence` 检查模拟器确定性字节序列。
- 使用 `fill_xor` 复现模拟器的 XOR 行为。
- 把样例当作协议帧或模拟器输出的测试覆盖。

## 流程函数结构

每个 `run_5_x_x_*()` 函数按同一结构组织：

1. PDF 小节标题。
2. ASCII 风格流程图。
3. `Prerequisites:`，列出 PDF 中的协议或部署前置条件。
4. `Data exchanged:`，列出用户之间交换的字段。
5. `Guide:`，解释流程目的和驱动调用边界。
6. 本流程内部的数据变量。
7. `printf` 输出流程名称、前置条件和交换数据。
8. 步骤注释，将 PDF 步骤映射到驱动调用或用户间交付。
9. 驱动 API 调用和返回值检查。
10. 流程级 `PASS` 输出。

流程中使用的数据变量应放在对应流程函数内部。文件作用域只保留通用 helper。

## 注释风格

注释应像使用说明。

驱动调用步骤说明当前用户内部正在做什么：

```c
/* Flow step 1, Terminal internal operation: generate Rand1[4]. */
rc = sc1777y_get_random4(dev, rand1);
```

用户间交付步骤只描述数据流转，不解释没有驱动调用：

```c
/*
 * Flow step 2, Terminal-to-Sensor handoff:
 * send Rand1[4].
 */
printf("  Terminal -> Sensor: Rand1[4]\n");
```

注释不应把安全芯片作为外部参与方。样例中的可见用户是 Terminal、Sensor、Maintenance Software、Platform。只有在解释驱动调用时，才可以把芯片交互作为某个用户的内部操作提及。

## 运行输出风格

运行输出应作为流程 trace 使用。输出字段名和长度，不输出模拟器固定字节。

示例：

```text
[5.3.3] Session negotiation
  Prerequisite: terminal certificate exists
  Prerequisite: platform public key has been imported
  Prerequisite: platform type has been selected before auth response
  Terminal -> Platform: RequestMsg { DATA, RequestSign[64] }
    DATA { Type, SubType, Len, Ver, SN, SIM, ID, Cert1, EnR1[128] }
    Key driver data: EnR1[128], RequestHash[32], RequestSign[64]
  Platform -> Terminal: ResponseMsg { Type, SubType, Len, SN, AuthFactor[32], EnR2[128], ResponseSign[64] }
    Key driver data: AuthFactor[32], EnR2[128], ResponseHash[32], ResponseSign[64]
  Terminal -> Platform: ConfirmMsg { Type, SubType, Len, SN, AuthResult, DKHash[32] }
    Key driver data: AuthResult, DKHash[32]
[5.3.3] PASS
```

错误输出仍应指出失败的 API：

```text
5.3.3 session_begin failed: -5
SC1777Y sample FAIL
```

## 平台报文格式

与平台交互时，先保留 PDF 中的完整报文结构，再说明驱动重点处理的字段。

会话协商示例：

```c
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

非平台流程直接列出交换字段：

```c
 * Data exchanged:
 * - Terminal -> Sensor: Rand1[4]
 * - Sensor -> Terminal: sensorEsamID[8], Version[4], enRand1[8]
 * - Terminal -> Sensor: AuthResult
```

## 必要前置条件

每个流程都应在 `Prerequisites:` 中写出 PDF 明确要求或使用时必须知道的条件。至少包括：

- 5.1.1 身份认证：传感器和终端进行业务数据交互前，应先完成身份认证。
- 5.1.2 业务数据：终端处理传感器数据前，应维护传感器设备地址与 `sensorEsamID[8]` 的对应关系。
- 5.2.1 密钥更新/恢复：维护软件应具备密钥更新/恢复 USBKey 和接口库，并与终端建立现场维护通道。
- 5.3.1 基本指令：平台公钥、AK、IV 材料来自平台或平台配置流程。
- 5.3.2 证书请求：生成证书请求前，终端应生成或确认本地 SM2 密钥对。重新生成密钥对会覆盖旧密钥对，并需要重新申请证书。
- 5.3.3 会话协商：会话发起前，终端证书已存在，平台公钥已导入。生成安全认证响应前，平台类型已设置。
- 5.3.4 会话密钥加密：会话协商已成功，输入长度满足 16 字节块约束。
- 5.3.5 会话密钥解密：会话协商已成功，终端已从平台报文解析出 `IV[16]` 和密文数据。
- 5.3.6 平台类型选择：应在 5.3.3 中依赖平台类型的安全认证响应步骤之前执行。

## 测试策略

样例按“可执行说明书”测试：

- `sample.yaml` 继续使用 console harness 匹配 `SC1777Y sample PASS`。
- 样例只在每个驱动 API 调用后检查 `rc`。
- 样例不检查模拟器返回的固定字节。
- 模拟器行为、APDU 帧、错误路径、精确输出字节由 `tests/drivers/misc/sc1777y` 覆盖。

实现后运行：

```sh
scripts/twister -T samples/drivers/sc1777y -p native_sim --inline-logs
scripts/twister -T tests/drivers/misc/sc1777y -p native_sim --inline-logs
```

## 范围边界

本设计不新增 helper 库，也不新增第二个 sample。只重塑现有 sample，使其成为更清晰的使用说明；详细行为验证继续由现有测试套件承担。
