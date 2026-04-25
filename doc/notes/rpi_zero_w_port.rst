Raspberry Pi Zero W Port Notes
##############################

This note is the current handoff summary for the Zephyr Raspberry Pi Zero W
port. The goal so far has been the minimal path toward boot and first useful
validation across reset, vectors, interrupt controller, timer, UART, and now
initial GPIO and logging:
``samples/hello_world``, ``samples/drivers/uart/echo_bot``,
``samples/basic/blinky``, ``samples/basic/button``,
``samples/basic/sys_heap``, ``samples/basic/threads``,
``samples/basic/hash_map``, ``samples/subsys/logging/logger``,
``samples/kernel/msg_queue``,
``samples/kernel/condition_variables/simple``, and
``samples/kernel/condition_variables/condvar``.

Current State
*************

- ``rpi_zero_w/bcm2835`` now builds successfully and produces ``zephyr.elf``.
- ``samples/hello_world`` has now been observed on real Raspberry Pi Zero W
  hardware over the mini-UART console.
- ``samples/drivers/uart/echo_bot`` now works under QEMU ``raspi0`` and on
  real Raspberry Pi Zero W hardware with RX and TX over the BCM2835 AUX
  mini-UART path.
- ``samples/basic/blinky`` now works on real Raspberry Pi Zero W hardware using
  the on-board ACT LED.
- ``samples/basic/button`` now works on real Raspberry Pi Zero W hardware with
  a temporary external button wired from GPIO17 to GND through a sample-specific
  overlay, and the same path has now also been validated with GPIO interrupts
  on real hardware.
- ``samples/basic/sys_heap`` now runs under QEMU ``raspi0`` and the previously
  suspicious large heap free-count was confirmed to be a sample-side reporting
  artifact, not a BCM2835 port bug.
- ``samples/basic/threads`` now works under QEMU ``raspi0`` and on real
  Raspberry Pi Zero W hardware using the board ACT LED as ``led0`` and a
  sample-specific temporary ``led1`` alias on GPIO27.
- ``samples/basic/hash_map`` now works under QEMU ``raspi0`` and on real
  Raspberry Pi Zero W hardware. Its earlier silence was not a missing logging
  feature on the board.
- ``samples/subsys/logging/logger`` now works under QEMU ``raspi0`` and on
  real Raspberry Pi Zero W hardware after fixing the ARM1176 bring-up issues
  that had made logger output look broken.
- ``samples/kernel/msg_queue`` now works on real Raspberry Pi Zero W hardware.
  The observed ``CBA012345`` receive order matches the sample's expected
  urgent-before-normal message queue behavior.
- ``samples/kernel/condition_variables/simple`` now works on real Raspberry Pi
  Zero W hardware. The main thread wakes repeatedly on the condition variable
  until ``done == 20``, matching the sample's expected behavior.
- ``samples/kernel/condition_variables/condvar`` now works on real Raspberry Pi
  Zero W hardware. The waiter wakes at the configured threshold and the sample
  ends with the expected final count of ``145``.
- ``tests/kernel/mem_protect/mem_map`` (``mem_map`` suite, 5 tests) now passes
  on real Raspberry Pi Zero W hardware, including the execute-permission test
  which correctly triggers a PREFETCH ABORT when jumping into an XN-mapped page.
  This validates the full ``arch_mem_map()`` / ``arch_mem_unmap()`` runtime path
  end-to-end on hardware.  QEMU ``raspi0`` does not model the XN bit so the exec
  test fails there; the remaining four tests pass under QEMU.
- The ARM1176 MMU bring-up milestone is now complete for bare-metal kernel-space
  use.  ``CONFIG_USERSPACE`` and per-thread address-space management are the next
  major MMU topic but are deferred as a separate project.
- This is now beyond compile-only and QEMU-only bring-up, but it is still not
  a fully hardware-validated board port yet.
- The current path intentionally prioritizes minimal boot infrastructure over
  completeness. GPIO, Wi-Fi, Bluetooth, and pinctrl completeness are still
  secondary.

What Has Landed
***************

The following pieces now exist in-tree:

- BCM2835 SoC scaffold under ``soc/brcm/bcm2835``.
- ``rpi_zero_w`` board scaffold under ``boards/raspberrypi/rpi_zero_w``.
- BCM2835 devicetree base description in
  ``dts/arm/broadcom/bcm2835.dtsi``.
- BCM2835 devicetree bindings for:

  - ARMCTRL interrupt controller
  - AUX mini-UART
  - GPIO
  - system timer

- BCM2835 GPIO driver in ``drivers/gpio/gpio_bcm2835.c``.
- BCM2835 ARMCTRL interrupt-controller driver in
  ``drivers/interrupt_controller/intc_bcm2835_armctrl.c``.
- BCM2835 system timer driver in
  ``drivers/timer/bcm2835_system_timer.c``.
- BCM2835 mini-UART reuse through the existing Broadcom AUX mini-UART driver.
- ``rpi_zero_w`` ``led0`` wiring for the real ACT LED on GPIO47.
- ``samples/basic/button/boards/rpi_zero_w.overlay`` for a temporary external
  GPIO17 button using the input ``gpio-keys`` path, now also validated in
  interrupt-driven mode on hardware.
- ``samples/basic/threads/boards/rpi_zero_w.overlay`` for a temporary external
  ``led1`` on GPIO27 so the basic threading sample can run without changing the
  base board description.
- Minimal BCM2835 pinctrl support for Pi Zero W mini-UART GPIO14/GPIO15 muxing
  and pull configuration.
