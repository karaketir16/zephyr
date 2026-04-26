/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT brcm_bcm2835_sdhci

#include <bcm2835_mbox.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/sdhc.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sd/sd_spec.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/sys_io.h>

LOG_MODULE_REGISTER(sdhc_bcm2835, CONFIG_SDHC_LOG_LEVEL);

#define BCM2835_SDHCI_MIN_FREQ_HZ    400000U
/* Linux sdhci-iproc waits four SD clocks after 32-bit MMIO when clock <= 400 kHz (Arasan CDC). */
#define BCM2835_SDHCI_SLOW_IO_MAX_HZ 400000U
#define BCM2835_SDHCI_IO_DELAY_TICKS 4U
#define BCM2835_SDHC_POLL_US         100U
#define BCM2835_SDHCI_IRQ_PRIO       0U

#define SDHCI_BLOCK_SIZE          0x04
#define SDHCI_BLOCK_COUNT         0x06
#define SDHCI_ARGUMENT            0x08
#define SDHCI_TRANSFER_MODE       0x0c
#define SDHCI_COMMAND             0x0e
#define SDHCI_RESPONSE            0x10
#define SDHCI_BUFFER              0x20
#define SDHCI_PRESENT_STATE       0x24
#define SDHCI_HOST_CONTROL        0x28
#define SDHCI_POWER_CONTROL       0x29
#define SDHCI_HOST_CONTROL2       0x3e
#define SDHCI_CLOCK_CONTROL       0x2c
#define SDHCI_TIMEOUT_CONTROL     0x2e
#define SDHCI_SOFTWARE_RESET      0x2f
#define SDHCI_INT_STATUS          0x30
#define SDHCI_INT_ENABLE          0x34
#define SDHCI_SIGNAL_ENABLE       0x38
#define SDHCI_CAPABILITIES        0x40
#define SDHCI_MAX_CURRENT         0x48
#define SDHCI_HOST_VERSION        0xfe

#define SDHCI_TRNS_BLK_CNT_EN     BIT(1)
#define SDHCI_TRNS_READ           BIT(4)
#define SDHCI_TRNS_MULTI          BIT(5)

#define SDHCI_CMD_RESP_NONE       0x00
#define SDHCI_CMD_RESP_LONG       0x01
#define SDHCI_CMD_RESP_SHORT      0x02
#define SDHCI_CMD_RESP_SHORT_BUSY 0x03
#define SDHCI_CMD_CRC             BIT(3)
#define SDHCI_CMD_INDEX           BIT(4)
#define SDHCI_CMD_DATA            BIT(5)

#define SDHCI_PRESENT_CMD_INHIBIT BIT(0)
#define SDHCI_PRESENT_DATA_INHIBIT BIT(1)
#define SDHCI_PRESENT_DAT_ACTIVE  BIT(2)
#define SDHCI_PRESENT_SPACE_AVAIL BIT(10)
#define SDHCI_PRESENT_DATA_AVAIL  BIT(11)
#define SDHCI_PRESENT_CARD_PRESENT BIT(16)
#define SDHCI_PRESENT_DATA0_LEVEL BIT(20)

#define SDHCI_CTRL_4BITBUS        BIT(1)
#define SDHCI_CTRL_HISPD          BIT(2)
/* Linux SDHCI_CTRL_PRESET_VAL_ENABLE — broken on brcm,bcm2835-sdhci (QUIRK2_PRESET_VALUE_BROKEN). */
#define SDHCI_CTRL_PRESET_VAL_ENABLE BIT(15)

#define SDHCI_POWER_ON            BIT(0)
#define SDHCI_POWER_180           0x0a
#define SDHCI_POWER_300           0x0c
#define SDHCI_POWER_330           0x0e

#define SDHCI_CLOCK_INT_EN        BIT(0)
#define SDHCI_CLOCK_INT_STABLE    BIT(1)
#define SDHCI_CLOCK_CARD_EN       BIT(2)
#define SDHCI_DIVIDER_SHIFT       8
#define SDHCI_DIVIDER_HI_SHIFT    6
#define SDHCI_DIV_MASK            0xff
#define SDHCI_DIV_HI_MASK         0x300

#define SDHCI_RESET_ALL           BIT(0)
#define SDHCI_RESET_CMD           BIT(1)
#define SDHCI_RESET_DATA          BIT(2)

#define SDHCI_INT_RESPONSE        BIT(0)
#define SDHCI_INT_DATA_END        BIT(1)
#define SDHCI_INT_SPACE_AVAIL     BIT(4)
#define SDHCI_INT_DATA_AVAIL      BIT(5)
#define SDHCI_INT_CARD_INT        BIT(8)
#define SDHCI_INT_ERROR           BIT(15)
#define SDHCI_INT_TIMEOUT         BIT(16)
#define SDHCI_INT_CRC             BIT(17)
#define SDHCI_INT_END_BIT         BIT(18)
#define SDHCI_INT_INDEX           BIT(19)
#define SDHCI_INT_DATA_TIMEOUT    BIT(20)
#define SDHCI_INT_DATA_CRC        BIT(21)
#define SDHCI_INT_DATA_END_BIT    BIT(22)
#define SDHCI_INT_ACMD12ERR       BIT(24)
#define SDHCI_INT_ADMA_ERROR      BIT(25)
#define SDHCI_INT_ALL_MASK        0xffffffffU

#define SDHCI_INT_CMD_MASK (SDHCI_INT_RESPONSE | SDHCI_INT_TIMEOUT | SDHCI_INT_CRC | \
			    SDHCI_INT_END_BIT | SDHCI_INT_INDEX)
#define SDHCI_INT_DATA_MASK (SDHCI_INT_DATA_END | SDHCI_INT_SPACE_AVAIL |               \
			     SDHCI_INT_DATA_AVAIL | SDHCI_INT_DATA_TIMEOUT |             \
			     SDHCI_INT_DATA_CRC | SDHCI_INT_DATA_END_BIT |               \
			     SDHCI_INT_ADMA_ERROR)

