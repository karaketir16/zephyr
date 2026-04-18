/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SOC_BRCM_BCM2835_PINCTRL_SOC_H_
#define ZEPHYR_SOC_BRCM_BCM2835_PINCTRL_SOC_H_

#include <zephyr/types.h>

/*
 * Temporary bring-up scaffold:
 * the initial Pi Zero W path relies on firmware-configured UART pins and does
 * not model BCM2835 pinctrl in Zephyr yet.
 */
typedef uint32_t pinctrl_soc_pin_t;

#define Z_PINCTRL_STATE_PINS_INIT(node_id, prop) {}

#endif /* ZEPHYR_SOC_BRCM_BCM2835_PINCTRL_SOC_H_ */
