Raspberry Pi Zero W Quick Start
###############################

This note is the short user-facing guide for the Raspberry Pi Zero W Zephyr
bring-up in this repository.

What Runs On Hardware
*********************

The following have been run on real Raspberry Pi Zero W hardware:

- ``samples/hello_world``
- ``samples/drivers/uart/echo_bot``
- ``samples/basic/blinky``
- ``samples/basic/button``
- ``samples/basic/threads``
- ``samples/basic/hash_map``
- ``samples/subsys/logging/logger``
- ``samples/kernel/msg_queue``
- ``samples/kernel/condition_variables/simple``
- ``samples/kernel/condition_variables/condvar``
- ``tests/kernel/mem_protect/mem_map``
- ``tests/kernel/timer/timer_monotonic``
- ``tests/kernel/sleep``
- ``tests/kernel/timer/timer_api``
- ``tests/kernel/timer/timer_behavior``
- ``tests/kernel/workq/work_queue``
- ``tests/kernel/sched/preempt``
- ``tests/kernel/pipe/pipe_api``
- ``tests/kernel/fatal/exception``
- ``tests/kernel/common``
- ``tests/kernel/threads/thread_apis``
- ``tests/kernel/mutex/mutex_api``
- ``tests/arch/common/interrupt``

Notes:

- ``samples/basic/button`` uses the sample-specific
  ``samples/basic/button/boards/rpi_zero_w.overlay`` overlay for a temporary
  external button on GPIO17.
- ``samples/basic/threads`` uses the sample-specific
  ``samples/basic/threads/boards/rpi_zero_w.overlay`` overlay for a temporary
  external ``led1`` on GPIO27.
- ``tests/kernel/mem_protect/mem_map`` is the current MMU validation
  test that have been run on hardware.
- The timer/scheduler validation set covers monotonic cycle reads, sleeps,
  timer APIs, timer jitter/drift behavior, delayed work, preemption, and pipe
  concurrency on real hardware.
- The broader kernel validation set now also covers fatal exceptions, common
  kernel helpers, thread lifecycle APIs, and mutex priority-inheritance paths
  on real hardware.
- ``tests/arch/common/interrupt`` passes on real hardware.  The BCM2835
  ``trigger_irq()`` support used by the dynamic/nested cases is a software ISR
  table dispatch, similar to the existing RX test hook, not an ARMCTRL
  hardware-pended GPU interrupt.  The timer-backed interrupt lock case uses
  the real hardware timer IRQ path.

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