#define SDHCI_TIMEOUT_MAX         0x0e

#define SDHCI_CAP_TIMEOUT_FREQ_MASK  GENMASK(5, 0)
#define SDHCI_CAP_TIMEOUT_FREQ_MHZ   BIT(7)
#define SDHCI_CAP_BASE_FREQ_MASK     GENMASK(15, 8)
#define SDHCI_CAP_BASE_FREQ_SHIFT    8
#define SDHCI_CAP_MAX_BLK_MASK       GENMASK(17, 16)
#define SDHCI_CAP_CAN_DO_HISPD       BIT(21)
#define SDHCI_CAP_CAN_VDD_330        BIT(24)
#define SDHCI_CAP_CAN_VDD_300        BIT(25)
#define SDHCI_CAP_CAN_VDD_180        BIT(26)

#define SDHCI_MAX_CURRENT_330_MASK   GENMASK(7, 0)
#define SDHCI_MAX_CURRENT_300_MASK   GENMASK(15, 8)
#define SDHCI_MAX_CURRENT_180_MASK   GENMASK(23, 16)

#define SDHCI_SPEC_VER_MASK          0x00ff
#define SDHCI_SPEC_200               1U
#define SDHCI_SPEC_300               2U

/* Linux drivers/mmc/host/sdhci-iproc.c REG_OFFSET_IN_BITS */
#define BCM2835_SDHCI_REG_WORD_SHIFT(reg) (((uint32_t)(reg) << 3) & 0x18U)

struct bcm2835_sdhci_config {
	DEVICE_MMIO_ROM;
	const struct pinctrl_dev_config *pcfg;
	void (*irq_config_func)(const struct device *dev);
	uint32_t min_freq;
	uint32_t max_freq;
	uint32_t power_delay_ms;
	bool non_removable;
};

struct bcm2835_sdhci_data {
	DEVICE_MMIO_RAM;
	struct k_mutex lock;
	struct sdhc_io host_io;
	struct sdhc_host_props props;
	sdhc_interrupt_cb_t sdio_cb;
	void *sdio_cb_user_data;
	/* Linux sdhci_iproc_host: Arasan 32-bit-only MMIO + shadowed block/TM until COMMAND */
	uint32_t shadow_cmd;
	uint32_t shadow_blk;
	bool is_cmd_shadowed;
	bool is_blk_shadowed;
	uint32_t base_clock_hz;
	uint32_t write_delay_us;
};

static void bcm2835_sdhci_iproc_shadow_clear(struct bcm2835_sdhci_data *d)
{
	d->shadow_cmd = 0U;
	d->shadow_blk = 0U;
	d->is_cmd_shadowed = false;
	d->is_blk_shadowed = false;
}

static void bcm2835_sdhci_mmio_post_write_delay(const struct bcm2835_sdhci_data *data)
{
	uint32_t clk = data->host_io.clock;
	uint32_t us;

	if (clk != 0U && clk <= BCM2835_SDHCI_SLOW_IO_MAX_HZ) {
		us = DIV_ROUND_UP(BCM2835_SDHCI_IO_DELAY_TICKS * USEC_PER_SEC, clk);
	} else if (clk == 0U && data->write_delay_us != 0U) {
		us = data->write_delay_us;
	} else {
		return;
	}

	if (us < 1U) {
		us = 1U;
	}

	k_busy_wait(us);
}

static uint32_t bcm2835_sdhci_reg_read32(const struct device *dev, uint32_t reg)
{
	struct bcm2835_sdhci_data *data = dev->data;

	if (reg == SDHCI_BLOCK_SIZE && data->is_blk_shadowed) {
		return data->shadow_blk;
	}
	if (reg == SDHCI_TRANSFER_MODE && data->is_cmd_shadowed) {
		return data->shadow_cmd;
	}

	return sys_read32(DEVICE_MMIO_GET(dev) + reg);
}

static void bcm2835_sdhci_reg_write32(const struct device *dev, uint32_t reg, uint32_t val)
{
	struct bcm2835_sdhci_data *data = dev->data;

	if (reg == SDHCI_BLOCK_SIZE) {
		data->shadow_blk = val;
		data->is_blk_shadowed = false;
	}

	sys_write32(val, DEVICE_MMIO_GET(dev) + reg);

	if (reg != SDHCI_BUFFER) {
		bcm2835_sdhci_mmio_post_write_delay(data);
	}
}

static uint16_t bcm2835_sdhci_reg_read16(const struct device *dev, uint32_t reg)
{
	struct bcm2835_sdhci_data *data = dev->data;
	uint32_t val;
	uint32_t shift = BCM2835_SDHCI_REG_WORD_SHIFT(reg);

	if ((reg == SDHCI_TRANSFER_MODE) && data->is_cmd_shadowed) {
		val = data->shadow_cmd;
	} else if ((reg == SDHCI_BLOCK_SIZE || reg == SDHCI_BLOCK_COUNT) && data->is_blk_shadowed) {
		val = data->shadow_blk;
	} else {
		val = bcm2835_sdhci_reg_read32(dev, reg & ~0x3U);
	}

	return (uint16_t)((val >> shift) & 0xffffU);
}

static uint8_t bcm2835_sdhci_reg_read8(const struct device *dev, uint32_t reg)
{
	uint32_t val = bcm2835_sdhci_reg_read32(dev, reg & ~0x3U);

	return (val >> ((reg & 0x3U) * 8U)) & 0xffU;
}

