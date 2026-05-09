Raspberry Pi Zero W Port Notes
##############################

This note is the current handoff summary for the Zephyr Raspberry Pi Zero W
port.  The work has moved past first console output into broad real-hardware
validation across reset, vectors, interrupt controller, timer, UART, GPIO,
logging, cache, MMU, and userspace paths.

The current summary files are the source of truth for full per-test status:

- ``doc/notes/results/run-20260509-123546/summary.txt``: broad kernel sweep.
- ``doc/notes/results/run-20260509-134149/summary.txt``: logging sweep.

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
- The broad kernel sweep now passes across timer/scheduler behavior, thread
  lifecycle, synchronization primitives, memory allocation, work queues, pipes,
  events, fatal exceptions, context/idle behavior, common helpers, cache, MMU,
  userspace, syscalls, memory domains, object validation, and memory
  protection.  See ``doc/notes/results/run-20260509-123546/summary.txt`` for
  the per-test list.
- The logging sweep now passes across core/API behavior, deferred and
  immediate logging, blocking mode, backend init, custom headers, frontend
  paths, link ordering, message/output formatting, rate limiting, stress,
  timestamp, and tracing-related coverage.  See
  ``doc/notes/results/run-20260509-134149/summary.txt`` for the per-test list
  and exceptions.
- The ARM1176 MMU bring-up milestone now includes kernel mappings,
  ``k_mem_map()``, userspace entry, syscalls, memory domains, user stack
  isolation, and object validation.
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
- ``cortex_a_r/cpu_idle.c`` now also uses the ARM1176 CP15
  wait-for-interrupt operation (``MCR p15, 0, r0, c7, c0, 4``) instead of the
  generic ``__WFI()`` path.  This fixed ``tests/kernel/context``
  ``test_cpu_idle`` on QEMU ``raspi0`` and real Raspberry Pi Zero W hardware.
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
- ARM1176 MMU attribute and runtime update policy for builtin atomics:

  - the static ``zephyr_data`` mapping is created as normal cacheable
    non-shareable RAM on ``CONFIG_ARMV6_ARM1176``
  - runtime-created normal WB/WT RAM mappings are also kept non-shareable on
    ARM1176
  - runtime page-table descriptor writes are cleaned out before TLB
    invalidation in ``z_arm1176_sync_page_table_updates()``
  - this combination is what made ARM1176 exclusive accesses work correctly on
    permanent writable RAM and runtime mappings on real hardware

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
- ``rpi_zero_w`` should still be treated as experimental, but timer IRQ
  delivery, sleeps, timers, delayed work, preemption, and several kernel
  concurrency paths now have real-hardware validation beyond first console
  output.

What Has Been Verified
**********************

Use the following commands as the default verification pattern:

.. code-block:: sh

   env CCACHE_DISABLE=1 west build -b rpi_zero_w <app-or-test> \
     -d <build-dir> -p always

.. code-block:: sh

   qemu-system-arm -M raspi0 -display none -monitor none \
     -serial null -serial stdio \
     -kernel <build-dir>/zephyr/zephyr.elf

Notes:

- The QEMU console setup must use ``-serial null -serial stdio`` because the
  board uses AUX mini-UART ``uart1`` as ``zephyr,console`` and QEMU wires it to
  serial slot 1, not slot 0.
- ``zephyr.bin`` is also confirmed usable as a Pi-style flat kernel image for
  SD-card boot on real hardware.
- The matching raw-image QEMU check is:

  .. code-block:: sh

     qemu-system-arm -M raspi0 -display none -monitor none \
       -serial null -serial stdio \
       -bios <build-dir>/zephyr/zephyr.bin

Representative working topics:

- Boot and console: ``hello_world`` reaches ``main()`` on QEMU and hardware,
  and ``uart/echo_bot`` validates mini-UART RX/TX on both paths.
- GPIO: ``blinky`` works with the ACT LED, and ``button`` works with the
  GPIO17 sample overlay in polling and interrupt-driven modes.
- Kernel timing and scheduling: sleeps, timeouts, timer APIs, the long
  timer-behavior jitter/drift run, delayed work, preemption, context switching,
  idle, and busy wait all pass on hardware.
- Kernel primitives: threads, dynamic threads, thread stacks, FIFOs, LIFOs,
  queues, message queues, mailboxes, pipes, events, condition variables,
  semaphores, mutexes, memory slabs/heaps, object core/tracking, cleanup, and
  common helper APIs all pass in the broad kernel sweep.
