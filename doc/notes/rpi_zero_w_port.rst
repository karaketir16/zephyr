Raspberry Pi Zero W Port Notes
##############################

This note is the current handoff summary for the Zephyr Raspberry Pi Zero W
port. The goal so far has been the minimal path toward boot and first useful
validation across reset, vectors, interrupt controller, timer, UART, and now
initial GPIO and logging:
``samples/hello_world``, ``samples/drivers/uart/echo_bot``,
``samples/basic/blinky``, ``samples/basic/button``,
``samples/basic/sys_heap``, ``samples/basic/threads``,
``samples/basic/hash_map``, and ``samples/subsys/logging/logger``.

Current State
*************

- ``rpi_zero_w/bcm2835`` now builds successfully and produces ``zephyr.elf``.
- ``samples/hello_world`` has now been observed on real Raspberry Pi Zero W
  hardware over the mini-UART console.
- ``samples/drivers/uart/echo_bot`` now works under QEMU ``raspi0`` with RX
  and TX over the BCM2835 AUX mini-UART path.
- ``samples/basic/blinky`` now works on real Raspberry Pi Zero W hardware using
  the on-board ACT LED.
- ``samples/basic/button`` now works on real Raspberry Pi Zero W hardware with
  a temporary external button wired from GPIO17 to GND through a sample-specific
  overlay.
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
- This is now beyond compile-only and QEMU-only bring-up, but it is still not
  a fully hardware-validated board port yet.
- The current path intentionally prioritizes minimal boot infrastructure over
  completeness. GPIO, Wi-Fi, Bluetooth, pinctrl completeness, and MMU support
  are all secondary.

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
  GPIO17 button using the input ``gpio-keys`` path in polling mode.
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
- ``samples/basic/button`` currently uses polling mode for the external button,
  so it does not validate BCM2835 GPIO interrupt delivery yet.
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
  polling-mode button test

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
     -singlerun \
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

- BCM2835 GPIO interrupt delivery on hardware
- ARMCTRL interrupt handling on hardware beyond the minimal timer/UART/GPIO
  polling path
- Exception entry and return behavior on hardware
- ``echo_bot`` RX/TX behavior on real Raspberry Pi Zero W hardware

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
- ARM1176 still has no MMU support in this bring-up, so the current atomic
  backend choice is intentionally conservative and should be revisited once
  minimal ARM11 MMU support exists.
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
   behavior.
2. Validate the now-working timer IRQ and mini-UART RX interrupt path on real
   Raspberry Pi Zero W hardware, not only under QEMU.
3. After first hardware console success, keep testing the minimal chain:

   - reset
   - vectors
   - periodic timer tick
   - mini-UART console
   - mini-UART RX echo path

4. Add minimal ARM1176 MMU support so RAM can be marked as Normal memory and
   the board can eventually move back to builtin atomic operations safely.
5. After stable tick/IRQ behavior, decide whether to:

   - continue with incremental ARM1176 support inside the shared path, or
   - split out a dedicated ARM11 path under ``arch/arm``

6. With first hardware boot now achieved, follow-up work can expand into:

   - broader BCM2835 pinctrl coverage beyond the mini-UART path
   - broader BCM2835 GPIO coverage beyond the current minimal banks
   - GPIO interrupt validation
   - PL011 selection options
   - less minimal timer behavior
   - general board refinement