static void bcm2835_sdhci_reg_write16(const struct device *dev, uint32_t reg, uint16_t val)
{
	struct bcm2835_sdhci_data *data = dev->data;
	uint32_t word_shift = BCM2835_SDHCI_REG_WORD_SHIFT(reg);
	uint32_t mask = 0xffffU << word_shift;
	uint32_t oldval;
	uint32_t newval;

	if (reg == SDHCI_COMMAND) {
		if (data->is_blk_shadowed) {
			bcm2835_sdhci_reg_write32(dev, SDHCI_BLOCK_SIZE, data->shadow_blk);
		}
		oldval = data->shadow_cmd;
		data->is_cmd_shadowed = false;
	} else if ((reg == SDHCI_BLOCK_SIZE || reg == SDHCI_BLOCK_COUNT) && data->is_blk_shadowed) {
		oldval = data->shadow_blk;
	} else {
		oldval = bcm2835_sdhci_reg_read32(dev, reg & ~0x3U);
	}

	newval = (oldval & ~mask) | ((uint32_t)val << word_shift);

	if (reg == SDHCI_TRANSFER_MODE) {
		data->shadow_cmd = newval;
		data->is_cmd_shadowed = true;
		return;
	}

	if (reg == SDHCI_BLOCK_SIZE || reg == SDHCI_BLOCK_COUNT) {
		data->shadow_blk = newval;
		data->is_blk_shadowed = true;
		return;
	}

	bcm2835_sdhci_reg_write32(dev, reg & ~0x3U, newval);
}

static void bcm2835_sdhci_reg_write8(const struct device *dev, uint32_t reg, uint8_t val)
{
	uint32_t oldval = bcm2835_sdhci_reg_read32(dev, reg & ~0x3U);
	uint32_t shift = (reg & 0x3U) * 8U;
	uint32_t mask = 0xffU << shift;
	uint32_t newval = (oldval & ~mask) | ((uint32_t)val << shift);

	bcm2835_sdhci_reg_write32(dev, reg & ~0x3U, newval);
}

static int bcm2835_sdhci_wait_reset(const struct device *dev, uint8_t mask)
{
	int timeout_us = 100000;

	while ((bcm2835_sdhci_reg_read8(dev, SDHCI_SOFTWARE_RESET) & mask) != 0U) {
		if (timeout_us <= 0) {
			return -ETIMEDOUT;
		}

		k_busy_wait(10);
		timeout_us -= 10;
	}

	return 0;
}

static int bcm2835_sdhci_reset_locked(const struct device *dev, uint8_t mask)
{
	struct bcm2835_sdhci_data *data = dev->data;
	int ret;

	bcm2835_sdhci_reg_write8(dev, SDHCI_SOFTWARE_RESET, mask);

	ret = bcm2835_sdhci_wait_reset(dev, mask);
	if (ret == 0) {
		bcm2835_sdhci_iproc_shadow_clear(data);
	}

	return ret;
}

static int bcm2835_sdhci_wait_present_state(const struct device *dev, uint32_t mask,
					    int timeout_ms)
{
	int timeout_us;

	if (timeout_ms == SDHC_TIMEOUT_FOREVER) {
		timeout_us = INT_MAX;
	} else {
		timeout_us = MAX(timeout_ms, 1) * USEC_PER_MSEC;
	}

	while ((bcm2835_sdhci_reg_read32(dev, SDHCI_PRESENT_STATE) & mask) != 0U) {
		if (timeout_us != INT_MAX) {
			if (timeout_us <= 0) {
				return -ETIMEDOUT;
			}

			timeout_us -= BCM2835_SDHC_POLL_US;
		}

		k_busy_wait(BCM2835_SDHC_POLL_US);
	}

	return 0;
}

static int bcm2835_sdhci_wait_int_status(const struct device *dev, uint32_t mask, int timeout_ms,
					 uint32_t *status)
{
	int timeout_us;

	if (timeout_ms == SDHC_TIMEOUT_FOREVER) {
		timeout_us = INT_MAX;
	} else {
		timeout_us = MAX(timeout_ms, 1) * USEC_PER_MSEC;
	}

	for (;;) {
		uint32_t stat = bcm2835_sdhci_reg_read32(dev, SDHCI_INT_STATUS);

		if ((stat & (mask | SDHCI_INT_ERROR)) != 0U) {
			*status = stat;
			return 0;
		}

		if (timeout_us != INT_MAX) {
			if (timeout_us <= 0) {
				*status = stat;
				return -ETIMEDOUT;
			}

			timeout_us -= BCM2835_SDHC_POLL_US;
		}

		k_busy_wait(BCM2835_SDHC_POLL_US);
	}
}

static void bcm2835_sdhci_read_response(const struct device *dev, struct sdhc_command *cmd)
{
	if ((cmd->response_type & SDHC_NATIVE_RESPONSE_MASK) == SD_RSP_TYPE_R2) {
		uint32_t r0 = bcm2835_sdhci_reg_read32(dev, SDHCI_RESPONSE + 0x0);
		uint32_t r1 = bcm2835_sdhci_reg_read32(dev, SDHCI_RESPONSE + 0x4);
		uint32_t r2 = bcm2835_sdhci_reg_read32(dev, SDHCI_RESPONSE + 0x8);
		uint32_t r3 = bcm2835_sdhci_reg_read32(dev, SDHCI_RESPONSE + 0xc);

		cmd->response[0] = r0 << 8;
		cmd->response[1] = (r1 << 8) | (r0 >> 24);
		cmd->response[2] = (r2 << 8) | (r1 >> 24);
		cmd->response[3] = (r3 << 8) | (r2 >> 24);
		return;
	}

	cmd->response[0] = bcm2835_sdhci_reg_read32(dev, SDHCI_RESPONSE);
	cmd->response[1] = 0U;
	cmd->response[2] = 0U;
	cmd->response[3] = 0U;
}

