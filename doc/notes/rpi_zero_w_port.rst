Raspberry Pi Zero W Port Notes
##############################

This note is the current handoff summary for the Zephyr Raspberry Pi Zero W
port. The goal so far has been the minimal path toward boot and
``samples/hello_world``: reset, vectors, interrupt controller, timer, and UART.

Current State
*************

- ``rpi_zero_w/bcm2835`` now builds successfully and produces ``zephyr.elf``.
- This is still a compile-valid and partially boot-oriented port, not a
  hardware-validated board port yet.
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
- ARM1176JZF-S CPU selection and ``-mcpu=arm1176jzf-s`` toolchain mapping.
- ARM1176-specific compile fixes in the shared ``cortex_a_r`` path for:

  - barriers
  - selected fault handling conditionals
  - reset/vector relocation assumptions

- ARM1176 reset entry cleanup in ``reset.S`` so early boot no longer assumes
  inherited firmware state is already suitable for Zephyr.

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
- The current UART path assumes firmware-configured pins rather than a Zephyr
  BCM2835 pinctrl driver.
- ``drivers/interrupt_controller/intc_bcm2835_armctrl.c`` is a first-pass
  driver aimed at minimal bring-up.
- ``drivers/timer/bcm2835_system_timer.c`` is a first-pass periodic timer
  driver. Timeout reprogramming and richer timer behavior are not implemented.
- ``rpi_zero_w`` should still be treated as experimental until hardware boot is
  demonstrated.

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

What Is Not Verified Yet
************************

- Boot on real Raspberry Pi Zero W hardware
- UART output on hardware
- Timer interrupt delivery on hardware
- ARMCTRL interrupt handling on hardware
- Exception entry and return behavior on hardware

Open Risks
**********

- The largest technical risk is still the temporary reuse of the shared
  ``cortex_a_r`` path for ARM1176.
- The current reset/vector work is more correct than before, but it is still
  incremental adaptation rather than a dedicated ARM11 architecture path.
- The current timer and interrupt-controller drivers are suitable for early
  bring-up, but they have not been stress-tested or hardware-validated.
- The board currently depends on firmware UART pin setup because BCM2835
  pinctrl is not implemented yet.

Recommended Next Steps
**********************

Work in this order unless new hardware results force a change:

1. Continue reducing ARM1176-specific assumptions inside the shared
   ``cortex_a_r`` code, especially reset, exception entry, IRQ entry, and exit
   behavior.
2. Validate that the BCM2835 ARMCTRL hooks match what the ARM1176 interrupt
   path expects once hardware testing begins.
3. Boot-test ``samples/hello_world`` on real Pi Zero W hardware with only the
   minimal chain enabled:

   - reset
   - vectors
   - periodic timer tick
   - mini-UART console

4. After first UART output and stable tick/IRQ behavior, decide whether to:

   - continue with incremental ARM1176 support inside the shared path, or
   - split out a dedicated ARM11 path under ``arch/arm``

5. Only after first boot should follow-up work expand into:

   - BCM2835 pinctrl
   - GPIO support
   - PL011 selection options
   - less minimal timer behavior
   - general board refinement
