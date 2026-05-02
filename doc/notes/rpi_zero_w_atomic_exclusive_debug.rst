Raspberry Pi Zero W Atomic Exclusive Access Debug Report
########################################################

Summary
*******

This note captures the investigation into why ARM1176 ``LDREX``/``STREX``
based atomics were failing on the Raspberry Pi Zero W Zephyr bring-up while
the C atomic backend worked.

ARM1176 ``LDREX``/``STREX`` based atomics do work on Raspberry Pi Zero W.

The required MMU behavior is:

- permanent writable image RAM must be mapped as normal cacheable,
  non-shareable memory on ARM1176
- runtime-created normal RAM mappings must also remain non-shareable on
  ARM1176
- runtime page-table descriptor writes must be cleaned out of D-cache before
  TLB invalidation so the ARM1176 hardware page-table walker observes the new
  entries

With that combination in place, the meaningful builtin atomic probes pass on
real Raspberry Pi Zero W hardware for:

- permanent writable image RAM
- permanent page-aligned writable RAM
- anonymous runtime mappings

Problem Statement
*****************

Observed behavior:

- With ``ATOMIC_OPERATIONS_BUILTIN``, a trivial ``atomic_set()`` test did not
  complete as expected.
- With ``ATOMIC_OPERATIONS_C``, the same test worked.

That established early that the problem was in the exclusive-access path, not
in UART, SD boot, or the sample itself.

Files Touched During Investigation
**********************************

- ``samples/basic/atomic_set`` was expanded into a diagnostic probe.
- ``arch/arm/core/mmu/arm_mmu.c`` was adjusted to compare permanent mappings,
  runtime mappings, and the final production behavior.
- ``soc/brcm/bcm2835/early_console.c`` was added temporarily so very early
  ``printk()`` output could be observed before the regular console hook was
  installed.

Important References Used
*************************

- ``references/raspberrypi-bare-metal/extest/README``
- ``references/raspberrypi-bare-metal/extest/notmain.c``
- ``references/linux-rpi/arch/arm/mm/proc-v6.S``
- ``references/linux-rpi/arch/arm/mm/mmu.c``
- ``references/linux-rpi/arch/arm/mm/cache-v6.S``

Key reference takeaways:

- The bare-metal ``extest`` material shows that exclusive accesses on ARMv6
  depend on MMU/cache behavior and can fail if the access falls through to a
  path that does not preserve the expected exclusive-access response behavior.
- Linux ARMv6 UP memory policy does not force shared normal RAM mappings the
  way SMP paths do.
- Linux ARMv6 cache maintenance code shows the expected kernel-side D-cache
  clean behavior for making memory updates visible to hardware consumers.

Diagnostic Sample
*****************

The sample ``samples/basic/atomic_set`` was turned into a targeted probe.

It tested:

- a permanent low static variable
- a permanent page-aligned backing page
- a runtime alias created with ``k_mem_map_phys_bare()``
- an anonymous page created with ``k_mem_map()``
- the same permanent pages again after ``k_mem_update_flags()``

It uses a one-shot assembly helper in
``samples/basic/atomic_set/src/atomic_strex_probe.S`` to avoid inline assembly
ambiguity.

Original Core Probe Result
**************************

Before any runtime repair of the permanent mapping:

- permanent pages were broken
- runtime-created mappings were correct

Representative observed behavior:

- permanent mappings:
  - ``old`` came back as the target virtual address
  - ``status`` came back as ``1``
  - memory still changed to the new value
- runtime mappings:
  - ``old`` came back as ``1``
  - ``status`` came back as ``0``
  - memory changed to the new value as expected

This was seen consistently across:

- ``low-static``
- ``direct-page``
- ``mapped-alias``
- ``anon-page``

What This Proved
****************

The failure was strongly isolated to the boot-time permanent writable RAM
mapping, not to the ARM1176 exclusive instructions themselves.

Applying ``k_mem_update_flags()`` to the permanent pages corrected them in
place, which showed that:

- the failing part was the initial permanent RAM mapping
- the runtime remap path created ARM1176-compatible attributes
- ``LDREX/STREX`` worked once those permanent pages used the same effective
  policy

