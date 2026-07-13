.. zephyr:code-sample:: sc1777y
   :name: SC1777Y driver flow guide

   按 SC1777Y 文档第 5 章交互流程演示驱动用法。

概览
****

本样例用于指导新用户按交互流程使用 SC1777Y 驱动。样例不是 emul
单元测试，重点不是验证 emul 行为，而是说明终端、传感器、平台和维护
软件之间交换哪些数据，以及每一步应调用哪个驱动 API。

样例按文档第 5 章组织，每个流程都包含：

* ASCII 流程图
* 前置条件
* 用户之间交换的数据
* 与平台交互时的 PDF 报文格式
* 每个步骤对应的驱动调用

运行环境
********

样例默认在 ``native_sim`` 上运行，并通过 SC1777Y SPI emulator 提供可重复的
响应数据。这样可以先学习驱动调用顺序和流程边界，再把相同调用迁移到真实
硬件平台。

Devicetree overlay 会提供 ``sc1777y-0`` alias：

.. code-block:: devicetree

   aliases {
           sc1777y-0 = &sc1777y0;
   };

构建和运行
**********

如果本分支已经合入当前 Zephyr checkout，可以在 Zephyr 工作区中运行：

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/sc1777y
   :board: native_sim
   :goals: build run
   :compact:

如果在 ``.worktrees/sc1777y-driver`` 这样的 git worktree 中验证本分支，
需要让 ``ZEPHYR_BASE`` 指向当前 worktree。否则 ``west`` 会按原工作区
解析 Zephyr base，可能读不到本分支新增的 Devicetree vendor prefix 和
binding。

在当前 worktree 中运行：

.. code-block:: console

   ZEPHYR_BASE=$PWD west twister -T samples/drivers/sc1777y -p native_sim --inline-logs

期望输出以每个流程的标题开始，并以如下内容结束：

.. code-block:: console

   SC1777Y sample PASS

如何阅读样例
************

从 ``src/main.c`` 的 ``main()`` 开始阅读。``main()`` 按交互流程依次调用
每个 ``run_5_x_x_*()`` 函数。

每个流程函数内部都按同一结构组织：

* 先用注释给出流程图、前置条件和交换数据。
* 再用 ``printf`` 打印当前流程的关键边界。
* 最后按步骤调用驱动 API。

注释中的“终端”“传感器”“平台”“维护软件”表示系统中的用户边界。
安全芯片只是某个用户内部被操作的对象，因此只在具体步骤说明中出现。

流程索引
********

5.1.1 身份认证流程
==================

入口函数：``run_5_1_1_identity_auth()``

用途：终端向传感器发送 ``Rand1[4]``，传感器返回
``sensorEsamID[8]``、``Version[4]`` 和 ``enRand1[8]``，终端验证后
把 ``AuthResult`` 交给传感器。

关键驱动调用：

* ``sc1777y_get_random4()``
* ``sc1777y_get_sensor_identity()``
* ``sc1777y_encrypt_sensor_challenge()``
* ``sc1777y_verify_sensor_auth()``

5.1.2 业务数据流程
==================

入口函数：``run_5_1_2_business_data()``

用途：演示传感器到终端、终端到传感器两个方向的业务数据加解密。
终端侧调用必须使用与传感器匹配的 ``sensorEsamID[8]``。

关键驱动调用：

* ``sc1777y_sensor_encrypt()``
* ``sc1777y_terminal_decrypt_sensor()``
* ``sc1777y_terminal_encrypt_sensor()``
* ``sc1777y_sensor_decrypt_from_terminal()``

5.2.1 密钥更新/恢复流程
=======================

入口函数：``run_5_2_1_key_update()``

用途：维护软件请求终端身份信息，终端提供 ``EsamID[8]``、
``Version[4]`` 和 ``ERand1[8]``；维护软件返回 ``enERand1[8]``，
终端验证后再应用 ``KeyData[len]``。

关键驱动调用：

* ``sc1777y_get_update_identity()``
* ``sc1777y_get_random8()``
* ``sc1777y_verify_update_auth()``
* ``sc1777y_apply_key_update()``

