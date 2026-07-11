SC1777Y secure-channel tests
============================

The phase-one gate contains both the ztest unit/component suite and the
native_sim TAP end-to-end test.  Run the complete gate from the Zephyr source
directory with:

.. code-block:: console

   west twister -T tests/net/lib/sc1777y_secure_channel -p native_sim \
     --inline-logs --outdir build/twister_secure_channel_phase1

The TAP end-to-end case is mandatory.  Do not filter or skip it, and do not
treat a missing host dependency or insufficient privilege as a successful
test run.

Host prerequisites
------------------

* Run with root privileges or ``CAP_NET_ADMIN`` sufficient to create,
  configure, and remove a TAP interface.  The Zephyr ``net-setup.sh`` helper
  invokes ``sudo`` when it is not already running as root, so any required
  authorization must work non-interactively during the test.
* Set ``NET_TOOLS_BASE`` to the Zephyr net-tools directory containing an
  executable ``net-setup.sh``.  For example:

  .. code-block:: console

     export NET_TOOLS_BASE=/path/to/zephyrproject/tools/net-tools

* The only supported platform for this gate is ``native_sim``.
* Leave host TAP interface ``zeth`` available for the test fixture.  The
  fixture creates and owns ``zeth`` and assigns host address
  ``192.0.2.2/24`` for the duration of the test, then removes the interface.
  The native_sim device owns ``192.0.2.1/24``.  Do not pre-create ``zeth`` or
  assign either address to another interface while the gate is running.

The end-to-end application reaches ``SecurityGatewayPeer`` at
``192.0.2.2:18883`` through the full native_sim TCP/IP stack.  The peer
terminates the deterministic secure protocol and forwards plaintext to a
host TCP echo service.  The SC1777Y SPI emulator remains behind the public
driver API and is not controlled or inspected by this test.