- ARM1176JZF-S CPU selection and ``-mcpu=arm1176jzf-s`` toolchain mapping.
- ARM1176-specific compile fixes in the shared ``cortex_a_r`` path for:

  - barriers
  - selected fault handling conditionals
  - reset/vector relocation assumptions

- ARM1176 reset entry cleanup in ``reset.S`` so early boot no longer assumes
  inherited firmware state is already suitable for Zephyr.
- ``cortex_a_r/isr_wrapper.S`` now keeps IRQs masked while dispatching ISRs
  when ``CONFIG_ARM_CUSTOM_INTERRUPT_CONTROLLER`` is in use.
- ARM1176 BCM2835 builds now force ``-mno-unaligned-access`` to match the
  currently working execution model on this temporary shared architecture path.
- ARM1176 BCM2835 builds without MMU support now use
  ``CONFIG_ATOMIC_OPERATIONS_C`` so the current bring-up no longer depends on
  ``LDREX/STREX`` in a no-MMU execution model where exclusive accesses are not
  yet safe.
- ARM1176-specific barrier implementations added to
  ``include/zephyr/arch/arm/barrier.h``:
  ``ISB``/``DSB``/``DMB`` mnemonics are ARMv7-only and are rejected by the
  assembler with ``-mcpu=arm1176jzf-s``.  The ARM1176 equivalents
  (``MCR p15, 0, r0, c7, c5, 4`` / ``c7, c10, 4`` / ``c7, c10, 5``) are now
  provided by overriding the ``z_barrier_*`` functions in the Zephyr-owned
  ARM barrier header so that the third-party CMSIS ``cmsis_gcc.h`` does not
  need to be modified.
- ``cortex_a_r/cpu_idle.c`` updated to use ``barrier_dsync_fence_full()`` and
  ``barrier_isync_fence_full()`` instead of the CMSIS ``__DSB()``/``__ISB()``
  calls, which were the remaining source of ``dsb 0xF``/``isb 0xF`` assembler
  errors on the ARM1176 target.
- ``arch/arm/core/mmu/arm_mmu.c`` ARM1176 cache init sequence improved
  following analysis of Linux ``arch/arm/mm/proc-v6.S __v6_setup``:

  - ``L1C_InvalidateICacheAll()`` removed; it called CMSIS ``__DSB()`` and
    ``__ISB()`` internally, which do not assemble on ARM1176.  Replaced with
    direct ``MCR p15, 0, r0, c7, c5, 0`` (invalidate I-cache).
  - D-cache operation changed from invalidate-only (``c7, c6, 0``) to
    clean+invalidate (``c7, c14, 0``) for robustness on warm resets.
  - Write-buffer drain (``MCR p15, 0, r0, c7, c10, 4``) added after cache
    invalidation, matching the Linux ``proc-v6.S`` sequence.
  - ``barrier_isync_fence_full()`` (ISB) now follows every ``__set_SCTLR()``
    call that changes the MMU-enable bit, as required by the ARM Architecture
    Reference Manual.  This covers the initial MMU enable in ``z_arm_mmu_init``
    and both the disable and re-enable in
    ``arm_mmu_remap_l1_section_to_l2_table``.
  - Duplicate ``ICACHE_ENABLE_BIT``/``DCACHE_ENABLE_BIT`` lines removed from
    both branches of the ``CONFIG_ARMV6_ARM1176`` ``#ifdef``; they now appear
    once after the conditional together with ``MMU_ENABLE_BIT``.

- ``cortex_a_r/cache.c`` ARM1176-specific whole-cache operations: all functions
  that previously called CMSIS ``L1C_InvalidateDCacheAll()``,
  ``L1C_CleanDCacheAll()``, or ``L1C_CleanInvalidateDCacheAll()`` — which loop
  over cache sets/ways by reading the ARMv7-only CLIDR register — are now
  replaced under ``CONFIG_ARMV6_ARM1176`` with direct MCR instructions per the
  ARM1176JZF-S TRM:

  - ``arch_dcache_invd_all()`` → ``MCR p15, 0, r0, c7, c6, 0`` (TRM B2.7.4)
  - ``arch_dcache_flush_all()`` → ``MCR p15, 0, r0, c7, c10, 0`` (TRM B2.7.6)
  - ``arch_dcache_flush_and_invd_all()`` → ``MCR p15, 0, r0, c7, c14, 0``
    (TRM B2.7.7)
  - ``arch_icache_invd_all()`` → ``MCR p15, 0, r0, c7, c5, 0`` (TRM B2.7.5)
  - ``arch_dcache_enable()``, ``arch_dcache_disable()``, and
    ``arch_icache_enable()`` now call the above helpers instead of the CMSIS
    ``L1C_*`` functions.  ``L1C_InvalidateICacheAll()`` was also unsafe because
    it calls ``__DSB()``/``__ISB()`` internally, emitting the ARMv7-only
    mnemonics.

Important Behavioral Changes
****************************

- ``uart_bcm2711.c`` now explicitly enables the AUX block before accessing the
  mini-UART registers. This is required for BCM2835 and is still valid for the
  later Broadcom AUX mini-UART variants.
- BCM2835 GPIO support is now split out from ``gpio_bcm2711.c`` into
  ``gpio_bcm2835.c`` so the BCM2711 GPIO driver can remain BCM2711-specific.
- ARM1176 vector relocation now uses VBAR instead of depending on copying
  vectors to ``0x00000000``.
- ARM1176 reset entry now:

  - forces SVC mode with IRQ/FIQ masked
  - invalidates cache and TLB state inherited from firmware
  - clears MMU, cache, and high-vector related SCTLR state
  - uses explicit synchronization barriers around those control-register
    changes

