Raspberry Pi Zero W Wi-Fi Notes
################################

This note is a focused handoff for the on-board Wi-Fi path on the Raspberry Pi
Zero W.  It is separate from ``rpi_zero_w_port.rst`` so the remaining Wi-Fi
work can be tracked without mixing it into the general ARM1176 and board
bring-up history.

Goal
****

Enable the on-board CYW43438 Wi-Fi device on ``rpi_zero_w/bcm2835`` using the
existing Zephyr Infineon AIROC Wi-Fi stack.

Current Status
**************

- The board-level Wi-Fi wiring has now been described in Zephyr devicetree.
- A first BCM2835 SDHC driver now exists for the Arasan controller used as the
  SDIO host.
- ``sdhci0`` and the ``airoc-wifi`` child node are now enabled in the Pi Zero W
  board DTS so the software path can build end-to-end.
- The on-board CYW43438 is still not hardware-validated.
- The remaining risk has shifted from "missing host controller support" to
  "prove the BCM2835 SDHCI clocking, PIO transfer path, and SDIO interrupt
  handling on real hardware."

Reference Files
***************

The following references were used to map the Pi Zero W hardware and decide
what could be landed safely first.

Linux Raspberry Pi downstream:

- ``references/linux-rpi/arch/arm/boot/dts/broadcom/bcm2835-rpi-zero-w.dts``
  Main Pi Zero W board DTS.  This confirmed:

  - GPIO34..GPIO39 are the Wi-Fi SDIO pins
  - GPIO41 is ``WL_ON``
  - the SD card uses ``sdhost`` on GPIO48..GPIO53
  - Wi-Fi is not wired through ``sdhost``

- ``references/linux-rpi/arch/arm/boot/dts/broadcom/bcm283x-rpi-wifi-bt.dtsi``
  Shared Raspberry Pi Wi-Fi / Bluetooth include.  This confirmed:

  - Wi-Fi uses the Arasan SDHCI path, not the ``sdhost`` controller
  - the SDIO device is represented as ``wifi@1`` under the SDHCI host
  - Linux uses a simple Wi-Fi power-sequencing node tied to GPIO41

- ``references/linux-rpi/arch/arm/boot/dts/broadcom/bcm283x.dtsi``
  SoC-level pinctrl and controller nodes.  This confirmed:

  - ``emmc_gpio34`` is ALT3 on GPIO34..GPIO39 with pull-ups on CMD/DAT lines
  - ``gpclk2_gpio43`` exists for the Bluetooth side clocking path
  - the Arasan controller lives at ``mmc@7e300000`` in VC/bus address space
  - the compatible string is ``brcm,bcm2835-sdhost`` for the SD card host and
    ``brcm,bcm2835-sdhci`` / ``brcm,bcm2835-mmc`` for the SDIO-capable host

- ``references/linux-rpi/arch/arm/boot/dts/broadcom/bcm270x.dtsi``
  Downstream overlay of the Broadcom DTS.  This confirmed:

  - the SDIO-capable controller is exposed as ``sdhci`` / ``mmc`` / ``mmcnr``
  - it is marked non-removable for soldered-down devices
  - it uses the EMMC clock domain and DMA in Linux

U-Boot:

- ``references/u-boot/drivers/mmc/bcm2835_sdhci.c``
  Compact Broadcom Arasan SDHCI implementation.  This is the best small-driver
  reference for a future Zephyr SDHC port.  Important findings:

  - the controller has a back-to-back register write timing quirk
  - U-Boot works around that by spacing non-data register writes
  - the compatible string is ``brcm,bcm2835-sdhci``
  - the controller clock comes from the firmware mailbox path

- ``references/u-boot/drivers/mmc/bcm2835_sdhost.c``
  Helpful mostly as a negative reference.  It explicitly states:

  - the ``sdhci`` controller supports SDIO
  - the ``sdhost`` controller is used for SD cards and does not support the
    Wi-Fi SDIO use case we need here

- ``references/u-boot/arch/arm/mach-bcm283x/msg.c``
- ``references/u-boot/arch/arm/mach-bcm283x/include/mach/msg.h``
  Useful for the firmware mailbox clock path, especially if the Zephyr SDHC
  driver needs a reliable way to obtain or program the controller clock.

Zephyr in-tree references:

- ``zephyr/drivers/wifi/infineon/airoc_wifi.c``
- ``zephyr/drivers/wifi/infineon/airoc_whd_hal_sdio.c``
- ``zephyr/drivers/wifi/infineon/Kconfig.airoc``
  These confirmed that Zephyr already has:

  - an AIROC SDIO client path
  - support for SDIO interrupt delivery through the SDHC API
  - part selection for ``CYW43438``

- ``zephyr/boards/infineon/cy8cproto_062_4343w/cy8cproto_062_4343w.dts``
  Good model for an SDIO-connected AIROC device under a Zephyr SDHC host.

