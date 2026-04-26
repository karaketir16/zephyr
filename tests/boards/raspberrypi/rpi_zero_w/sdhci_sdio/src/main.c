/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Exercise SD stack sd_init() on the Pi Zero W SDHCI / SDIO bus without
 * building the Infineon Wi-Fi driver, for SDIO bring-up diagnostics.
 *
 * The on-board CYW43438 is held in reset until WL_REG_ON (GPIO41) is asserted.
 * The Infineon driver does this in airoc_wifi_power_on(); this test must do
 * the same or every CMD will time out (SDHCI int 0x18000).
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sd/sd.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(sdhci_sdio_test, LOG_LEVEL_INF);

static const struct device *sdhc = DEVICE_DT_GET(DT_ALIAS(sdhc0));

/* Match drivers/wifi/infineon/airoc_whd_hal_common.h sequencing */
#define WL_REG_ON_DISCHARGE_MS 10
#define WL_REG_ON_POWERUP_MS    250

static int rpi_zero_w_wl_reg_on(void)
{
#if DT_NODE_EXISTS(DT_NODELABEL(wifi)) && DT_NODE_HAS_PROP(DT_NODELABEL(wifi), wifi_reg_on_gpios)
	const struct gpio_dt_spec wl = GPIO_DT_SPEC_GET(DT_NODELABEL(wifi), wifi_reg_on_gpios);
	int err;

	if (!gpio_is_ready_dt(&wl)) {
		LOG_ERR("wifi-reg-on GPIO controller not ready");
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(&wl, GPIO_OUTPUT);
	if (err != 0) {
		return err;
	}

	err = gpio_pin_set_dt(&wl, 0);
	if (err != 0) {
		return err;
	}
	k_msleep(WL_REG_ON_DISCHARGE_MS);

	err = gpio_pin_set_dt(&wl, 1);
	if (err != 0) {
		return err;
	}
	k_msleep(WL_REG_ON_POWERUP_MS);

	LOG_INF("WL_REG_ON asserted (CYW43438 power sequence done)");
	return 0;
#else
	LOG_WRN("No wifi node with wifi-reg-on-gpios; SDIO may be unpowered");
	return 0;
#endif
}

ZTEST(sdhci_sdio, test_sd_init_sdio_probe)
{
	struct sd_card card;
	int ret;

	zassert_true(device_is_ready(sdhc), "sdhc0 not ready");

	ret = rpi_zero_w_wl_reg_on();
	zassert_equal(ret, 0, "WL_REG_ON sequence failed: %d", ret);

	memset(&card, 0, sizeof(card));
	ret = sd_init(sdhc, &card);
	if (ret != 0) {
		LOG_ERR("sd_init failed: %d (see sd / sdhc / sdio logs above)", ret);
	}
	zassert_equal(ret, 0, "sd_init() failed; check CMD5 R4 and SDHCI traces");
	zassert_equal(card.type, CARD_SDIO, "expected SDIO card on Pi Zero W");
}

ZTEST_SUITE(sdhci_sdio, NULL, NULL, NULL, NULL, NULL);
