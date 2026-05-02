/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/toolchain.h>

.section .text.atomic_strex_probe, "ax", %progbits
.arm

GTEXT(atomic_strex_probe)

/*
 * void atomic_strex_probe(atomic_t *target, atomic_val_t value,
 *                         struct strex_probe_result *out)
 *
 * r0 = target
 * r1 = value
 * r2 = out
 */
atomic_strex_probe:
	ldrex r3, [r0]
	strex ip, r1, [r0]
	ldr r1, [r0]
	str r3, [r2, #0]
	str ip, [r2, #4]
	str r1, [r2, #8]
	bx lr