In other words:

- not a UART issue
- not an SD boot issue
- not an interrupt timing issue
- not a broken probe
- not "ARM1176 does not support ``strex``"

Final Solution
**************

The final solution in ``arch/arm/core/mmu/arm_mmu.c`` is:

1. Create the static ``zephyr_data`` mapping as normal WB/WA non-shareable
   memory on ``CONFIG_ARMV6_ARM1176``.
2. Keep ARM1176 runtime normal WB/WT mappings non-shareable as well.
3. Before TLB invalidation after runtime map/unmap operations, clean the page
   table updates from D-cache and drain the write buffer.

In practice, the important code areas are:

- ``mmu_zephyr_ranges[]`` / ``zephyr_data``
- ``z_arm1176_sync_page_table_updates()``
- ``arch_mem_map()``
- ``arch_mem_unmap()``

Why This Works
**************

There are two parts to the explanation.

1. Exclusive accesses needed the permanent writable RAM mapping to use the same
   ARM1176-compatible normal-memory policy as the working runtime mappings:

   - normal memory
   - write-back/write-allocate cacheable
   - read/write
   - non-shareable

   With that mapping, the permanent image RAM behaves like the runtime RAM that
   already allowed correct ``LDREX``/``STREX`` behavior.

2. Once page tables live in cached normal RAM, software page-table updates are
   not automatically visible to the ARM1176 page-table walker.

   Runtime descriptor writes therefore need explicit cache maintenance before
   TLB invalidation. Without that step, software can see updated descriptors in
   memory while the hardware walker still faults on newly mapped pages.

Root Cause
**********

The real issue was a two-part ARM1176 MMU interaction:

1. The initial permanent writable RAM mapping attributes were wrong for
   exclusive accesses.

   The permanent ``zephyr_data`` region needed to behave like the working
   ARM1176 runtime normal mapping policy:

   - normal memory
   - write-back/write-allocate cacheable
   - read/write
   - non-shareable

2. Once page tables lived in cached non-shareable RAM, runtime MMU descriptor
   writes required explicit cache maintenance before TLB invalidation so the
   hardware page-table walker could observe the new entries.

Final Working Code Shape
************************

The final working implementation has these pieces in
``arch/arm/core/mmu/arm_mmu.c``:

- ARM1176 static ``zephyr_data`` mappings are created as normal WB/WA
  non-shareable memory from the start
- ARM1176 runtime-created normal WB/WT mappings are not forced shareable
- ARM1176-specific page-table cache maintenance is performed before TLB
  invalidation after runtime map/unmap operations

Representative code areas:

- ``mmu_zephyr_ranges[]`` / ``zephyr_data``
- ``z_arm1176_sync_page_table_updates()``
- ``arch_mem_map()``
- ``arch_mem_unmap()``

Hardware Result
***************

With the final solution applied, the Raspberry Pi Zero W hardware probe showed
successful builtin atomic behavior on the important cases, including:

- ``low-static``
- ``direct-page``
- ``anon-page``

Representative successful result shape:

- ``old=1``
- ``status=0``
- ``current=42``
- ``after_probe=42``

Meaning:

- builtin atomics now work on the important permanent RAM case
- builtin atomics now work on runtime anonymous mappings
- builtin atomics work without a late ARM1176 ``zephyr_data`` remap

About ``mapped-alias``
**********************

The ``mapped-alias`` case used ``k_mem_map_phys_bare()`` to create a second
cacheable virtual alias to the same RAM page that already had a permanent
mapping.

This was useful diagnostically, but it is not required for the port, and it is
not a normal or recommended pattern for system RAM.

Zephyr's own MM APIs already warn against using ``k_mem_map_phys_bare()`` for
RAM page frames unless the caller fully understands the consequences.

For ARM1176 bring-up purposes:

- ``mapped-alias`` should be treated as diagnostic-only
- it is not a success criterion for the port

Notes
*****

The key port success criteria are now:

- permanent writable image RAM supports builtin atomics
- permanent page-aligned writable RAM supports builtin atomics
- anonymous runtime mappings support builtin atomics
- boot remains stable

These criteria are satisfied with the current solution.