- ``rpi_zero_w`` now disables ``CONFIG_TICKLESS_KERNEL`` explicitly because the
  BCM2835 timer driver currently implements only a simple periodic tick source.
- The shared ``cortex_a_r`` IRQ wrapper no longer re-enables IRQs before the
  device-level interrupt source has been cleared on BCM2835. Under QEMU this
  had allowed immediate recursive re-entry on level-triggered IRQ sources,
  eventually overflowing the exception stack and corrupting RAM-backed text.
  Keeping IRQs masked for custom interrupt-controller paths fixed that issue
  and allowed both timer IRQ delivery and mini-UART RX interrupts to run
  normally.
- ARM1176 BCM2835 builds now disable compiler-generated unaligned accesses.
  This was required because the temporary shared ARM1176 execution path was
  still faulting on unaligned word loads in real sample code. The first clear
  reproducer was the logging sample's hexdump path, which had made deferred
  logging look broken when the real failure was an alignment abort.

Ground Truth Used
*****************

The current implementation was based on the BCM2835 peripherals manual, the
ARM1176JZF-S TRM, and reference code from Linux and U-Boot under
``references/``.

Key facts confirmed from those sources:

- BCM2835 AUX mini-UART register access is gated by AUX enable control.
- BCM2835 ARMCTRL base is ``0x2000b000`` on the ARM-visible peripheral map.
- BCM2835 system timer is suitable for a first periodic kernel tick source.
- ARM1176 reset starts from low vectors unless high vectors are enabled.
- ARM1176 exception vectors can be relocated with VBAR.
- BCM2835 D-cache is 16 KB, 4-way set-associative, 16-byte cache lines, 256
  sets (confirmed from ``references/linux-rpi/arch/arm/boot/dts/broadcom/bcm2835.dtsi``).
- BCM2835 has no L2 cache available to the CPU; the L2 is dedicated to the GPU
  (same DTS source).  No L2 cache maintenance is needed.
- ARM1176 MMU init sequence derived from
  ``references/linux-rpi/arch/arm/mm/proc-v6.S`` (``__v6_setup`` function),
  extracted from the blobless partial clone using ``git show HEAD:<path>``:

  - cache clean+invalidate before MMU enable: ``c7, c14, 0`` (D), ``c7, c5, 0`` (I)
  - write-buffer drain after cache ops: ``c7, c10, 4``
  - TLB invalidation: ``c8, c7, 0`` (I+D TLBs)
  - TTBR0 flags for UP (uniprocessor): outer WB+WA region (``TTB_RGN_WBWA``)
  - SCTLR ``v6_crval``: XP (bit 23), Z (branch prediction, bit 11),
    U (unaligned, bit 22) all set by Linux in addition to M/C/I
  - ISB required after every SCTLR write that changes translation state
  - ARM1176 barrier encodings (ISB/DSB/DMB) from the same file and from the
    ARM1176JZF-S TRM chapters B2.7.1–B2.7.3

Temporary Scaffolding And Stubs
*******************************

These items are still intentional scaffolding and should not be mistaken for a
finished port:

- ``CONFIG_CPU_ARM1176JZF_S`` currently reuses the generic
  ``CPU_AARCH32_CORTEX_A`` path.
- ``arch/arm/core/cortex_a_r`` is still being reused as the temporary ARM1176
  execution path. This is a bring-up shortcut, not a claim that ARM11 is a
  proper Cortex-A/R target.
- ``soc/brcm/bcm2835/soc.h`` only provides the minimum core-identification
  support needed for compilation.
- ``soc/brcm/bcm2835/pinctrl_soc.h`` is a stub.
- ``soc/brcm/bcm2835/pinctrl_stub.c`` provides a no-op
  ``pinctrl_configure_pins()``.
- The current BCM2835 pinctrl support is intentionally minimal and only covers
  the Pi Zero W mini-UART bring-up path on GPIO14/GPIO15.
- The current BCM2835 GPIO bring-up is still intentionally minimal and only
  covers the banks needed for the ACT LED and the temporary GPIO17 button test.
- ``drivers/interrupt_controller/intc_bcm2835_armctrl.c`` is a first-pass
  driver aimed at minimal bring-up.
- ``drivers/timer/bcm2835_system_timer.c`` is a first-pass periodic timer
  driver. Timeout reprogramming and richer timer behavior are not implemented.
- ``rpi_zero_w`` should still be treated as experimental until timer IRQ
  delivery and broader hardware behavior are validated beyond first console
  output.

What Has Been Verified
**********************

The current verified build command is:

.. code-block:: sh

   env CCACHE_DISABLE=1 west build -b rpi_zero_w zephyr/samples/hello_world \
     -d /tmp/zephyr-rpi-zero-w-build -p always

Result:

- configuration succeeds
- compilation succeeds
- linking succeeds
- ``zephyr/zephyr.elf`` is produced for ``rpi_zero_w/bcm2835``

The current verified QEMU boot command is:

.. code-block:: sh

   qemu-system-arm -M raspi0 -display none -monitor none \
     -serial null -serial stdio \
     -kernel /tmp/zephyr-rpi-zero-w-build/zephyr/zephyr.elf

Observed result under QEMU ``raspi0``:

- reset reaches ``z_arm_reset``
- early arch setup reaches ``z_cstart``
- console init reaches ``uart_console_init``
- ``samples/hello_world`` reaches ``main()``
- QEMU prints the Zephyr banner and ``Hello World! rpi_zero_w/bcm2835``

Observed result on real Raspberry Pi Zero W hardware for
``samples/hello_world``:

- the mini-UART console prints the Zephyr banner and
  ``Hello World! rpi_zero_w/bcm2835``