5.3.1 平台基础指令流程
======================

入口函数：``run_5_3_1_platform_basic()``

用途：演示终端读取版本、序列号和随机数，以及从平台侧获得并导入
``PlatformPublicKey[64]``、``AK[16]`` 和 ``IV[16]``。

关键驱动调用：

* ``sc1777y_get_version_info()``
* ``sc1777y_get_serial()``
* ``sc1777y_get_random()``
* ``sc1777y_import_platform_public_key()``
* ``sc1777y_import_ak()``
* ``sc1777y_import_iv()``

5.3.2 证书请求流程
==================

入口函数：``run_5_3_2_certificate_request()``

用途：平台发起证书申请，终端生成或确认 SM2 密钥对，读取 ``Serial[8]``，
生成 ``CSR[len]`` 并交给平台。

关键驱动调用：

* ``sc1777y_generate_sm2_keypair()``
* ``sc1777y_get_serial()``
* ``sc1777y_generate_cert_request()``

5.3.6 平台类型选择流程
======================

入口函数：``run_5_3_6_platform_type()``

用途：终端在生成 5.3.3 的认证响应前选择平台类型，并读取确认。

关键驱动调用：

* ``sc1777y_set_platform_type()``
* ``sc1777y_get_platform_type()``

5.3.3 会话协商流程
==================

入口函数：``run_5_3_3_session_negotiation()``

前置条件：

* 终端证书已经存在。
* 平台公钥已经导入终端安全芯片。
* 生成认证响应前，已经完成平台类型选择。

用途：终端生成 ``RequestMsg`` 并交给平台，平台返回 ``ResponseMsg``，
终端验证平台签名、生成认证响应、确认会话，再把 ``ConfirmMsg`` 交给平台。

PDF 报文格式在样例中保留为：

.. code-block:: text

   RequestMsg { DATA, RequestSign[64] }
   DATA { Type, SubType, Len, Ver, SN, SIM, ID, Cert1, EnR1[128] }
   ResponseMsg { Type, SubType, Len, SN, AuthFactor[32], EnR2[128], ResponseSign[64] }
   ConfirmMsg { Type, SubType, Len, SN, AuthResult, DKHash[32] }

关键驱动调用：

* ``sc1777y_session_begin()``
* ``sc1777y_hash()``
* ``sc1777y_sign_hash()``
* ``sc1777y_verify_signature()``
* ``sc1777y_generate_auth_response()``
* ``sc1777y_session_confirm()``

5.3.4 会话密钥加密流程
======================

入口函数：``run_5_3_4_session_key_encryption()``

用途：终端导入 ``IV[16]``，使用会话密钥加密 ``DATA[16]``，并把
``RequestMsg { Type, SubType, Len, IV[16], ResponseData[ciphertext] }``
交给平台。

关键驱动调用：

* ``sc1777y_get_random()``
* ``sc1777y_import_iv()``
* ``sc1777y_session_encrypt()``

5.3.5 会话密钥解密流程
======================

入口函数：``run_5_3_5_session_key_decryption()``

用途：平台把 ``RequestMsg { Type, SubType, Len, IV[16],
RequestData[ciphertext] }`` 交给终端，终端导入 ``IV[16]`` 并解密
``RequestData[ciphertext]``。

关键驱动调用：

* ``sc1777y_import_iv()``
* ``sc1777y_session_decrypt()``

迁移到真实硬件
**************

把样例迁移到真实硬件时，需要完成以下准备：

* 在目标板 overlay 中声明真实 SPI 设备，并提供 ``sc1777y-0`` alias。
* 根据硬件连接设置 ``spi-max-frequency``、片选和 SPI mode。
* 关闭 emulator 配置，启用实际 SC1777Y 驱动。
* 用真实平台提供的公钥、AK、IV、证书、报文和密钥更新包替换样例中的
  示例数据。

样例中的流程函数可以作为迁移模板：保留用户之间的数据交付边界，把示例
数据替换成实际业务数据即可。
