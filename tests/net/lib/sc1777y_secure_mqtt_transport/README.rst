###################################
SC1777Y secure MQTT transport tests
###################################

The default release gate runs all four native_sim test instances: the secure
channel unit suite, the secure-channel TAP echo end-to-end test, the MQTT
adapter unit suite, and the secure MQTT TAP end-to-end test against a real
Mosquitto Broker.  Run it from the Zephyr source directory exactly as follows:

.. code-block:: shell

   west twister \
     -T tests/net/lib/sc1777y_secure_channel \
     -T tests/net/lib/sc1777y_secure_mqtt_transport \
     -p native_sim --inline-logs \
     --outdir build/twister_sc1777y_secure_mqtt_all

Both TAP end-to-end cases are mandatory.  Do not use a test filter, tag
filter, ``--build-only``, or any other option that omits either case.  A
missing host dependency or insufficient privilege is a failed gate, not a
skip or successful run.

******************
Host prerequisites
******************

* Run as root, or configure ``sudo`` so that the stock Zephyr
  ``net-setup.sh`` can elevate non-interactively.  The stock helper checks its
  UID and unconditionally re-executes itself through ``sudo`` whenever it is
  not root.  Giving only ``CAP_NET_ADMIN`` to Twister is therefore not enough.
  ``CAP_NET_ADMIN`` can replace root only with a modified helper that does not
  impose that UID check and retains the capability while creating,
  configuring, and removing the TAP interface.
* Set ``NET_TOOLS_BASE`` to the Zephyr net-tools directory that contains an
  executable ``net-setup.sh``.  For example:

  .. code-block:: shell

     export NET_TOOLS_BASE=/path/to/zephyrproject/tools/net-tools

* Install the real Mosquitto Broker executable ``mosquitto`` and the official
  client tools ``mosquitto_sub`` and ``mosquitto_pub``.  The phase-two fixture
  does not use a Broker stub or substitute client implementation.
* Use ``native_sim`` and leave host TAP interface ``zeth`` available.  The
  fixtures create, configure, own, and remove ``zeth``; they assign
  ``192.0.2.2/24`` to the host side, while native_sim owns ``192.0.2.1/24``.
  Do not pre-create ``zeth`` or assign either address elsewhere while the gate
  is running.

The two TAP fixtures use the same cross-process advisory lock at
``/tmp/zephyr-sc1777y-zeth.lock``.  Twister may build and run unrelated tests
concurrently, but only one fixture can own ``zeth``; the lock is held from
before TAP setup until after gateway and TAP cleanup.

During each security session, the terminal maintains exactly one TCP
connection to ``SecurityGatewayPeer`` at ``192.0.2.2:18883``.  A reconnect
closes the failed connection and establishes a new connection and security
session; it never creates a simultaneous connection or a direct connection to
the Broker.  The peer completes the deterministic SC1777Y session handshake
and then transparently proxies decrypted MQTT bytes to the isolated local
Mosquitto Broker.  Test-side observation and downlink publication use
``mosquitto_sub`` and ``mosquitto_pub`` respectively.
