Atomic Set
##########

This sample is a small ARM exclusive-access repro for ``LDREX``/``STREX``.

It runs a handwritten exclusive store loop twice:

- once with normal IRQ state
- once with IRQs masked around the loop

Each run prints:

- whether the ``strex`` loop ever succeeded
- how many retries were needed
- the old value read by ``ldrex``
- the final value after the attempted store

Example output:

.. code-block:: text

   atomic_set sample
   manual irq-on  success=1 retries=0 old=1 current=42
   manual irq-off success=1 retries=0 old=1 current=42