- this sample has also now been re-validated on real hardware with the new
  minimal ARM1176 MMU support enabled
- this same MMU path has now also been observed working on real hardware after
  enabling I-cache and D-cache simultaneously; the D-cache path required fixing
  ``L1C_InvalidateDCacheAll()`` (see D-cache root cause note below)

The current verified ``echo_bot`` build command is:

.. code-block:: sh

   env CCACHE_DISABLE=1 west build -b rpi_zero_w \
     zephyr/samples/drivers/uart/echo_bot \
     -d /tmp/zephyr-rpi-zero-w-echo-bot-build -p always

The current verified ``echo_bot`` QEMU boot command is:

.. code-block:: sh

   qemu-system-arm -M raspi0 -display none -monitor none \
     -serial null -serial stdio \
     -kernel /tmp/zephyr-rpi-zero-w-echo-bot-build/zephyr/zephyr.elf

Observed result under QEMU ``raspi0`` for ``echo_bot``:

- QEMU prints the Zephyr banner and the ``echo_bot`` prompt
- the BCM2835 system timer IRQ fires and reaches the Zephyr timer path
- AUX mini-UART RX IRQ delivery now reaches both ``uart_isr`` and
  ``serial_cb``
- entering a line such as ``hello`` prints ``Echo: hello``

Observed result on real Raspberry Pi Zero W hardware for ``echo_bot``:

- the serial console prints the Zephyr banner and the ``echo_bot`` prompt
- AUX mini-UART RX and TX both work on the Pi Zero W mini-UART console path
- entering a line such as ``hello`` prints ``Echo: hello``
- this confirms the mini-UART RX interrupt-driven echo path is now validated
  on real hardware, not only under QEMU
- this path has also now been re-validated on real hardware with the new
  minimal ARM1176 MMU support enabled
- this same MMU path has now also been observed working on real hardware after
  enabling I-cache

The current verified GDB-assisted root cause for the earlier failed
``echo_bot`` RX path is:

- the failure was not a QEMU console-slot mismatch once TX output was visible
- the first real problem was recursive IRQ re-entry in the shared
  ``cortex_a_r`` IRQ wrapper when used with the BCM2835 custom interrupt
  controller path
- that recursion eventually overflowed the exception stack and corrupted
  RAM-backed text, leading to undefined-instruction faults inside code that was
  otherwise valid in the ELF image
- masking IRQs during ISR dispatch for
  ``CONFIG_ARM_CUSTOM_INTERRUPT_CONTROLLER`` fixed the issue in QEMU

Important QEMU console detail:

- In QEMU's BCM2835 model, PL011 ``uart0`` is wired to ``serial_hd(0)``
  while AUX mini-UART ``uart1`` is wired to ``serial_hd(1)``.
- Because the current Zephyr board uses ``uart1`` as ``zephyr,console``,
  a simple ``-serial stdio`` test targets the wrong UART.
- For the current board, the working QEMU console setup is
  ``-serial null -serial stdio`` so stdio is attached to QEMU serial slot 1.

The current verified raw-image QEMU boot command is:

.. code-block:: sh

   qemu-system-arm -M raspi0 -display none -monitor none \
     -serial null -serial stdio \
     -bios /tmp/zephyr-rpi-zero-w-build/zephyr/zephyr.bin

Observed result under that raw-image path:

- QEMU also prints the Zephyr banner and ``Hello World! rpi_zero_w/bcm2835``
- ``zephyr.bin`` is therefore confirmed usable as a Pi-style flat kernel image

The current verified real-hardware result is:

- Pi Zero W boots from SD card with the Zephyr raw image
- serial console at 115200 8N1 prints the Zephyr banner
- serial console prints ``Hello World! rpi_zero_w/bcm2835``

The current verified ``blinky`` build command is:

.. code-block:: sh

   env CCACHE_DISABLE=1 west build -b rpi_zero_w \
     zephyr/samples/basic/blinky \
     -d /tmp/zephyr-rpi-zero-w-blinky-build -p always

Observed result on real Raspberry Pi Zero W hardware for ``blinky``:

- the Pi Zero W ACT LED blinks
- console output reports LED state changes

The current verified ``button`` build command is:

.. code-block:: sh

   env CCACHE_DISABLE=1 west build -b rpi_zero_w \
     zephyr/samples/basic/button \
     -d /tmp/zephyr-rpi-zero-w-button-build -p always

Observed result on real Raspberry Pi Zero W hardware for ``button``:

- the console prints ``Press the button``
- the external button test works using GPIO17 with a momentary switch to GND
- press and release events are printed on the console
- the ACT LED follows button state through the sample's optional ``led0`` path
- periodic timer activity is therefore now observed on real hardware during the
  button test
- with ``polling-mode`` removed from the sample overlay, the same setup also
  produces button press and release events through the BCM2835 GPIO interrupt
  path on real hardware
- temporary driver-side ``printk`` tracing during that run showed BCM2835 GPIO
  ISR entry for GPIO17 before the sample callback printed its button events

The current verified ``sys_heap`` build command is:

.. code-block:: sh

   env CCACHE_DISABLE=1 west build -b rpi_zero_w \
     zephyr/samples/basic/sys_heap \
     -d /tmp/zephyr-rpi-zero-w-sys-heap-build -p always

The current verified ``sys_heap`` QEMU boot command is:

.. code-block:: sh

   qemu-system-arm -M raspi0 -display none -monitor none \
     -serial null -serial stdio \
     -kernel /tmp/zephyr-rpi-zero-w-sys-heap-build/zephyr/zephyr.elf

Observed result under QEMU ``raspi0`` for ``sys_heap``:

- QEMU prints the Zephyr banner and the sample banner
- the sample prints expected allocator activity for the test heap instances
- the apparently inconsistent large free-count is acceptable for the libc
  malloc arena on this board because it is backed by the remaining SRAM
- the printed ``heap size 256`` field for every heap is a sample-side constant,
  so that part of the output should not be treated as the true heap size for
  ``z_malloc_heap``

The current verified ``threads`` build command is:

.. code-block:: sh

   env CCACHE_DISABLE=1 west build -b rpi_zero_w \
     zephyr/samples/basic/threads \
     -d /tmp/zephyr-rpi-zero-w-threads-build -p always

The current verified ``threads`` QEMU boot command is:

.. code-block:: sh

   qemu-system-arm -M raspi0 -display none -monitor none \
     -serial null -serial stdio \
     -kernel /tmp/zephyr-rpi-zero-w-threads-build/zephyr/zephyr.elf

Observed result under QEMU ``raspi0`` for ``threads``:

- QEMU prints the Zephyr banner and repeated ``Toggled ledX`` console output
- the faster ``led0`` thread and slower ``led1`` thread both run concurrently
- FIFO handoff plus ``k_malloc()`` and ``k_free()`` activity works during the
  sample's steady-state loop

Observed result on real Raspberry Pi Zero W hardware for ``threads``:

- the ACT LED and an external LED on GPIO27 both toggle as expected
- the console prints repeated ``Toggled ledX`` messages on the mini-UART
- the sample therefore also exercises thread scheduling and FIFO handoff on
  real hardware

The current verified ``hash_map`` build command is:

.. code-block:: sh

   env CCACHE_DISABLE=1 west build -b rpi_zero_w \
     zephyr/samples/basic/hash_map \
     -d /tmp/zephyr-rpi-zero-w-hash-map-build -p always

The current verified ``hash_map`` QEMU boot command is:

.. code-block:: sh

   qemu-system-arm -M raspi0 -display none -monitor none \
     -serial null -serial stdio \
     -kernel /tmp/zephyr-rpi-zero-w-hash-map-build/zephyr/zephyr.elf

Observed result under QEMU ``raspi0`` for ``hash_map``:

- the sample now prints its insert/remove/replace statistics
- the sample ends with ``success``
- the earlier lack of visible output was downstream of the ARM1176
  unaligned-access bug, not a missing console or logging backend feature

Observed result on real Raspberry Pi Zero W hardware for ``hash_map``:

- the sample prints its expected hash map statistics on the mini-UART console
- the sample ends with ``success`` on real hardware as well
- this confirms the default ``hash_map`` path is now validated beyond QEMU

The current verified ``logger`` build command is:

.. code-block:: sh

   env CCACHE_DISABLE=1 west build -b rpi_zero_w \
     zephyr/samples/subsys/logging/logger \
     -d /tmp/zephyr-rpi-zero-w-logger-build -p always

The current verified ``logger`` QEMU boot command is:

.. code-block:: sh

   qemu-system-arm -M raspi0 -display none -monitor none \
     -serial null -serial stdio \
     -kernel /tmp/zephyr-rpi-zero-w-logger-build/zephyr/zephyr.elf

Observed result under QEMU ``raspi0`` for ``logger``:

- the sample now prints its module, instance, hexdump, severity, and external
  logger output as expected
- deferred logging and ``CONFIG_LOG_PRINTK=y`` both work on this board in QEMU
- the earlier apparent logger stall was actually an alignment fault triggered
  by the sample's hexdump path on the temporary ARM1176 execution path

Observed result on real Raspberry Pi Zero W hardware for ``logger``:

- the sample now prints its expected logger output on the mini-UART console
- the sample's module, instance, hexdump, severity, and external logger output
  are all now observed on target
- this confirms the logger sample is now validated on real hardware, not only
  under QEMU

The current verified root cause for the earlier logging failure is:

- the board UART path itself was functioning
- the first strong reproducer was
  ``samples/subsys/logging/logger``, which faults in
  ``sample_instance_call()`` during the hexdump path when unaligned accesses
  are permitted by the compiler on this target
- the apparent deferred-logging failure was therefore a secondary symptom of an
  ARM1176 unaligned-access mismatch, not a separate logging backend defect
- forcing ``-mno-unaligned-access`` for ``CONFIG_ARMV6_ARM1176`` BCM2835 builds
  fixed both the logger reproducer and the default ``hash_map`` output path in
  QEMU
- a later real-hardware fault investigation also showed that ARM1176 no-MMU
  bring-up must not rely on builtin exclusive-access atomics, so ARM1176 now
  selects ``CONFIG_ATOMIC_OPERATIONS_C`` unless MMU support is enabled

The current verified ``msg_queue`` build command is:

.. code-block:: sh

   env CCACHE_DISABLE=1 west build -b rpi_zero_w \
     zephyr/samples/kernel/msg_queue \
     -d ./zephyr/build -p always

Observed result on real Raspberry Pi Zero W hardware for ``msg_queue``:

- the mini-UART console prints the producer trace for normal and urgent items
- the consumer prints ``CBA012345``
- this matches the expected urgent-before-normal ordering and confirms working
  message queue behavior on target
- this sample has also now been re-validated on real hardware with the new
  minimal ARM1176 MMU support enabled
- this same MMU path has now also been observed working on real hardware after
  enabling I-cache

The current verified ``condition_variables/simple`` build command is:

.. code-block:: sh

   env CCACHE_DISABLE=1 west build -b rpi_zero_w \
     zephyr/samples/kernel/condition_variables/simple \
     -d ./zephyr/build -p always