- Faults and interrupts: fatal exception handling passes, and
  ``tests/arch/common/interrupt`` validates ISR table connection,
  enable-state handling, offload behavior, nested ISR control flow, and the
  real hardware timer interrupt path under ``irq_lock()`` / unlock.  Its
  BCM2835 ``trigger_irq()`` hook is still a software ISR table dispatch for
  test control flow, not hardware-pended ARMCTRL GPU IRQ injection.
- MMU, cache, and userspace: cache tests, ``k_mem_map()``, memory protection,
  memory domains, futexes, syscalls, object validation, stack protection,
  stack randomization, user stacks, and userspace access-fault behavior pass on
  hardware.
- Logging: the logging sweep validates the core/API paths, deferred and
  immediate modes, blocking logging, backend init, custom headers, frontend
  paths, link ordering, message/output formatting, network-output formatting,
  rate limiting, stress, timestamp, and system-tracing related coverage.
- Samples: ``sys_heap``, ``threads``, ``hash_map``, ``logger``,
  ``msg_queue``, and the condition-variable samples have all been used during
  bring-up.  ``samples/basic/atomic_set`` remains a development-only ARM1176
  exclusive-access probe, not a normal validation sample.

See the summary files listed at the top of this note for the full per-test
status.  These results validate the working boot path, console path,
timer/interrupt path, sleeps, timer APIs, delayed work, preemption, pipe
concurrency, core threading/synchronization primitives, mutex priority
inheritance, fatal and exception handling, context/idle behavior, common
interrupt-test control flow, logging behavior, userspace, and the ARM1176
runtime MMU mapping path on real hardware.
ARM1176 short-descriptor DFSR/IFSR decoding has now also been taught the
relevant second-level translation and permission fault codes, so MMU test
failures now report named ARM faults instead of raw
``Unknown (...)`` status numbers.

Known Test Exceptions
*********************

- Demand-paging tests are skipped because the current ARM1176 MMU port does
  not implement Zephyr demand paging/demand mapping.
- SMP, IPI, MP, and most FPU-sharing tests are skipped because Raspberry Pi
  Zero W is a single-core ARM1176 board and those features are outside the
  current validation target.
- ``tests/kernel/timer/cycle64`` is skipped because the BCM2835 timer path
  currently exposes 32-bit cycle reads.
- ``tests/kernel/timer/starve`` is a 3600 s starvation soak and is kept out of
  the normal hardware sweep.
- ``tests/kernel/fatal/message_capture`` is skipped by the local harness
  because its expected fatal path does not end with the normal project success
  marker.
- In the logging sweep, ``dictionary`` and ``log_disabled`` are intentionally
  skipped by ``doc/notes/tests-logging.txt``.  ``log_backend_fs`` and
  ``log_backend_uart`` currently build-fail in the hardware sweep and are not
  claimed as working coverage yet.

Key Debugging Notes
*******************

The main bring-up problems and their fixes were:

- Earlier ``echo_bot`` RX failures were caused by recursive IRQ re-entry in the
  shared ``cortex_a_r`` IRQ wrapper under
  ``CONFIG_ARM_CUSTOM_INTERRUPT_CONTROLLER``. Masking IRQs during ISR dispatch
  fixed the problem.
- Earlier ``logger`` and ``hash_map`` failures were caused by compiler-generated
  unaligned accesses on the temporary ARM1176 execution path. Forcing
  ``-mno-unaligned-access`` for BCM2835 ARM1176 builds fixed both.
- Earlier D-cache enable failures after MMU enable were caused by ARMv7-only
  CMSIS cache helpers. ``L1C_InvalidateDCacheAll()`` reads the ARMv7 CLIDR
  register, which does not exist on ARM1176, and the CMSIS helpers also rely
  on ARMv7-only ``dsb``/``isb`` mnemonics. The ARM1176 path was fixed by using
  direct MCR-based cache and barrier operations.
- Earlier builtin atomic failures on ARM1176 were caused by the permanent
  writable image RAM mapping and runtime page-table visibility rules, not by
  broken ``LDREX``/``STREX`` instructions. The working fix was to map
  permanent and runtime normal RAM as non-shareable on ARM1176 and to clean
  runtime page-table updates before TLB invalidation.
- The earlier ``mem_map_api`` corruption was caused by page-frame accounting,
  not by ``memset()`` or by runtime PTE programming. The boot-mapped vector
  page at ``0x8000`` sat outside ``z_mapped_start..z_mapped_end``, so the
  generic boot-image pinning logic missed it and left the frame on the
  anonymous free-page list. ``k_mem_map()`` could then reuse physical
  ``0x8000`` for an anonymous page, and zeroing that page clobbered
  ``_vector_table``. The fix in ``kernel/mmu.c`` was to add a narrow
  ARM1176-specific vector-page accounting step in addition to the normal
  kernel image range. It only touches pages that actually belong to the
  generic SRAM page-frame database, so other targets are unaffected. If
  another target later needs the same treatment for some other boot-mapped RAM
  range, that logic can be generalized at that time.

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

