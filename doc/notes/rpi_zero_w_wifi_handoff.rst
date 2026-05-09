Raspberry Pi Zero W Wi-Fi Handoff
#################################

This note summarizes the current Wi-Fi bring-up slice for the Raspberry Pi
Zero W Zephyr port as of 2026-05-09.  The work deliberately started with the
SDIO host interface before attempting a CYW43438 Wi-Fi driver.

Implemented So Far
******************

- Added a first BCM2835 SDHCI/eMMC host driver:
  ``drivers/sdhc/sdhc_bcm2835.c``.
- Added ``CONFIG_SDHC_BCM2835`` in ``drivers/sdhc/Kconfig.bcm2835`` and wired
  it into ``drivers/sdhc/Kconfig`` and ``drivers/sdhc/CMakeLists.txt``.
- Added devicetree binding ``dts/bindings/sdhc/brcm,bcm2835-sdhci.yaml``.
- Added the BCM2835 SDHCI controller node:
  ``mmc@20300000`` / ``&sdhci`` in ``dts/arm/broadcom/bcm2835.dtsi``.
- Wired the Pi Zero W board DTS for onboard WLAN SDIO:

  - GPIO34-GPIO39 muxed to ALT3 for SDIO/eMMC.
  - GPIO43 muxed to ALT0 for ``WIFI_CLK`` / GPCLK2.
  - GPIO41 / ``WL_ON`` exposed as ``reset-gpios``.
  - ``sdhc0`` alias points at ``&sdhci``.

- Expanded the upper BCM2835 GPIO bank description so GPIO41 ``WL_ON`` and
  GPIO47 ACT LED can coexist on one GPIO device/interrupt registration.
- Added minimal CPRMAN setup in the SDHCI driver:

  - EMMC clock enabled from ``plld_per`` with divider 2, giving a 250 MHz
    controller input clock.
  - GPCLK2 enabled from the oscillator with a divider intended for 32.768 kHz
    ``WIFI_CLK`` on GPIO43.

- Added a focused hardware probe sample:
  ``samples/boards/raspberrypi/rpi_zero_w/sdio_probe``.

Verification Performed
**********************

The probe sample builds successfully:

.. code-block:: sh

   env CCACHE_DISABLE=1 west build -b rpi_zero_w \
     zephyr/samples/boards/raspberrypi/rpi_zero_w/sdio_probe \
     -d /tmp/zephyr-rpi-zero-w-sdio-probe -p always

The sample was run on real Raspberry Pi Zero W hardware with the existing
OpenOCD/GDB and UART setup from ``doc/notes/run_sample.py``,
``doc/notes/cmds.gdb``, and ``doc/notes/rpi-zero-jlink.cfg``.

Observed output after the latest run:

.. code-block:: text

   *** Booting Zephyr OS build v4.4.0-1147-g7728c8027aff ***
   Raspberry Pi Zero W SDIO probe
   CMD0 ret=0
   sdhc cmd5 failed: ret=-116 int=0x00000000 present=0x01ff0000 clk=0x0000 ctl=0x00 pwr=0x00 cmctl=0x00000296 cmdiv=0x00002000
   CMD5 ret=-116 resp0=0x00000000
   sdhc cmd8 failed: ret=-116 int=0x00018000 present=0x01ff0001 clk=0x3947 ctl=0x00 pwr=0x0f cmctl=0x00000296 cmdiv=0x00002000
   ...
   sdhc cmd5 failed: ret=-116 int=0x00018000 present=0x01ff0001 clk=0x3947 ctl=0x00 pwr=0x0f cmctl=0x00000296 cmdiv=0x00002000
   sd_init failed: -134

Important Fixes Already Made
****************************

- The first hardware run hit an ARM1176 alignment fault at ``0x2030002f`` in
  the SDHCI byte-register polling path.  The driver was fixed so byte/word
  SDHCI registers are polled through aligned 32-bit MMIO reads.
- CMD0 initially timed out because no-response commands were treated like
  response-producing commands.  The driver now handles ``SD_RSP_TYPE_NONE`` by
  issuing the command and returning after a short delay.
- ``WL_ON`` was initially left in the asserted state because the GPIO is
  active-low.  The driver now deasserts the active-low reset line by writing
  logical ``0`` when powering on and asserts it with logical ``1`` when powering
  off.
- Splitting GPIO28-GPIO45 and GPIO46-GPIO53 into two devices caused duplicate
  IRQ registration for BCM2835 GPIO bank 1.  The board description now exposes
  one GPIO28-GPIO53 device for the upper bank.

Current Blocker
***************

The SDHCI host is now bootable on hardware and CMD0 succeeds, but the onboard
CYW43438 still does not answer SDIO CMD5.  ``sd_init()`` therefore fails with
``-134`` / ``-ENOTSUP`` because Zephyr's SDIO stack sees no usable SDIO OCR
response.

At this point the blocker is below the Wi-Fi driver layer.  Do not start a
CYW43438 network driver until CMD5 succeeds and Zephyr can enumerate at least
function 0 and function 1 through the SDIO stack.

Likely Next Debug Targets
*************************

- Confirm GPIO34-GPIO39 SDIO pinmux and pulls on hardware.  The current
  pinctrl path should program ALT3, but verify GPFSEL/PUD state with GDB if
  CMD5 remains silent.
- Confirm GPIO43 GPCLK2 really outputs the expected ``WIFI_CLK``.  The current
  code programs GPCLK2 from the oscillator; if this is wrong, the CYW43438 may
  remain unresponsive even with ``WL_ON`` deasserted.
- Confirm ``WL_ON`` timing and level with GDB or a scope.  The driver toggles
  the active-low GPIO via Zephyr's GPIO API, but the module may require a
  longer delay after clock stabilization before reset release.
- Compare the SDHCI register sequence against Linux
  ``references/linux-rpi/drivers/mmc/host/bcm2835-mmc.c``.  The current driver
  is a polling PIO subset and may still miss a BCM2835-specific command,
  timeout, or reset quirk.
- Consider probing with the SDHCI controller powered and clocked before manual
  CMD5.  The diagnostic sample currently sends manual CMD0/CMD5 before
  ``sd_init()``, so the first manual CMD5 runs with SDHCI power/clock registers
  still at zero.  The later ``sd_init()`` CMD5 still times out after clock/power
  setup, so this is diagnostic noise rather than the full failure.

Files Touched In This Slice
***************************

- ``boards/raspberrypi/rpi_zero_w/rpi_zero_w.dts``
- ``drivers/sdhc/CMakeLists.txt``
- ``drivers/sdhc/Kconfig``
- ``drivers/sdhc/Kconfig.bcm2835``
- ``drivers/sdhc/sdhc_bcm2835.c``
- ``dts/arm/broadcom/bcm2835.dtsi``
- ``dts/bindings/sdhc/brcm,bcm2835-sdhci.yaml``
- ``samples/boards/raspberrypi/rpi_zero_w/sdio_probe/CMakeLists.txt``
- ``samples/boards/raspberrypi/rpi_zero_w/sdio_probe/prj.conf``
- ``samples/boards/raspberrypi/rpi_zero_w/sdio_probe/sample.yaml``
- ``samples/boards/raspberrypi/rpi_zero_w/sdio_probe/src/main.c``