static int bcm2835_sdhci_set_clock_locked(const struct device *dev, uint32_t target_hz)
{
	struct bcm2835_sdhci_data *data = dev->data;
	uint16_t clk = 0U;
	uint32_t div;
	uint16_t spec;
	int timeout_ms = 20;
	int ret;

	ret = bcm2835_sdhci_wait_present_state(dev,
					       SDHCI_PRESENT_CMD_INHIBIT | SDHCI_PRESENT_DATA_INHIBIT,
					       timeout_ms);
	if (ret != 0) {
		return ret;
	}

	bcm2835_sdhci_reg_write16(dev, SDHCI_CLOCK_CONTROL, 0U);
	if (target_hz == 0U) {
		data->host_io.clock = 0U;
		return 0;
	}

	if (data->base_clock_hz == 0U) {
		return -ENOTSUP;
	}

	target_hz = CLAMP(target_hz, data->props.f_min, data->props.f_max);
	spec = bcm2835_sdhci_reg_read16(dev, SDHCI_HOST_VERSION) & SDHCI_SPEC_VER_MASK;

	if (spec >= SDHCI_SPEC_300) {
		if (data->base_clock_hz <= target_hz) {
			div = 1U;
		} else {
			for (div = 2U; div < 2046U; div += 2U) {
				if ((data->base_clock_hz / div) <= target_hz) {
					break;
				}
			}
		}

		div >>= 1U;
	} else {
		for (div = 1U; div < 256U; div <<= 1U) {
			if ((data->base_clock_hz / div) <= target_hz) {
				break;
			}
		}

		div >>= 1U;
	}

	clk |= ((div & SDHCI_DIV_MASK) << SDHCI_DIVIDER_SHIFT);
	clk |= ((div & SDHCI_DIV_HI_MASK) >> 8U) << SDHCI_DIVIDER_HI_SHIFT;
	clk |= SDHCI_CLOCK_INT_EN;
	bcm2835_sdhci_reg_write16(dev, SDHCI_CLOCK_CONTROL, clk);

	while ((bcm2835_sdhci_reg_read16(dev, SDHCI_CLOCK_CONTROL) & SDHCI_CLOCK_INT_STABLE) == 0U) {
		if (timeout_ms-- <= 0) {
			return -ETIMEDOUT;
		}

		k_msleep(1);
	}

	clk |= SDHCI_CLOCK_CARD_EN;
	bcm2835_sdhci_reg_write16(dev, SDHCI_CLOCK_CONTROL, clk);
	data->host_io.clock = target_hz;

	return 0;
}

static int bcm2835_sdhci_transfer_block_read(const struct device *dev, uint8_t *buf, size_t len)
{
	size_t i = 0U;

	while (i < len) {
		uint32_t val = bcm2835_sdhci_reg_read32(dev, SDHCI_BUFFER);

		for (size_t j = 0; j < sizeof(val) && i < len; j++, i++) {
			buf[i] = (uint8_t)(val >> (j * 8U));
		}
	}

	return 0;
}

static int bcm2835_sdhci_transfer_block_write(const struct device *dev, const uint8_t *buf, size_t len)
{
	size_t i = 0U;

	while (i < len) {
		uint32_t val = 0U;

		for (size_t j = 0; j < sizeof(val) && i < len; j++, i++) {
			val |= ((uint32_t)buf[i]) << (j * 8U);
		}

		bcm2835_sdhci_reg_write32(dev, SDHCI_BUFFER, val);
	}

	return 0;
}

static bool bcm2835_sdhci_cmd_reads_data(const struct sdhc_command *cmd)
{
	switch (cmd->opcode) {
	case SD_READ_SINGLE_BLOCK:
	case SD_READ_MULTIPLE_BLOCK:
	case MMC_SEND_EXT_CSD:
	case MMC_CHECK_BUS_TEST:
	case SD_APP_SEND_SCR:
	case SD_APP_SEND_NUM_WRITTEN_BLK:
	case SD_SWITCH:
		return true;
	case SDIO_RW_EXTENDED:
		return (cmd->arg & BIT(SDIO_CMD_ARG_RW_SHIFT)) == 0U;
	default:
		return false;
	}
}

static int bcm2835_sdhci_transfer_data_locked(const struct device *dev, struct sdhc_command *cmd,
					      struct sdhc_data *data)
{
	uint8_t *buf = data->data;
	uint32_t ready_bit = bcm2835_sdhci_cmd_reads_data(cmd) ?
		SDHCI_INT_DATA_AVAIL : SDHCI_INT_SPACE_AVAIL;
	bool is_write = (ready_bit == SDHCI_INT_SPACE_AVAIL);
	size_t remaining = (size_t)data->block_size * data->blocks;
	size_t chunk = data->block_size;
	uint32_t status = 0U;
	int ret;

	data->bytes_xfered = 0U;

	while (remaining != 0U) {
		ret = bcm2835_sdhci_wait_int_status(dev,
						    ready_bit | SDHCI_INT_DATA_END,
						    data->timeout_ms, &status);
		if (ret != 0 && ret != -ETIMEDOUT) {
			return ret;
		}

		if ((status & SDHCI_INT_ERROR) != 0U) {
			break;
		}

		if ((status & ready_bit) == 0U) {
			return -ETIMEDOUT;
		}

		if (chunk > remaining) {
			chunk = remaining;
		}

		if (is_write) {
			ret = bcm2835_sdhci_transfer_block_write(dev, buf, chunk);
		} else {
			ret = bcm2835_sdhci_transfer_block_read(dev, buf, chunk);
		}

		if (ret != 0) {
			return ret;
		}

		bcm2835_sdhci_reg_write32(dev, SDHCI_INT_STATUS, ready_bit);

		buf += chunk;
		remaining -= chunk;
		data->bytes_xfered += chunk;
	}

	ret = bcm2835_sdhci_wait_int_status(dev, SDHCI_INT_DATA_END, data->timeout_ms, &status);
	if (ret != 0 && ret != -ETIMEDOUT) {
		return ret;
	}

	if ((status & SDHCI_INT_ERROR) != 0U) {
		return -EIO;
	}

	if ((status & SDHCI_INT_DATA_END) == 0U) {
		return -ETIMEDOUT;
	}

	bcm2835_sdhci_reg_write32(dev, SDHCI_INT_STATUS, SDHCI_INT_DATA_END);