Observed result on real Raspberry Pi Zero W hardware for
``condition_variables/simple``:

- the mini-UART console prints worker progress for threads 0 through 19
- the main thread wakes repeatedly after each condition-variable signal
- the sample ends with ``done == 20 so everyone is done``
- this confirms working condition-variable wait/signal behavior on target
- this sample has also now been re-validated on real hardware with the new
  minimal ARM1176 MMU support enabled

The current verified ``condition_variables/condvar`` build command is:

.. code-block:: sh

   env CCACHE_DISABLE=1 west build -b rpi_zero_w \
     zephyr/samples/kernel/condition_variables/condvar \
     -d ./zephyr/build -p always

Observed result on real Raspberry Pi Zero W hardware for
``condition_variables/condvar``:

- the waiter thread blocks until the threshold signal is sent at count 12
- the waiter resumes, updates the shared count, and unlocks the mutex cleanly
- the sample ends with ``Final value of count = 145. Done.``
- this confirms working condition-variable signal, wake, and mutex interaction
  on target
- this sample has also now been re-validated on real hardware with the new
  minimal ARM1176 MMU support enabled

The current verified ``mem_map`` build command is:

.. code-block:: sh

   west build -b rpi_zero_w zephyr/tests/kernel/mem_protect/mem_map \
     -d ./zephyr/build --pristine

The current verified ``mem_map`` QEMU boot command is:

.. code-block:: sh

   qemu-system-arm -M raspi0 -display none -monitor none \
     -serial null -serial stdio \
     -kernel ./zephyr/build/zephyr/zephyr.elf

Observed result under QEMU ``raspi0`` for ``mem_map`` (``mem_map`` suite):

- ``test_k_mem_map_phys_bare_exec``: **FAIL** — QEMU ``raspi0`` does not model
  the XN (Execute Never) MMU bit; no PREFETCH ABORT is generated when jumping
  into an XN-mapped page, so the test's expected-fault path is never reached
- ``test_k_mem_map_phys_bare_rw``: PASS — DATA ABORT on write to an RO mapping
- ``test_k_mem_map_phys_bare_side_effect``: PASS — no unintended alias side-effects
- ``test_k_mem_map_phys_bare_unmap_reclaim_addr``: PASS — VA region correctly
  reclaimed and reused after ``arch_mem_unmap()``
- ``test_k_mem_unmap_phys_bare``: PASS — DATA ABORT on access to a page after unmap
- Overall: ``TESTSUITE mem_map failed`` (one QEMU-only XN limitation)

Observed result on real Raspberry Pi Zero W hardware for ``mem_map``
(``mem_map`` suite):

- ``test_k_mem_map_phys_bare_exec``: PASS — hardware correctly generates a
  PREFETCH ABORT at ``pc: 0x00805000`` when jumping into a page mapped without
  execute permission, confirming the ARM1176 MMU XN bit is enforced
- ``test_k_mem_map_phys_bare_rw``: PASS
- ``test_k_mem_map_phys_bare_side_effect``: PASS
- ``test_k_mem_map_phys_bare_unmap_reclaim_addr``: PASS — both mapped addresses
  returned as ``0x7fd4d6``, confirming VA reclaim
- ``test_k_mem_unmap_phys_bare``: PASS
- Overall: ``TESTSUITE mem_map succeeded``

This result validates the complete ``arch_mem_map()`` / ``arch_mem_unmap()``
runtime path on real ARM1176 silicon.  The fault type ``Unknown (15)`` in the
permission-fault cases and ``Unknown (7)`` in the translation-fault cases reflect
that the current Zephyr DFSR decoder does not yet name ARMv6 short-descriptor
fault status codes; the underlying MMU behavior is correct.

Note on ``mem_map_api`` suite: the ``test_k_mem_map_exhaustion`` test allocates
all available virtual pages in a loop (~16 000 iterations at 4 KB/page for
the ~63 MB free address space on this board) and runs for several minutes.
``test_k_mem_map_user`` auto-skips because ``CONFIG_USERSPACE`` is not enabled.
The remaining tests (``test_k_mem_map_unmap``, ``test_k_mem_map_guard_before``,
``test_k_mem_map_guard_after``) exercise ``k_mem_map()`` / ``k_mem_unmap()`` and
guard-page fault enforcement but were not observed completing due to the
exhaustion test's runtime.

The current verified root cause for the D-cache failure after MMU enable is:

- enabling D-cache with ``ARM_MMU_SCTLR_DCACHE_ENABLE_BIT`` caused an
  immediate Undefined Instruction exception inside ``z_arm_mmu_init``
- the faulting instruction was ``MRC p15, 1, r0, c0, c0, 1``
  (the CLIDR / Cache Level ID Register read used by the CMSIS
  ``L1C_InvalidateDCacheAll()`` function)
- CLIDR is an ARMv7-only register; it does not exist on ARM1176 (ARMv6);
  executing this coprocessor access on ARM1176 triggers an Undefined Instruction
  exception which propagates through ``z_fatal_error`` to ``arch_system_halt``
- the exception mode at the time of the fault was System mode (``SPSR_und = 0x1DF``)
  with the CPU halted in UND mode at ``arch_system_halt+0xC`` (``b .``)
- the symptom was that ``hello_world`` appeared to halt immediately because
  the GDB session still held echo_bot symbols; ``info address arch_system_halt``
  confirmed the halt location was ``arch_system_halt``, not a corrupted
  ``uart_isr``
