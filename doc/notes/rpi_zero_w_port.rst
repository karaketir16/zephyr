Raspberry Pi Zero W Port Notes
##############################

This note is the current handoff summary for the Zephyr Raspberry Pi Zero W
port. The goal so far has been the minimal path toward boot and first useful
UART validation with ``samples/hello_world`` and ``samples/drivers/uart/echo_bot``:
reset, vectors, interrupt controller, timer, and UART.

Current State
*************

- ``rpi_zero_w/bcm2835`` now builds successfully and produces ``zephyr.elf``.
- ``samples/hello_world`` has now been observed on real Raspberry Pi Zero W
  hardware over the mini-UART console.
- ``samples/drivers/uart/echo_bot`` now works under QEMU ``raspi0`` with RX
  and TX over the BCM2835 AUX mini-UART path.
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
  - system timer

- BCM2835 ARMCTRL interrupt-controller driver in
  ``drivers/interrupt_controller/intc_bcm2835_armctrl.c``.
- BCM2835 system timer driver in
  ``drivers/timer/bcm2835_system_timer.c``.
- BCM2835 mini-UART reuse through the existing Broadcom AUX mini-UART driver.
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

Important Behavioral Changes
****************************

- ``uart_bcm2711.c`` now explicitly enables the AUX block before accessing the
  mini-UART registers. This is required for BCM2835 and is still valid for the
  later Broadcom AUX mini-UART variants.
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

What Is Not Verified Yet
************************

- Timer interrupt delivery on hardware
- ARMCTRL interrupt handling on hardware
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

4. After stable tick/IRQ behavior, decide whether to:

   - continue with incremental ARM1176 support inside the shared path, or
   - split out a dedicated ARM11 path under ``arch/arm``

5. With first hardware boot now achieved, follow-up work can expand into:

   - broader BCM2835 pinctrl coverage beyond the mini-UART path
   - GPIO support
   - PL011 selection options
   - less minimal timer behavior
   - general board refinement