	return 0;
}

static int bcm2835_sdhci_decode_error(uint32_t status)
{
	if ((status & (SDHCI_INT_TIMEOUT | SDHCI_INT_DATA_TIMEOUT)) != 0U) {
		return -ETIMEDOUT;
	}

	return -EIO;
}

static int bcm2835_sdhci_request_once_locked(const struct device *dev, struct sdhc_command *cmd,
					     struct sdhc_data *xfer)
{
	struct bcm2835_sdhci_data *host = dev->data;
	uint32_t inhibit_mask = SDHCI_PRESENT_CMD_INHIBIT;
	uint32_t wait_mask = SDHCI_INT_RESPONSE;
	uint16_t cmd_flags = 0U;
	uint16_t trns_mode = 0U;
	uint32_t status = 0U;
	uint32_t native_rsp = cmd->response_type & SDHC_NATIVE_RESPONSE_MASK;
	int ret;

	if (IS_ENABLED(CONFIG_SDHC_BCM2835_VERBOSE)) {
		LOG_INF("bcm2835 SDHCI op=%u arg=0x%08x native_rsp=%u xfer=%s", cmd->opcode,
			cmd->arg, (unsigned int)native_rsp, (xfer != NULL) ? "yes" : "no");
	}

	if (xfer != NULL || native_rsp == SD_RSP_TYPE_R1b || native_rsp == SD_RSP_TYPE_R5b) {
		inhibit_mask |= SDHCI_PRESENT_DATA_INHIBIT;
	}

	ret = bcm2835_sdhci_wait_present_state(dev, inhibit_mask, cmd->timeout_ms);
	if (ret != 0) {
		if (IS_ENABLED(CONFIG_SDHC_BCM2835_VERBOSE)) {
			LOG_INF("  inhibit timeout (mask=0x%x): %d", inhibit_mask, ret);
		}
		return ret;
	}

	bcm2835_sdhci_reg_write32(dev, SDHCI_INT_STATUS, SDHCI_INT_ALL_MASK);

	switch (native_rsp) {
	case SD_RSP_TYPE_NONE:
		cmd_flags = SDHCI_CMD_RESP_NONE;
		break;
	case SD_RSP_TYPE_R2:
		cmd_flags = SDHCI_CMD_RESP_LONG | SDHCI_CMD_CRC;
		break;
	case SD_RSP_TYPE_R3:
		cmd_flags = SDHCI_CMD_RESP_SHORT;
		break;
	case SD_RSP_TYPE_R4:
		/*
		 * Arasan/BCM2835: R4 + CRC check often fails SDIO CMD5 even when the
		 * card returns a valid R4; use unchecked short response like Linux R3.
		 */
		cmd_flags = SDHCI_CMD_RESP_SHORT;
		break;
	case SD_RSP_TYPE_R7:
		/* R7: CRC yes; CMD index check has been problematic on some Arasan IP. */
		cmd_flags = SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC;
		break;
	case SD_RSP_TYPE_R1b:
	case SD_RSP_TYPE_R5b:
		cmd_flags = SDHCI_CMD_RESP_SHORT_BUSY | SDHCI_CMD_CRC | SDHCI_CMD_INDEX;
		wait_mask |= SDHCI_INT_DATA_END;
		break;
	case SD_RSP_TYPE_R1:
	case SD_RSP_TYPE_R5:
	case SD_RSP_TYPE_R6:
	default:
		cmd_flags = SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX;
		break;
	}

	if (xfer != NULL) {
		cmd_flags |= SDHCI_CMD_DATA;
		bcm2835_sdhci_reg_write8(dev, SDHCI_TIMEOUT_CONTROL, SDHCI_TIMEOUT_MAX);

		if (xfer->blocks > 1U) {
			trns_mode |= SDHCI_TRNS_MULTI | SDHCI_TRNS_BLK_CNT_EN;
		}

		if ((cmd->arg & BIT(SDIO_CMD_ARG_RW_SHIFT)) == 0U) {
			trns_mode |= SDHCI_TRNS_READ;
		}

		bcm2835_sdhci_reg_write16(dev, SDHCI_BLOCK_SIZE, xfer->block_size);
		bcm2835_sdhci_reg_write16(dev, SDHCI_BLOCK_COUNT, xfer->blocks);
		bcm2835_sdhci_reg_write16(dev, SDHCI_TRANSFER_MODE, trns_mode);
	} else {
		/*
		 * Linux sdhci-iproc: clear software shadows then zero the block register
		 * word on the hardware before a no-data command.
		 */
		bcm2835_sdhci_iproc_shadow_clear(host);
		bcm2835_sdhci_reg_write32(dev, SDHCI_BLOCK_SIZE, 0U);
		/*
		 * Arasan on BCM2835 ties command/data timeouts to SDCLK
		 * (Linux SDHCI_QUIRK_DATA_TIMEOUT_USES_SDCLK). Program the
		 * slowest counter for no-data commands (CMD8, CMD5, etc.).
		 */
		bcm2835_sdhci_reg_write8(dev, SDHCI_TIMEOUT_CONTROL, SDHCI_TIMEOUT_MAX);
	}

	bcm2835_sdhci_reg_write32(dev, SDHCI_ARGUMENT, cmd->arg);
	bcm2835_sdhci_reg_write16(dev, SDHCI_COMMAND, ((cmd->opcode & 0xffU) << 8U) | cmd_flags);

	ret = bcm2835_sdhci_wait_int_status(dev, wait_mask, cmd->timeout_ms, &status);
	if (ret != 0 && ret != -ETIMEDOUT) {
		if (IS_ENABLED(CONFIG_SDHC_BCM2835_VERBOSE)) {
			LOG_INF("  wait_int unexpected ret=%d stat=0x%08x", ret, status);
		}
		return ret;
	}

	if ((status & SDHCI_INT_ERROR) != 0U) {
		goto error;
	}