- the fix is in ``arch/arm/core/mmu/arm_mmu.c`` under
  ``#ifdef CONFIG_ARMV6_ARM1176``: replace all CMSIS cache helper calls with
  direct MCR instructions valid on ARM1176:

  - ``MCR p15, 0, r0, c7, c5, 0`` — invalidate entire I-cache (TRM B2.7.5)
  - ``MCR p15, 0, r0, c7, c14, 0`` — clean+invalidate entire D-cache (TRM
    B2.7.7); clean+invalidate rather than invalidate-only so that dirty lines
    are written back on warm resets
  - ``MCR p15, 0, r0, c7, c10, 4`` — drain write buffer / DSB (TRM B2.7.2)

- ``L1C_InvalidateICacheAll()`` is also not safe to call on ARM1176 because it
  calls CMSIS ``__DSB()`` and ``__ISB()`` internally; those expand to
  ``dsb 0xF`` / ``isb 0xF`` which are ARMv7-only mnemonics rejected by the
  assembler with ``-mcpu=arm1176jzf-s``
- the broader barrier problem (``dsb``/``isb``/``dmb`` mnemonics not valid on
  ARM1176) was fixed by overriding ``z_barrier_dsync_fence_full()``,
  ``z_barrier_isync_fence_full()``, and ``z_barrier_dmem_fence_full()`` in
  ``include/zephyr/arch/arm/barrier.h`` with inline MCR equivalents under
  ``CONFIG_ARMV6_ARM1176``; the CMSIS ``cmsis_gcc.h`` file is not modified
- ``cortex_a_r/cpu_idle.c`` was also using ``__DSB()``/``__ISB()`` directly and
  was updated to use ``barrier_dsync_fence_full()``/``barrier_isync_fence_full()``

SD Card Boot Guide
******************

The current bring-up recipe for real Pi Zero W hardware is:

1. Build the sample:

   .. code-block:: sh

      env CCACHE_DISABLE=1 west build -b rpi_zero_w zephyr/samples/hello_world \
        -d /tmp/zephyr-rpi-zero-w-build -p always

2. Use the generated raw binary:

   - ``/tmp/zephyr-rpi-zero-w-build/zephyr/zephyr.bin``

3. Copy that binary to the FAT boot partition under a Pi firmware-visible name,
   for example:

   - ``kernel_zephyr.img``

4. Add the following lines to ``config.txt`` on the boot partition:

   .. code-block:: ini

      [all]
      kernel=kernel_zephyr.img
      enable_uart=1

5. Insert the SD card into the Pi Zero W and power the board normally.

Notes:

- The current board uses the AUX mini-UART path, not PL011.
- The current image does not rely on Linux, U-Boot, or an initramfs.
- ``cmdline.txt`` is not used by this Zephyr ``hello_world`` bring-up path.
- Existing Raspberry Pi firmware files such as ``bootcode.bin``, ``start.elf``,
  ``fixup.dat``, and the standard overlays should remain on the boot
  partition.

Serial Console Guide
********************

Use a 3.3 V TTL USB-UART adapter only.

Wire the Pi Zero W header as follows:

- Pi pin 8 / GPIO14 TX -> USB-UART RX
- Pi pin 10 / GPIO15 RX -> USB-UART TX
- Pi GND -> USB-UART GND

Serial settings:

- 115200 baud
- 8 data bits
- no parity
- 1 stop bit
- no hardware flow control

Important:

- Do not connect 5 V UART signals to the Raspberry Pi GPIO header.
- Do not connect the adapter's VCC pin unless you intentionally want to power
  the board from that adapter.
- Normal USB power into the Pi is the safer default.

Expected first successful output:

.. code-block:: text

   *** Booting Zephyr OS build ...
   Hello World! rpi_zero_w/bcm2835

JTAG RAM Load Guide
*******************

The Pi Zero W can also be debugged over JTAG with a SEGGER J-Link. This path
is useful for loading and running Zephyr images directly into RAM without
rewriting the SD card boot image each time.

Important limitations:

- This is a RAM download and debug-run path, not persistent flashing.
- After reset or power-cycle, the JTAG-loaded image is gone.
- For persistent booting, keep copying ``zephyr.bin`` to the SD boot partition
  as ``kernel_zephyr.img``.

Working Pi firmware config for the A4 JTAG pin group:

.. code-block:: ini

   [all]
   kernel=kernel_zephyr.img
   enable_uart=1
   enable_jtag_gpio=1
   gpio=22-27=a4

Working A4 JTAG wiring on the Pi Zero W header:

- ``Pin 1`` or ``Pin 17`` -> J-Link ``VTref`` / ``3.3V sense``
- any Pi ``GND`` -> J-Link ``GND``
- ``Pin 13`` / ``GPIO27`` -> ``TMS``
- ``Pin 22`` / ``GPIO25`` -> ``TCK``
- ``Pin 37`` / ``GPIO26`` -> ``TDI``
- ``Pin 18`` / ``GPIO24`` -> ``TDO``

Optional:

- ``Pin 15`` / ``GPIO22`` -> ``TRST``
- ``Pin 16`` / ``GPIO23`` -> ``RTCK``

The current proven JTAG setup on hardware is:

- SEGGER ``JLinkGDBServer``
- target device ``ARM11``
- interface ``JTAG``
- speed ``1000`` kHz

Example build command for a RAM-loaded debug session:

.. code-block:: sh

   env CCACHE_DISABLE=1 west build -b rpi_zero_w \
     zephyr/samples/basic/blinky \
     -d /tmp/zephyr-rpi-zero-w-jtag-blinky-build -p always

Exact repeatable SEGGER GDB server command:

.. code-block:: sh

   JLinkGDBServer \
     -device ARM11 \
     -if JTAG \
     -speed 1000 \
     -port 2331 \
     -noir \
     -strict \
     -select USB=69650079

