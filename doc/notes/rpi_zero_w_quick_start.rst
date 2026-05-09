Raspberry Pi Zero W Quick Start
###############################

This note is the short user-facing guide for the Raspberry Pi Zero W Zephyr
bring-up in this repository.

What Runs On Hardware
*********************

The current branch is beyond first-boot bring-up.  The broad kernel sweep is
summarized in ``doc/notes/results/run-20260509-123546/summary.txt`` and the
logging sweep is summarized in
``doc/notes/results/run-20260509-134149/summary.txt``.

Working topics covered by those hardware runs include:

- boot, reset, vectors, exceptions, and fatal-error recovery
- AUX mini-UART console and RX/TX
- GPIO output on the ACT LED and GPIO input/interrupts for the button sample
- ARMCTRL timer IRQ delivery, sleeps, timeouts, timers, and delayed work
- scheduler behavior, preemption, thread lifecycle, stacks, dynamic threads,
  work queues, pipes, FIFOs, LIFOs, queues, message queues, mailboxes, events,
  condition variables, semaphores, mutexes, and memory slabs/heaps
- common kernel helpers, object tracking/core APIs, device APIs, profiling
  hooks, runtime stats, and cleanup paths
- ARM1176 cache maintenance and runtime MMU mapping
- userspace, syscalls, object validation, memory domains, memory protection,
  futexes, stack protection, stack randomization, and ``k_mem_map()``
- logging core/API coverage, deferred/immediate/blocking logging, custom
  headers, rate limiting, timestamps, output formatting, link ordering,
  frontend paths, stress tests, and network-output formatting

Notes:

- ``samples/basic/button`` uses the sample-specific
  ``samples/basic/button/boards/rpi_zero_w.overlay`` overlay for a temporary
  external button on GPIO17.
- ``samples/basic/threads`` uses the sample-specific
  ``samples/basic/threads/boards/rpi_zero_w.overlay`` overlay for a temporary
  external ``led1`` on GPIO27.
- The full per-test status is intentionally kept in the summary files above
  instead of duplicated here.
- ``tests/arch/common/interrupt`` passes on real hardware.  The BCM2835
  ``trigger_irq()`` support used by the dynamic/nested cases is a software ISR
  table dispatch, similar to the existing RX test hook, not an ARMCTRL
  hardware-pended GPU interrupt.  The timer-backed interrupt lock case uses
  the real hardware timer IRQ path.
- ``tests/kernel/context`` passes on real hardware after the ARM1176 idle path
  was changed to use the CP15 wait-for-interrupt operation instead of the
  ARMv7-style ``WFI`` instruction.

Known Not-Working Or Not-Applicable Cases
*****************************************

- Demand-paging tests are skipped.  The current ``rpi_zero_w`` ARM1176 MMU
  port does not implement Zephyr demand paging/demand mapping.
- SMP, IPI, MP, and most FPU-sharing coverage is skipped because Raspberry Pi
  Zero W is a single-core ARM1176 target and the current validation target does
  not enable those features.
- ``tests/kernel/timer/cycle64`` is skipped because the current BCM2835 timer
  path exposes 32-bit cycle reads.
- ``tests/kernel/timer/starve`` is a long 3600 s starvation soak and is left
  out of the normal sweep.
- ``tests/kernel/fatal/message_capture`` is skipped by the local hardware
  harness because the expected fatal path does not end with the normal project
  success marker.
- In the logging sweep, ``dictionary`` and ``log_disabled`` are skipped by the
  local test list.  ``log_backend_fs`` and ``log_backend_uart`` currently
  build-fail in the hardware sweep and are not counted as working topics yet.

Build A Sample
**************

Example:

.. code-block:: sh

   env CCACHE_DISABLE=1 west build -b rpi_zero_w zephyr/samples/hello_world \
     -d /tmp/zephyr-rpi-zero-w-hello -p always

The SD-card boot image is:

- ``/tmp/zephyr-rpi-zero-w-hello/zephyr/zephyr.bin``

Build A Test
************

Example:

.. code-block:: sh

   env CCACHE_DISABLE=1 west build -b rpi_zero_w \
     zephyr/tests/kernel/mem_protect/mem_map \
     -d /tmp/zephyr-rpi-zero-w-mem-map -p always

The SD-card boot image is:

- ``/tmp/zephyr-rpi-zero-w-mem-map/zephyr/zephyr.bin``

Run On Hardware From SD Card
****************************

1. Build the sample or test.
2. Copy the generated ``zephyr.bin`` to the FAT boot partition on the SD card.
3. Rename it to a firmware-visible filename such as ``kernel_zephyr.img``.
4. Add the following to ``config.txt`` on the boot partition:

   .. code-block:: ini

      [all]
      kernel=kernel_zephyr.img
      enable_uart=1

5. Keep the normal Raspberry Pi firmware files on the boot partition.
6. Insert the SD card and power the board.

Serial console:

- Pi pin 8 / GPIO14 TX -> USB-UART RX
- Pi pin 10 / GPIO15 RX -> USB-UART TX
- Pi GND -> USB-UART GND
- 115200 8N1

For ``samples/hello_world``, the expected output is:

.. code-block:: text

   *** Booting Zephyr OS build ...
   Hello World! rpi_zero_w/bcm2835

Run In QEMU
***********

Build the sample or test as usual, then run:

.. code-block:: sh

   qemu-system-arm -M raspi0 -display none -monitor none \
     -serial null -serial stdio \
     -kernel <build-dir>/zephyr/zephyr.elf

Example:

.. code-block:: sh

   qemu-system-arm -M raspi0 -display none -monitor none \
     -serial null -serial stdio \
     -kernel /tmp/zephyr-rpi-zero-w-hello/zephyr/zephyr.elf

Note:

- QEMU uses the AUX mini-UART console on serial slot 1 here, so keep
  ``-serial null -serial stdio`` in that order.

More Detail
***********

For the detailed bring-up log and implementation notes, see
``doc/notes/rpi_zero_w_port.rst``.