	if ((status & wait_mask) != wait_mask) {
		ret = -ETIMEDOUT;
		goto error;
	}

	bcm2835_sdhci_read_response(dev, cmd);
	bcm2835_sdhci_reg_write32(dev, SDHCI_INT_STATUS, wait_mask);

	if (IS_ENABLED(CONFIG_SDHC_BCM2835_VERBOSE)) {
		if (native_rsp == SD_RSP_TYPE_R2) {
			LOG_INF("  OK R2 %08x %08x %08x %08x", cmd->response[0], cmd->response[1],
				cmd->response[2], cmd->response[3]);
		} else {
			LOG_INF("  OK resp0=%08x", cmd->response[0]);
		}
	}

	if (xfer != NULL) {
		ret = bcm2835_sdhci_transfer_data_locked(dev, cmd, xfer);
		if (ret != 0) {
			goto error;
		}
	}

	/*
	 * Arasan pairs TRANSFER_MODE and COMMAND in one 32-bit word. The driver
	 * shadows TRANSFER_MODE across that write; clear it after each command
	 * completes so the next transfer mode + command programming cannot reuse
	 * a stale block-transfer mode for a no-data command.
	 */
	bcm2835_sdhci_iproc_shadow_clear(host);

	return 0;

error:
	if (IS_ENABLED(CONFIG_SDHC_BCM2835_VERBOSE)) {
		uint32_t present = bcm2835_sdhci_reg_read32(dev, SDHCI_PRESENT_STATE);

		LOG_INF("  ERR int_stat=0x%08x wait_mask=0x%x present=0x%08x ret=%d", status,
			wait_mask, present, ret);
	}
	bcm2835_sdhci_iproc_shadow_clear(host);
	bcm2835_sdhci_reg_write32(dev, SDHCI_INT_STATUS, SDHCI_INT_ALL_MASK);
	(void)bcm2835_sdhci_reset_locked(dev, SDHCI_RESET_CMD);
	(void)bcm2835_sdhci_reset_locked(dev, SDHCI_RESET_DATA);

	if (ret == -ETIMEDOUT) {
		return ret;
	}

	return bcm2835_sdhci_decode_error(status);
}

static int bcm2835_sdhci_reset(const struct device *dev)
{
	struct bcm2835_sdhci_data *data = dev->data;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);
	ret = bcm2835_sdhci_reset_locked(dev, SDHCI_RESET_ALL);
	k_mutex_unlock(&data->lock);

	return ret;
}

static int bcm2835_sdhci_request(const struct device *dev, struct sdhc_command *cmd,
				 struct sdhc_data *data)
{
	struct bcm2835_sdhci_data *priv = dev->data;
	int retries = cmd->retries + 1;
	int ret;

	k_mutex_lock(&priv->lock, K_FOREVER);

	do {
		ret = bcm2835_sdhci_request_once_locked(dev, cmd, data);
	} while (ret != 0 && --retries > 0);

	k_mutex_unlock(&priv->lock);

	return ret;
}

static int bcm2835_sdhci_set_io(const struct device *dev, struct sdhc_io *ios)
{
	const struct bcm2835_sdhci_config *config = dev->config;
	struct bcm2835_sdhci_data *data = dev->data;
	uint8_t power = 0U;
	uint8_t host_ctrl;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);

	switch (ios->signal_voltage) {
	case SD_VOL_3_3_V:
		power = SDHCI_POWER_330;
		break;
	case SD_VOL_3_0_V:
		power = SDHCI_POWER_300;
		break;
	case SD_VOL_1_8_V:
		power = SDHCI_POWER_180;
		break;
	default:
		ret = -ENOTSUP;
		goto out;
	}

	if (ios->power_mode == SDHC_POWER_OFF) {
		/*
		 * On-board SDIO (e.g. Pi Zero W CYW43438) is fed from regulators/GPIO
		 * (WL_REG_ON), not from a removable slot. Clearing SDHCI bus power
		 * here after the chip is up can leave the interface timing out on CMD8.
		 * Still gate SDCLK via set_clock_locked(0) below.
		 */
		if (!config->non_removable) {
			bcm2835_sdhci_reg_write8(dev, SDHCI_POWER_CONTROL, 0U);
		}
	} else {
		bcm2835_sdhci_reg_write8(dev, SDHCI_POWER_CONTROL, power | SDHCI_POWER_ON);
	}

	host_ctrl = bcm2835_sdhci_reg_read8(dev, SDHCI_HOST_CONTROL);
	host_ctrl &= ~(SDHCI_CTRL_4BITBUS | SDHCI_CTRL_HISPD);

	if (ios->bus_width == SDHC_BUS_WIDTH4BIT) {
		host_ctrl |= SDHCI_CTRL_4BITBUS;
	} else if (ios->bus_width != SDHC_BUS_WIDTH1BIT) {
		ret = -ENOTSUP;
		goto out;
	}

	switch (ios->timing) {
	case SDHC_TIMING_LEGACY:
	case SDHC_TIMING_HS:
	case SDHC_TIMING_SDR12:
	case SDHC_TIMING_SDR25:
		break;
	default:
		ret = -ENOTSUP;
		goto out;
	}

	bcm2835_sdhci_reg_write8(dev, SDHCI_HOST_CONTROL, host_ctrl);
	ret = bcm2835_sdhci_set_clock_locked(dev, ios->clock);
	if (ret == 0) {
		data->host_io = *ios;
	}

out:
	k_mutex_unlock(&data->lock);
	return ret;
}

static int bcm2835_sdhci_get_card_present(const struct device *dev)
{
	const struct bcm2835_sdhci_config *config = dev->config;

	if (config->non_removable) {
		return 1;
	}

	return (bcm2835_sdhci_reg_read32(dev, SDHCI_PRESENT_STATE) &
		SDHCI_PRESENT_CARD_PRESENT) != 0U;
}

