/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/arch/arm/mmu/arm_mmu.h>
#include <zephyr/sys/util.h>

static const struct arm_mmu_region mmu_regions[] = {
	/*
	 * BCM2835 peripherals are visible to the ARM at 0x20000000-0x20ffffff.
	 * Keep the whole peripheral window as device memory for this first MMU
	 * bring-up instead of enumerating individual IP blocks.
	 */
	MMU_REGION_FLAT_ENTRY("bcm2835-peripherals", 0x20000000, 0x01000000,
			      MT_DEVICE | MPERM_R | MPERM_W),
};

const struct arm_mmu_config mmu_config = {
	.num_regions = ARRAY_SIZE(mmu_regions),
	.mmu_regions = mmu_regions,
};