Exact repeatable GDB RAM-load command:

.. code-block:: sh

   "/Volumes/T7/zephyr-sdk/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi-gdb" \
     /tmp/zephyr-rpi-zero-w-jtag-blinky-build/zephyr/zephyr.elf \
     --batch \
     -ex "set pagination off" \
     -ex "target remote :2331" \
     -ex "load" \
     -ex 'set $pc=0x8bf0' \
     -ex "tbreak main" \
     -ex "continue" \
     -ex "detach"

Notes on that sequence:

- ``load`` transfers the Zephyr ELF into target RAM over JTAG.
- ``set $pc=0x8bf0`` sets the ARM11 PC to the ELF entry point for the proven
  ``blinky`` build above. For other builds, read the entry point with:

  .. code-block:: sh

     "/Volumes/T7/zephyr-sdk/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi-readelf" \
       -h /tmp/zephyr-rpi-zero-w-jtag-blinky-build/zephyr/zephyr.elf | \
       awk '/Entry point address:/{print $4}'

- ``tbreak main`` confirms the downloaded image actually starts executing.
- ``detach`` releases the debugger and lets the RAM-loaded image continue to
  run.

Observed result with the sequence above:

- the ARM11 core is detected through JTAG
- GDB successfully downloads the ELF into RAM
- GDB reaches ``main()``
- detaching leaves the RAM-loaded image running on the target

What Is Not Verified Yet
************************

- ARMCTRL interrupt handling on hardware beyond the currently validated
  timer, mini-UART RX, and BCM2835 GPIO button-interrupt cases
- Exception entry and return behavior on hardware

Open Risks
**********

- The largest technical risk is still the temporary reuse of the shared
  ``cortex_a_r`` path for ARM1176.
- The current reset/vector work is more correct than before, but it is still
  incremental adaptation rather than a dedicated ARM11 architecture path.
- The current timer and interrupt-controller drivers are suitable for early
  bring-up, but they have not been stress-tested or hardware-validated.
- The recursive IRQ re-entry bug seen in QEMU has been fixed in the shared
  wrapper, but that path still needs real hardware validation on ARM1176.
- The new ``-mno-unaligned-access`` workaround fixes the observed failures, but
  it is still a workaround on top of the temporary shared ``cortex_a_r`` path
  rather than a dedicated ARM11 architecture solution.
- The ARM1176 MMU bring-up milestone is complete for bare-metal kernel-space use.
  ``arch_mem_map()`` / ``arch_mem_unmap()`` are hardware-validated via
  ``tests/kernel/mem_protect/mem_map``, including XN (Execute Never) enforcement.
  The atomic backend remains intentionally conservative
  (``CONFIG_ATOMIC_OPERATIONS_C``) and can be revisited after the
  ``cortex_a_r`` shared-path risk is resolved.
- ``CONFIG_USERSPACE`` is the next major MMU topic: it requires implementing
  ``arch_mem_domain_*``, USR mode entry/exit, and SVC syscall dispatch.  None
  of these exist yet for the ARM1176 path.  Userspace is deferred as a
  separate project.
- The current board no longer depends on firmware UART pin muxing for the
  mini-UART console path, but BCM2835 pinctrl coverage is still far from
  complete.
- QEMU ``raspi0`` is now good enough to validate the current reset, timer,
  interrupt-controller, and mini-UART boot path, but it is still not a
  substitute for real Pi Zero W hardware validation.

Recommended Next Steps
**********************

Work in this order unless new hardware results force a change:

1. Complete ``tests/kernel/mem_protect/mem_map_api`` validation on hardware:

   - ``test_k_mem_map_exhaustion`` runs for several minutes (16 000+ page
     allocations); let it finish or add a board overlay to skip it
   - ``test_k_mem_map_unmap``, ``test_k_mem_map_guard_before``, and
     ``test_k_mem_map_guard_after`` should pass once exhaustion completes;
     they test ``k_mem_map()`` / ``k_mem_unmap()`` and guard-page fault
     enforcement via the higher-level kernel API

2. Continue reducing ARM1176-specific assumptions inside the shared
   ``cortex_a_r`` code, especially reset, exception entry, IRQ entry, and exit
   behavior.  The shared path is still the largest architectural risk.

3. Revisit the atomic backend: now that the MMU path is stable, evaluate
   whether ``CONFIG_ATOMIC_OPERATIONS_C`` can be replaced with real
   ``LDREX``/``STREX`` exclusive accesses under the MMU.

4. Expand real-hardware validation beyond the already working timer tick,
   mini-UART RX echo path, and BCM2835 GPIO interrupt-driven button path.

5. Decide whether to continue with incremental ARM1176 support inside the
   shared ``cortex_a_r`` path or to split out a dedicated ARM11 path under
   ``arch/arm``.  The current scaffolding is a known temporary shortcut.

6. Once core ARM1176 execution and interrupt behavior are less risky,
   follow-up work can expand into:

   - broader BCM2835 pinctrl coverage beyond the mini-UART path
   - broader BCM2835 GPIO coverage beyond the current minimal banks
   - PL011 selection options
   - less minimal timer behavior (tickless, reprogrammable comparator)
   - general board refinement

7. ``CONFIG_USERSPACE`` is a separate, large project.  Prerequisites before
   starting:

   - stable ARM1176 execution path (ideally a dedicated arch path, not
     shared ``cortex_a_r``)
   - implement ``arch_mem_domain_init()`` and related ``arch_mem_domain_*``
     functions for MMU-based per-thread address space management
   - implement USR mode entry/exit and SVC syscall dispatch
   - implement ASID management to avoid full TLB flushes on context switch
