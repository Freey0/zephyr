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

* Run as root, or configure ``sudo`` so that the stock Zephyr
  ``net-setup.sh`` can elevate non-interactively.  That helper unconditionally
  re-executes itself through ``sudo`` whenever its UID is not zero, so granting
  ``CAP_NET_ADMIN`` only to the Twister process is not sufficient with the
  stock helper.  ``CAP_NET_ADMIN`` can replace root only when using a modified
  helper which does not impose that UID check and which retains the capability
  while creating, configuring, and removing the TAP interface.
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