static int bcm2835_sdhci_execute_tuning(const struct device *dev)
{
	ARG_UNUSED(dev);

	return -ENOTSUP;
}

static int bcm2835_sdhci_card_busy(const struct device *dev)
{
	uint32_t state = bcm2835_sdhci_reg_read32(dev, SDHCI_PRESENT_STATE);

	return ((state & SDHCI_PRESENT_DAT_ACTIVE) != 0U) ||
	       ((state & SDHCI_PRESENT_DATA0_LEVEL) == 0U);
}

static int bcm2835_sdhci_get_host_props(const struct device *dev, struct sdhc_host_props *props)
{
	struct bcm2835_sdhci_data *data = dev->data;

	*props = data->props;
	return 0;
}

static int bcm2835_sdhci_enable_interrupt(const struct device *dev, sdhc_interrupt_cb_t callback,
					  int sources, void *user_data)
{
	struct bcm2835_sdhci_data *data = dev->data;
	uint32_t signal_en;
	int ret = 0;

	if (sources != SDHC_INT_SDIO || callback == NULL) {
		return -ENOTSUP;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	data->sdio_cb = callback;
	data->sdio_cb_user_data = user_data;
	signal_en = bcm2835_sdhci_reg_read32(dev, SDHCI_SIGNAL_ENABLE);
	signal_en |= SDHCI_INT_CARD_INT;
	bcm2835_sdhci_reg_write32(dev, SDHCI_SIGNAL_ENABLE, signal_en);
	k_mutex_unlock(&data->lock);

	return ret;
}

static int bcm2835_sdhci_disable_interrupt(const struct device *dev, int sources)
{
	struct bcm2835_sdhci_data *data = dev->data;
	uint32_t signal_en;

	if (sources != SDHC_INT_SDIO) {
		return -ENOTSUP;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	data->sdio_cb = NULL;
	data->sdio_cb_user_data = NULL;
	signal_en = bcm2835_sdhci_reg_read32(dev, SDHCI_SIGNAL_ENABLE);
	signal_en &= ~SDHCI_INT_CARD_INT;
	bcm2835_sdhci_reg_write32(dev, SDHCI_SIGNAL_ENABLE, signal_en);
	k_mutex_unlock(&data->lock);

	return 0;
}

static void bcm2835_sdhci_isr(const struct device *dev)
{
	struct bcm2835_sdhci_data *data = dev->data;
	uint32_t status = bcm2835_sdhci_reg_read32(dev, SDHCI_INT_STATUS);

	if ((status & SDHCI_INT_CARD_INT) == 0U) {
		return;
	}

	bcm2835_sdhci_reg_write32(dev, SDHCI_INT_STATUS, SDHCI_INT_CARD_INT);

	if (data->sdio_cb != NULL) {
		data->sdio_cb(dev, SDHC_INT_SDIO, data->sdio_cb_user_data);
	}
}

static int bcm2835_sdhci_init(const struct device *dev)
{
	const struct bcm2835_sdhci_config *config = dev->config;
	struct bcm2835_sdhci_data *data = dev->data;
	uint32_t caps;
	uint32_t current;
	uint32_t base_freq_mhz;
	uint32_t mbox_mmc_hz = 0U;
	int mbox_ret;
	int ret;

	DEVICE_MMIO_MAP(dev, K_MEM_CACHE_NONE);

	/*
	 * VideoCore holds the SDHCI power domain and EMMC root clock off until
	 * the property mailbox SET_POWER_STATE / GET_CLOCK_RATE sequence runs
	 * (see U-Boot bcm2835_get_mmc_clock). Without this, commands time out.
	 */
	mbox_ret = bcm2835_mbox_mmc_prepare(BCM2835_MBOX_CLOCK_ID_EMMC, &mbox_mmc_hz);
	if (mbox_ret != 0) {
		LOG_WRN("BCM2835 mailbox SDHCI power/clock failed: %d", mbox_ret);
		mbox_mmc_hz = 0U;
	}

	ret = pinctrl_apply_state(config->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret != 0) {
		return ret;
	}

	k_mutex_init(&data->lock);
	config->irq_config_func(dev);

	ret = bcm2835_sdhci_reset_locked(dev, SDHCI_RESET_ALL);
	if (ret != 0) {
		return ret;
	}

	/* Linux SDHCI_QUIRK2_PRESET_VALUE_BROKEN for brcm,bcm2835-sdhci */
	{
		uint16_t hc2 = bcm2835_sdhci_reg_read16(dev, SDHCI_HOST_CONTROL2);

		hc2 &= (uint16_t)~SDHCI_CTRL_PRESET_VAL_ENABLE;
		bcm2835_sdhci_reg_write16(dev, SDHCI_HOST_CONTROL2, hc2);
	}

	caps = bcm2835_sdhci_reg_read32(dev, SDHCI_CAPABILITIES);
	current = bcm2835_sdhci_reg_read32(dev, SDHCI_MAX_CURRENT);
	base_freq_mhz = (caps & SDHCI_CAP_BASE_FREQ_MASK) >> SDHCI_CAP_BASE_FREQ_SHIFT;

	if (mbox_mmc_hz != 0U) {
		data->base_clock_hz = mbox_mmc_hz;
	} else {
		data->base_clock_hz = base_freq_mhz != 0U ? MHZ(base_freq_mhz) : config->max_freq;
	}
	data->write_delay_us = DIV_ROUND_UP(BCM2835_SDHCI_IO_DELAY_TICKS * USEC_PER_SEC,
					    MAX(config->min_freq, BCM2835_SDHCI_MIN_FREQ_HZ));

	if (IS_ENABLED(CONFIG_SDHC_BCM2835_VERBOSE)) {
		uint16_t ver = bcm2835_sdhci_reg_read16(dev, SDHCI_HOST_VERSION);

		LOG_INF("bcm2835 SDHCI init: mbox_ret=%d mbox_hz=%u caps=0x%08x base_cap_mhz=%u "
			"base_hz=%u host_ver=0x%04x write_delay_us=%u",
			mbox_ret, mbox_mmc_hz, caps, (unsigned int)base_freq_mhz,
			data->base_clock_hz, ver, data->write_delay_us);
	}

	memset(&data->props, 0, sizeof(data->props));
	data->props.f_min = config->min_freq;
	data->props.f_max = MIN(config->max_freq, data->base_clock_hz);
	data->props.power_delay = config->power_delay_ms;
	data->props.is_spi = false;
	data->props.bus_4_bit_support = true;
	data->props.max_current_330 = current & SDHCI_MAX_CURRENT_330_MASK;
	data->props.max_current_300 = (current & SDHCI_MAX_CURRENT_300_MASK) >> 8;
	data->props.max_current_180 = (current & SDHCI_MAX_CURRENT_180_MASK) >> 16;
	data->props.host_caps.timeout_clk_freq = caps & SDHCI_CAP_TIMEOUT_FREQ_MASK;
	data->props.host_caps.timeout_clk_unit = (caps & SDHCI_CAP_TIMEOUT_FREQ_MHZ) != 0U;
	data->props.host_caps.sd_base_clk =
		base_freq_mhz != 0U ? base_freq_mhz :
		(mbox_mmc_hz != 0U ? (uint8_t)(mbox_mmc_hz / 1000000U) : 0U);
	data->props.host_caps.max_blk_len = (caps & SDHCI_CAP_MAX_BLK_MASK) >> 16;
	data->props.host_caps.high_spd_support = (caps & SDHCI_CAP_CAN_DO_HISPD) != 0U;
	data->props.host_caps.vol_330_support = (caps & SDHCI_CAP_CAN_VDD_330) != 0U;
	data->props.host_caps.vol_300_support = (caps & SDHCI_CAP_CAN_VDD_300) != 0U;
	data->props.host_caps.vol_180_support = (caps & SDHCI_CAP_CAN_VDD_180) != 0U;

	/*
	 * On BCM2835 the ARM-visible Arasan block often reads CAPABILITIES as zero
	 * (no VDD or base-clock bits). The SD stack then defaults to 1.8V in
	 * sd_init_io(), but Raspberry Pi SDIO (e.g. CYW43438) is 3.3V — every
	 * command times out with INT_ERROR|INT_TIMEOUT until we advertise 3.3V.
	 */
	if ((data->props.host_caps.vol_330_support | data->props.host_caps.vol_300_support |
	     data->props.host_caps.vol_180_support) == 0U) {
		data->props.host_caps.vol_330_support = true;
	}

	memset(&data->host_io, 0, sizeof(data->host_io));
	data->host_io.signal_voltage = data->props.host_caps.vol_330_support ? SD_VOL_3_3_V :
				      (data->props.host_caps.vol_300_support ? SD_VOL_3_0_V :
				       SD_VOL_1_8_V);
	bcm2835_sdhci_iproc_shadow_clear(data);

	bcm2835_sdhci_reg_write32(dev, SDHCI_INT_ENABLE,
				  SDHCI_INT_CMD_MASK | SDHCI_INT_DATA_MASK | SDHCI_INT_CARD_INT |
					  SDHCI_INT_ACMD12ERR);
	bcm2835_sdhci_reg_write32(dev, SDHCI_SIGNAL_ENABLE, 0U);
	bcm2835_sdhci_reg_write32(dev, SDHCI_INT_STATUS, SDHCI_INT_ALL_MASK);

	return 0;
}

static DEVICE_API(sdhc, bcm2835_sdhci_api) = {
	.reset = bcm2835_sdhci_reset,
	.request = bcm2835_sdhci_request,
	.set_io = bcm2835_sdhci_set_io,
	.get_card_present = bcm2835_sdhci_get_card_present,
	.execute_tuning = bcm2835_sdhci_execute_tuning,
	.card_busy = bcm2835_sdhci_card_busy,
	.get_host_props = bcm2835_sdhci_get_host_props,
	.enable_interrupt = bcm2835_sdhci_enable_interrupt,
	.disable_interrupt = bcm2835_sdhci_disable_interrupt,
};

#define BCM2835_SDHCI_INIT(inst)                                                                   \
	PINCTRL_DT_INST_DEFINE(inst);                                                              \
                                                                                                   \
	static void bcm2835_sdhci_irq_config_##inst(const struct device *dev)                      \
	{                                                                                          \
		ARG_UNUSED(dev);                                                                   \
		IRQ_CONNECT(DT_INST_IRQN(inst), BCM2835_SDHCI_IRQ_PRIO, bcm2835_sdhci_isr,        \
			    DEVICE_DT_INST_GET(inst), 0);                                         \
		irq_enable(DT_INST_IRQN(inst));                                                   \
	}                                                                                          \
                                                                                                   \
	static const struct bcm2835_sdhci_config bcm2835_sdhci_config_##inst = {                   \
		DEVICE_MMIO_ROM_INIT(DT_DRV_INST(inst)),                                          \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(inst),                                     \
		.irq_config_func = bcm2835_sdhci_irq_config_##inst,                               \
		.min_freq = DT_INST_PROP(inst, min_bus_freq),                                     \
		.max_freq = DT_INST_PROP(inst, max_bus_freq),                                     \
		.power_delay_ms = DT_INST_PROP(inst, power_delay_ms),                             \
		.non_removable = DT_INST_PROP_OR(inst, non_removable, 0),                          \
	};                                                                                         \
                                                                                                   \
	static struct bcm2835_sdhci_data bcm2835_sdhci_data_##inst;                                \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, bcm2835_sdhci_init, NULL, &bcm2835_sdhci_data_##inst,          \
			      &bcm2835_sdhci_config_##inst, POST_KERNEL,                        \
			      CONFIG_SDHC_INIT_PRIORITY, &bcm2835_sdhci_api);

DT_INST_FOREACH_STATUS_OKAY(BCM2835_SDHCI_INIT)