- ``zephyr/drivers/sdhc/*``
  These were checked to see if a compatible Broadcom host driver already
  existed.  It does not.

What Was Found
**************

Hardware mapping:

- The Pi Zero W on-board Wi-Fi device is attached to the Arasan SDHCI/MMC
  controller, not to the BCM2835 ``sdhost`` controller.
- The SDIO pins are GPIO34..GPIO39.
- ``WL_ON`` is GPIO41.
- The normal SD card path is separate and remains on ``sdhost`` via
  GPIO48..GPIO53.

Software implications:

- The existing Zephyr AIROC Wi-Fi driver is potentially reusable.
- The main missing substrate is the BCM2835 SDHC host-controller driver.
- The current board could not simply enable Wi-Fi by DTS alone because the
  ``brcm,bcm2835-sdhci`` host side does not exist in Zephyr.

One useful cleanup surfaced while wiring the board:

- GPIO41 sits in the middle GPIO bank (GPIO28..GPIO45).
- The current BCM2835 GPIO split had assumed every enabled bank must also have
  an interrupt line configured.
- For the Wi-Fi groundwork we only needed GPIO41 as an output, so the BCM2835
  GPIO driver was relaxed to allow a bank without interrupts.

What Was Landed
***************

The following groundwork has now been added to the Zephyr tree.

Devicetree and bindings:

- ``zephyr/dts/arm/broadcom/bcm2835.dtsi``

  - added ``gpio1`` for GPIO28..GPIO45
  - added disabled ``sdhci0`` node at CPU-visible address ``0x20300000``
  - used ``compatible = "brcm,bcm2835-sdhci"``

- ``zephyr/dts/bindings/sdhc/brcm,bcm2835-sdhci.yaml``

  - added a minimal binding for the BCM2835 SDHCI host

- ``zephyr/boards/raspberrypi/rpi_zero_w/rpi_zero_w.dts``

  - added SDIO pinctrl for GPIO34..GPIO39 using ALT3
  - enabled ``gpio1`` so GPIO41 can be driven
  - added a disabled ``airoc-wifi`` child node under ``sdhci0``
  - wired ``wifi-reg-on-gpios`` to GPIO41

GPIO support:

- ``zephyr/dts/bindings/gpio/brcm,bcm2835-gpio.yaml``

  - made ``interrupts`` optional instead of mandatory

- ``zephyr/drivers/gpio/gpio_bcm2835.c``

  - made the bank IRQ setup optional at init time
  - kept existing interrupt behavior unchanged for banks that do define IRQs

Documentation:

- ``zephyr/doc/notes/rpi_zero_w_port.rst``

  - updated with a short mention that Wi-Fi devicetree groundwork now exists
  - documented that the remaining blocker is the missing BCM2835 SDHC driver

What Still Needs To Be Done
**************************

1. Decide clock-source handling for the BCM2835 SDHC driver.

   Open question:

   - keep using the controller capability register / firmware-inherited setup
     for early bring-up
   - or add a mailbox-backed clock query/programming path modeled after U-Boot

   The mailbox route is more correct.  The current first pass assumes the
   firmware leaves the Arasan block in a usable clock domain and uses the
   advertised SDHCI base clock for divider programming.

2. Review DMA versus PIO for first bring-up.

   Recommendation:

   - keep the first version PIO-only while proving probe and early transfers
   - add DMA later if needed

   Reason:

   - the MMU/cache environment is now much better than before, but Wi-Fi will
     stress data movement harder than the current peripherals
   - starting with a simpler transfer path lowers the number of unknowns

3. Revisit the Wi-Fi node now that the host driver exists.

   Items to check:

   - whether leaving ``airoc-wifi`` enabled by default on this board is the
     right policy once hardware testing starts
   - whether ``wifi-host-wake-gpios`` can be wired cleanly on Pi Zero W
   - whether a simple ``wifi_pwrseq``-style node should be added
   - whether the AIROC SDIO path expects additional properties for this board
   - whether the Pi Zero W should default to ``CYW43438`` in board defconfig

4. Add board configuration defaults once probe is real.

   Possible future updates:

   - board ``Kconfig.defconfig`` defaults for ``CYW43438``
   - Wi-Fi-specific heap / stack tuning if needed

5. Build and run Wi-Fi samples after host bring-up.

   First candidates:

   - ``samples/net/wifi/shell``
   - a very small connect/status sample

   Best initial goal:

   - prove SDIO enumeration and firmware load
   - then prove scan
   - then prove association

Suggested Implementation Order
******************************

The next pass should probably follow this order:

1. Build a tiny SDIO-focused configuration to validate the host in isolation if
   possible.
2. Select ``CYW43438`` and verify that the AIROC driver probes.
3. Verify that SDIO interrupts are delivered reliably on real hardware.
4. Only after probe works, start worrying about Wi-Fi shell, scanning, and
   network behavior.

Verification Done So Far
************************

The Wi-Fi groundwork itself was verified only as a non-regression step:

- ``west build -b rpi_zero_w zephyr/samples/hello_world``
- QEMU ``raspi0`` boot of ``hello_world``

This verified that:

- the new DTS and binding additions parse cleanly
- the optional-IRQ GPIO change did not break current board boot

No Wi-Fi runtime validation exists yet on real hardware; the SDHCI host
driver is a first-pass PIO implementation pending hardware bring-up.

Open Questions
**************

- What is the cleanest CPU-visible clock/control story for the BCM2835 SDHCI
  path inside Zephyr: fixed clock first or mailbox-managed clock from day one?
- Does the Pi Zero W Wi-Fi path require a host-wake GPIO in practice for basic
  operation with the AIROC SDIO stack, or can first bring-up proceed without it?
- Should the long-term board model follow the downstream Linux ``wifi_pwrseq``
  layout more closely, or keep the current simpler direct GPIO description?

Bottom Line
***********

The board wiring, devicetree, and a first-pass BCM2835 SDHCI host driver are in
place so Wi-Fi builds can link end-to-end.  The on-board CYW43438 should still be
treated as not hardware-validated until SDIO and firmware load are proven on a
real Pi Zero W.
