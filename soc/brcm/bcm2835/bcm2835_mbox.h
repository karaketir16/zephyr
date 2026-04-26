/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SOC_BRCM_BCM2835_BCM2835_MBOX_H_
#define ZEPHYR_SOC_BRCM_BCM2835_BCM2835_MBOX_H_

#include <stdint.h>

/** Property-channel clock id for the Arasan SDHCI / SDIO root clock (BCM2835). */
#define BCM2835_MBOX_CLOCK_ID_EMMC 1U

/**
 * Power on the SDHCI block via VideoCore and read the EMMC root clock rate.
 * Matches the mailbox sequence used by U-Boot bcm2835_get_mmc_clock(); without
 * this, the Arasan SDHCI often sees no bit clock and all commands time out.
 *
 * @param clock_id BCM2835_MBOX_CLOCK_ID_EMMC (or EMMC2 on BCM2711, not used here)
 * @param rate_hz_out optional; set to the firmware-reported rate on success
 * @return 0 on success, negative errno otherwise
 */
int bcm2835_mbox_mmc_prepare(uint32_t clock_id, uint32_t *rate_hz_out);

#endif /* ZEPHYR_SOC_BRCM_BCM2835_BCM2835_MBOX_H_ */