Representative successful output:

.. code-block:: text

   [early-console] raw mini-UART ready
   Initializing kernel...
   [uart-console] hook installed on uart@20215040
   *** Booting Zephyr OS build ...
   Hello World! rpi_zero_w/bcm2835

Notes:

- The extra early-console and console-hook lines above reflect the current
  debug-enabled branch state and may be removed during cleanup.
- The important success signal is that Zephyr reaches the normal boot banner
  and then prints ``Hello World! rpi_zero_w/bcm2835``.

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

- ARMCTRL interrupt handling on hardware beyond the currently validated timer,
  mini-UART RX, and BCM2835 GPIO button-interrupt cases
- Hardware-pended software triggering for arbitrary BCM2835 ARMCTRL GPU IRQ
  lines.  ``tests/arch/common/interrupt`` now passes, but its BCM2835
  ``trigger_irq()`` helper is an SW ISR table dispatch used for test control
  flow, not an ARMCTRL pending-bit mechanism.

Open Risks
**********

- The largest technical risk is still the temporary reuse of the shared
  ``cortex_a_r`` path for ARM1176.
- The current reset/vector work is more correct than before, but it is still
  incremental adaptation rather than a dedicated ARM11 architecture path.
- The current timer driver is still a simple periodic tick source, but it now
  has meaningful real-hardware validation across monotonic cycle reads,
  sleeps, timer APIs, delayed work, preemption, pipe concurrency, and a
  roughly 200-second jitter/drift run.
- The recursive IRQ re-entry bug seen in QEMU has been fixed in the shared
  wrapper.  Real-hardware validation now covers timer IRQ delivery, mini-UART
  RX interrupts, and BCM2835 GPIO button interrupts, but broader ARMCTRL source
  coverage is still needed.
- The new ``-mno-unaligned-access`` workaround fixes the observed failures, but
  it is still a workaround on top of the temporary shared ``cortex_a_r`` path
  rather than a dedicated ARM11 architecture solution.
- The ARM1176 MMU bring-up milestone now covers kernel mappings,
  ``arch_mem_map()`` / ``arch_mem_unmap()``, XN enforcement, memory domains,
  user stacks, SVC syscall dispatch, userspace access checks, and builtin
  ARM1176 atomics.  Remaining risk is in the temporary shared ``cortex_a_r``
  execution path and in the lack of ASID-based address-space switching, not in
  the basic MMU/userspace policy proven by the current tests.
- The current board no longer depends on firmware UART pin muxing for the
  mini-UART console path, but BCM2835 pinctrl coverage is still far from
  complete.
- QEMU ``raspi0`` is now good enough to validate the current reset, timer,
  interrupt-controller, and mini-UART boot path, but it is still not a
  substitute for real Pi Zero W hardware validation.

Recommended Next Steps
**********************

Work in this order unless new hardware results force a change:

1. Continue reducing ARM1176-specific assumptions inside the shared
   ``cortex_a_r`` code, especially reset, exception entry, IRQ entry, and exit
   behavior.  The shared path is still the largest architectural risk.

2. Expand real-hardware interrupt validation beyond the already working timer
   tick, mini-UART RX echo path, and BCM2835 GPIO interrupt-driven button path.
   The timer/scheduler side now has good real-hardware coverage; the remaining
   gap is broader interrupt-source coverage beyond the synthetic
   ``tests/arch/common/interrupt`` trigger path.

3. Decide whether to continue with incremental ARM1176 support inside the
   shared ``cortex_a_r`` path or to split out a dedicated ARM11 path under
   ``arch/arm``.  The current scaffolding is a known temporary shortcut.

4. Once core ARM1176 execution and interrupt behavior are less risky,
   follow-up work can expand into:

   - broader BCM2835 pinctrl coverage beyond the mini-UART path
   - broader BCM2835 GPIO coverage beyond the current minimal banks
   - PL011 selection options
   - less minimal timer behavior (tickless, reprogrammable comparator)
   - general board refinement

5. Continue hardening the userspace/MMU work that now passes the kernel sweep:

   - review the full SVC and exception return paths for ARM1176-specific
     assumptions inherited from ``cortex_a_r``
   - decide whether ASID support is worth adding now or should wait for a
     dedicated ARM11 architecture path
   - keep growing userspace regression coverage when memory-domain behavior or
     syscall entry/exit changes
