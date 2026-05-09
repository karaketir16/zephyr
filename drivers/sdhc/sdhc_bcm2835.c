/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT brcm_bcm2835_sdhci

#include <zephyr/arch/cpu.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/sdhc.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/sys_io.h>

#include <string.h>

LOG_MODULE_REGISTER(sdhc_bcm2835, CONFIG_SDHC_LOG_LEVEL);

#define SDHCI_DMA_ADDR		0x00
#define SDHCI_BLOCK_SIZE	0x04
#define SDHCI_BLOCK_COUNT	0x06
#define SDHCI_ARG		0x08
#define SDHCI_TRANSFER_MODE	0x0c
#define SDHCI_COMMAND		0x0e
#define SDHCI_RESPONSE		0x10
#define SDHCI_BUFFER		0x20
#define SDHCI_PRESENT_STATE	0x24
#define SDHCI_HOST_CONTROL	0x28
#define SDHCI_POWER_CONTROL	0x29
#define SDHCI_CLOCK_CONTROL	0x2c
#define SDHCI_TIMEOUT_CONTROL	0x2e
#define SDHCI_SOFTWARE_RESET	0x2f
#define SDHCI_INT_STATUS	0x30
#define SDHCI_INT_ENABLE	0x34
#define SDHCI_SIGNAL_ENABLE	0x38
#define SDHCI_HOST_CONTROL2	0x3e

#define SDHCI_CMD_INHIBIT	BIT(0)
#define SDHCI_DATA_INHIBIT	BIT(1)
#define SDHCI_DATA_AVAILABLE	BIT(11)
#define SDHCI_SPACE_AVAILABLE	BIT(10)
#define SDHCI_DAT0_LEVEL	BIT(20)

#define SDHCI_CTRL_4BITBUS	BIT(1)
#define SDHCI_CTRL_HISPD	BIT(2)

#define SDHCI_POWER_ON		BIT(0)
#define SDHCI_POWER_330		0x0e

#define SDHCI_CLOCK_INT_EN	BIT(0)
#define SDHCI_CLOCK_INT_STABLE	BIT(1)
#define SDHCI_CLOCK_CARD_EN	BIT(2)
#define SDHCI_DIVIDER_SHIFT	8
#define SDHCI_DIVIDER_HI_SHIFT	6

#define SDHCI_RESET_ALL		BIT(0)
#define SDHCI_RESET_CMD		BIT(1)
#define SDHCI_RESET_DATA	BIT(2)

#define SDHCI_INT_RESPONSE	BIT(0)
#define SDHCI_INT_DATA_END	BIT(1)
#define SDHCI_INT_DATA_AVAIL	BIT(5)
#define SDHCI_INT_SPACE_AVAIL	BIT(4)
#define SDHCI_INT_CARD_INT	BIT(8)
#define SDHCI_INT_ERROR	BIT(15)
#define SDHCI_INT_TIMEOUT	BIT(16)
#define SDHCI_INT_CRC		BIT(17)
#define SDHCI_INT_END_BIT	BIT(18)
#define SDHCI_INT_INDEX	BIT(19)
#define SDHCI_INT_DATA_TIMEOUT	BIT(20)
#define SDHCI_INT_DATA_CRC	BIT(21)
#define SDHCI_INT_DATA_END_BIT	BIT(22)

#define SDHCI_TRNS_DMA		BIT(0)
#define SDHCI_TRNS_BLK_CNT_EN	BIT(1)
#define SDHCI_TRNS_READ	BIT(4)
#define SDHCI_TRNS_MULTI	BIT(5)

#define SDHCI_CMD_RESP_NONE	0x0000
#define SDHCI_CMD_RESP_LONG	0x0001
#define SDHCI_CMD_RESP_SHORT	0x0002
#define SDHCI_CMD_RESP_BUSY	0x0003
#define SDHCI_CMD_CRC		BIT(3)
#define SDHCI_CMD_INDEX	BIT(4)
#define SDHCI_CMD_DATA		BIT(5)

#define BCM2835_WRITE_DELAY_US(freq) (((2 * 1000000U) / MAX(freq, 400000U)) + 1U)

#define BCM2835_CPRMAN_BASE	0x20101000U
#define BCM2835_GPIO_BASE	0x20200000U
#define GPIO_GPFSEL0		(BCM2835_GPIO_BASE + 0x00U)
#define GPIO_GPFSEL3		(BCM2835_GPIO_BASE + 0x0cU)
#define GPIO_GPSET1		(BCM2835_GPIO_BASE + 0x20U)
#define GPIO_GPCLR1		(BCM2835_GPIO_BASE + 0x2cU)
#define GPIO_GPLEV1		(BCM2835_GPIO_BASE + 0x38U)
#define GPIO_GPPUD		(BCM2835_GPIO_BASE + 0x94U)
#define GPIO_GPPUDCLK1		(BCM2835_GPIO_BASE + 0x9cU)
#define CM_PASSWORD		0x5a000000U
#define CM_EMMCCTL		0x1c0U
#define CM_EMMCDIV		0x1c4U
#define CM_GP2CTL		0x080U
#define CM_GP2DIV		0x084U
#define CM_ENABLE		BIT(4)
#define CM_GATE			BIT(6)
#define CM_BUSY			BIT(7)
#define CM_SRC_PLLD_PER		6U
#define CM_SRC_OSC		1U
#define CM_DIV_FRAC_BITS	12U

#define GPIO_FUNC_IN		0U
#define GPIO_FUNC_OUT		1U
#define BCM2835_GPIO_PULL_NONE	0U
#define BCM2835_GPIO_PULL_UP	2U

#define SD_CLK_PIN		34U
#define SD_CMD_PIN		35U
#define SD_D0_PIN		36U
#define SD_D1_PIN		37U
#define SD_D2_PIN		38U
#define SD_D3_PIN		39U

#define SDIO_BB_MSG_BYTES	6U
#define SDIO_BB_MSG_BITS	48U
#define SDIO_BB_DATA_PINS	4U
#define SDIO_BB_CLK_DELAY_US	1U
#define SDIO_BB_RSP_WAIT_CYCLES	200U
#define SDIO_BB_DATA_WAIT_CYCLES 2000U
#define SDIO_BB_CRC16R_POLY	(BIT(15) | BIT(10) | BIT(3))

struct sdhc_bcm2835_config {
	mem_addr_t base;
	uint32_t input_clk;
	uint32_t max_freq;
	uint32_t min_freq;
	uint32_t power_delay_ms;
	uint8_t bus_width;
	struct gpio_dt_spec reset_gpio;
	const struct pinctrl_dev_config *pcfg;
};

struct sdhc_bcm2835_data {
	struct k_mutex lock;
	uint32_t clock;
	uint32_t trns_shadow; /* saved TRANSFER_MODE for combined cmd write */
	bool bitbang_ready;
	bool bitbang_reset_done;
	uint64_t crc16r_table[BIT(SDIO_BB_DATA_PINS)];
};

static uint32_t reg_read32(const struct device *dev, uint32_t reg)
{
	const struct sdhc_bcm2835_config *cfg = dev->config;

	return sys_read32(cfg->base + reg);
}

static uint8_t reg_read8(const struct device *dev, uint32_t reg)
{
	return (reg_read32(dev, reg & ~3U) >> ((reg & 3U) * 8U)) & 0xffU;
}

static uint16_t reg_read16(const struct device *dev, uint32_t reg)
{
	return (reg_read32(dev, reg & ~3U) >> ((reg & 2U) * 8U)) & 0xffffU;
}

static void reg_write32(const struct device *dev, uint32_t val, uint32_t reg)
{
	const struct sdhc_bcm2835_config *cfg = dev->config;
	struct sdhc_bcm2835_data *data = dev->data;

	sys_write32(val, cfg->base + reg);
	k_busy_wait(BCM2835_WRITE_DELAY_US(data->clock));
}

static void reg_write16(const struct device *dev, uint16_t val, uint32_t reg)
{
	uint32_t shift = (reg & 2U) * 8U;
	uint32_t old = reg_read32(dev, reg & ~3U);

	reg_write32(dev, (old & ~(0xffffU << shift)) | ((uint32_t)val << shift), reg & ~3U);
}

static void reg_write8(const struct device *dev, uint8_t val, uint32_t reg)
{
	uint32_t shift = (reg & 3U) * 8U;
	uint32_t old = reg_read32(dev, reg & ~3U);

	reg_write32(dev, (old & ~(0xffU << shift)) | ((uint32_t)val << shift), reg & ~3U);
}

static int wait_for_mask(const struct device *dev, uint32_t reg, uint32_t mask,
			 uint32_t want, int timeout_us)
{
	while (timeout_us > 0) {
		if ((reg_read32(dev, reg) & mask) == want) {
			return 0;
		}
		k_busy_wait(10);
		timeout_us -= 10;
	}

	return -ETIMEDOUT;
}

static int wait_for_mask8(const struct device *dev, uint32_t reg, uint8_t mask,
			  uint8_t want, int timeout_us)
{
	while (timeout_us > 0) {
		if ((reg_read8(dev, reg) & mask) == want) {
			return 0;
		}
		k_busy_wait(10);
		timeout_us -= 10;
	}

	return -ETIMEDOUT;
}

static int wait_for_mask16(const struct device *dev, uint32_t reg, uint16_t mask,
			   uint16_t want, int timeout_us)
{
	while (timeout_us > 0) {
		if ((reg_read16(dev, reg) & mask) == want) {
			return 0;
		}
		k_busy_wait(10);
		timeout_us -= 10;
	}

	return -ETIMEDOUT;
}

static void bb_gpio_set_func(unsigned int pin, unsigned int func)
{
	uint32_t reg = GPIO_GPFSEL0 + ((pin / 10U) * 4U);
	uint32_t shift = (pin % 10U) * 3U;
	uint32_t val = sys_read32(reg);

	val &= ~(7U << shift);
	val |= (func & 7U) << shift;
	sys_write32(val, reg);
}

static void bb_gpio_set_pull(unsigned int pin, unsigned int pull)
{
	sys_write32(pull, GPIO_GPPUD);
	k_busy_wait(5);
	sys_write32(BIT(pin - 32U), GPIO_GPPUDCLK1);
	k_busy_wait(5);
	sys_write32(0, GPIO_GPPUD);
	sys_write32(0, GPIO_GPPUDCLK1);
}

static void bb_gpio_write(unsigned int pin, unsigned int val)
{
	sys_write32(BIT(pin - 32U), val ? GPIO_GPSET1 : GPIO_GPCLR1);
}

static unsigned int bb_gpio_read(unsigned int pin)
{
	return (sys_read32(GPIO_GPLEV1) >> (pin - 32U)) & 1U;
}

static void bb_gpio_write4(uint8_t val)
{
	uint32_t bits = (uint32_t)(val & 0x0fU) << (SD_D0_PIN - 32U);

	sys_write32((0x0fU << (SD_D0_PIN - 32U)) & ~bits, GPIO_GPCLR1);
	sys_write32(bits, GPIO_GPSET1);
}

static uint8_t bb_gpio_read4(void)
{
	return (sys_read32(GPIO_GPLEV1) >> (SD_D0_PIN - 32U)) & 0x0fU;
}

static void bb_gpio_set_data_func(unsigned int func)
{
	for (unsigned int pin = SD_D0_PIN; pin <= SD_D3_PIN; pin++) {
		bb_gpio_set_func(pin, func);
		if (func == GPIO_FUNC_IN) {
			bb_gpio_set_pull(pin, BCM2835_GPIO_PULL_UP);
		}
	}
}

static uint64_t sdio_bb_quadval(uint16_t val)
{
	uint64_t ret = 0;

	for (int i = 0; i < 16; i++) {
		if (val & BIT(i)) {
			ret |= 1ULL << (i * SDIO_BB_DATA_PINS);
		}
	}

	return ret;
}

static void sdio_bb_crc16r_init(struct sdhc_bcm2835_data *data)
{
	uint64_t poly = sdio_bb_quadval(SDIO_BB_CRC16R_POLY);

	for (int i = 0; i < ARRAY_SIZE(data->crc16r_table); i++) {
		data->crc16r_table[i] = ((i & BIT(3)) ? poly << 3 : 0) |
					((i & BIT(2)) ? poly << 2 : 0) |
					((i & BIT(1)) ? poly << 1 : 0) |
					((i & BIT(0)) ? poly : 0);
	}
}

static void sdio_bb_setup(const struct device *dev)
{
	struct sdhc_bcm2835_data *data = dev->data;

	if (data->bitbang_ready) {
		return;
	}

	bb_gpio_set_func(SD_CLK_PIN, GPIO_FUNC_OUT);
	bb_gpio_set_pull(SD_CLK_PIN, BCM2835_GPIO_PULL_NONE);
	bb_gpio_write(SD_CLK_PIN, 0);

	for (unsigned int pin = SD_CMD_PIN; pin <= SD_D3_PIN; pin++) {
		bb_gpio_set_func(pin, GPIO_FUNC_IN);
		bb_gpio_set_pull(pin, BCM2835_GPIO_PULL_UP);
	}

	sdio_bb_crc16r_init(data);
	data->bitbang_ready = true;
	LOG_DBG("GPIO34-39 switched to bit-banged SDIO mode GPFSEL3=0x%08x",
		sys_read32(GPIO_GPFSEL3));
}

static uint8_t sdio_bb_crc7_byte(uint8_t data)
{
	uint16_t crc = data;

	for (int bit = 0; bit < 8; bit++) {
		crc <<= 1;
		if (crc & 0x100U) {
			crc ^= 0x112U;
		}
	}

	return crc & 0xffU;
}

static uint8_t sdio_bb_crc7(uint8_t *cmd)
{
	uint8_t crc = 0;

	for (int i = 0; i < SDIO_BB_MSG_BYTES - 1; i++) {
		crc = sdio_bb_crc7_byte(crc ^ cmd[i]);
	}

	return crc | 1U;
}

static void sdio_bb_clock_cycle(void)
{
	k_busy_wait(SDIO_BB_CLK_DELAY_US);
	bb_gpio_write(SD_CLK_PIN, 1);
	k_busy_wait(SDIO_BB_CLK_DELAY_US);
	bb_gpio_write(SD_CLK_PIN, 0);
}

static void sdio_bb_idle_clocks(unsigned int cycles)
{
	for (unsigned int i = 0; i < cycles; i++) {
		sdio_bb_clock_cycle();
	}
}

static void sdio_bb_write_cmd(uint8_t *cmd)
{
	bb_gpio_set_func(SD_CMD_PIN, GPIO_FUNC_OUT);

	for (int bit = 0; bit < SDIO_BB_MSG_BITS; bit++) {
		unsigned int byte = bit / 8;
		unsigned int mask = BIT(7 - (bit % 8));

		bb_gpio_write(SD_CMD_PIN, (cmd[byte] & mask) != 0);
		sdio_bb_clock_cycle();
	}

	bb_gpio_set_func(SD_CMD_PIN, GPIO_FUNC_IN);
	bb_gpio_set_pull(SD_CMD_PIN, BCM2835_GPIO_PULL_UP);
}

static int sdio_bb_read_rsp(uint8_t *rsp)
{
	memset(rsp, 0, SDIO_BB_MSG_BYTES);

	for (int wait = 0; wait < SDIO_BB_RSP_WAIT_CYCLES; wait++) {
		k_busy_wait(SDIO_BB_CLK_DELAY_US);
		bb_gpio_write(SD_CLK_PIN, 1);
		if (bb_gpio_read(SD_CMD_PIN) == 0) {
			k_busy_wait(SDIO_BB_CLK_DELAY_US);
			bb_gpio_write(SD_CLK_PIN, 0);
			goto got_start;
		}
		k_busy_wait(SDIO_BB_CLK_DELAY_US);
		bb_gpio_write(SD_CLK_PIN, 0);
	}

	return 0;

got_start:
	for (int bit = 1; bit < SDIO_BB_MSG_BITS; bit++) {
		unsigned int byte = bit / 8;

		k_busy_wait(SDIO_BB_CLK_DELAY_US);
		bb_gpio_write(SD_CLK_PIN, 1);
		rsp[byte] = (rsp[byte] << 1) | bb_gpio_read(SD_CMD_PIN);
		k_busy_wait(SDIO_BB_CLK_DELAY_US);
		bb_gpio_write(SD_CLK_PIN, 0);
	}

	return SDIO_BB_MSG_BITS;
}

static int sdio_bb_cmd_raw(unsigned int opcode, uint32_t arg, uint8_t *rsp)
{
	uint8_t raw_cmd[SDIO_BB_MSG_BYTES] = {
		0x40U | (opcode & 0x3fU),
		(arg >> 24) & 0xffU,
		(arg >> 16) & 0xffU,
		(arg >> 8) & 0xffU,
		arg & 0xffU,
		0,
	};
	int bits;

	raw_cmd[5] = sdio_bb_crc7(raw_cmd);
	sdio_bb_idle_clocks(2);
	sdio_bb_write_cmd(raw_cmd);
	bits = sdio_bb_read_rsp(rsp);
	sdio_bb_idle_clocks(1);

	return bits == SDIO_BB_MSG_BITS ? 0 : -ETIMEDOUT;
}

static uint32_t sdio_bb_cmd52_arg(unsigned int func, unsigned int addr, uint8_t data, bool write)
{
	return (write ? BIT(31) : 0U) | ((func & 7U) << 28) |
	       ((addr & SDIO_CMD_ARG_REG_ADDR_MASK) << SDIO_CMD_ARG_REG_ADDR_SHIFT) | data;
}

static void sdio_bb_soft_reset_if_needed(const struct device *dev, struct sdhc_command *cmd)
{
	struct sdhc_bcm2835_data *data = dev->data;
	uint8_t rsp[SDIO_BB_MSG_BYTES];

	if (data->bitbang_reset_done || cmd->opcode != SD_GO_IDLE_STATE) {
		return;
	}

	(void)sdio_bb_cmd_raw(SDIO_RW_DIRECT, sdio_bb_cmd52_arg(0, SDIO_CCCR_ABORT, 0, false),
			      rsp);
	k_sleep(K_MSEC(20));
	(void)sdio_bb_cmd_raw(SDIO_RW_DIRECT, sdio_bb_cmd52_arg(0, SDIO_CCCR_ABORT, 8, true),
			      rsp);
	k_sleep(K_MSEC(20));
	data->bitbang_reset_done = true;
}

static bool sdio_bb_supported_cmd(const struct sdhc_command *cmd, const struct sdhc_data *data)
{
	if (data != NULL) {
		return cmd->opcode == SDIO_RW_EXTENDED;
	}

	switch (cmd->opcode) {
	case SD_GO_IDLE_STATE:
	case SD_SEND_RELATIVE_ADDR:
	case SDIO_SEND_OP_COND:
	case SD_SELECT_CARD:
	case SD_SEND_IF_COND:
	case SDIO_RW_DIRECT:
		return true;
	default:
		return false;
	}
}

static int sdio_bb_read_cmd53(const struct device *dev, uint8_t *rsp,
			      uint8_t *buf, uint32_t nbytes)
{
	struct sdhc_bcm2835_data *data = dev->data;
	unsigned int rbits = 1;
	unsigned int dbits = 0;
	bool data_started = false;
	uint64_t crc = 0;

	memset(rsp, 0, SDIO_BB_MSG_BYTES);
	if (buf != NULL && nbytes > 0) {
		buf[0] = 0;
	}

	for (int wait = 0; wait < SDIO_BB_DATA_WAIT_CYCLES; wait++) {
		k_busy_wait(SDIO_BB_CLK_DELAY_US);
		bb_gpio_write(SD_CLK_PIN, 1);
		if (bb_gpio_read(SD_CMD_PIN) == 0) {
			k_busy_wait(SDIO_BB_CLK_DELAY_US);
			bb_gpio_write(SD_CLK_PIN, 0);
			goto got_response_start;
		}
		k_busy_wait(SDIO_BB_CLK_DELAY_US);
		bb_gpio_write(SD_CLK_PIN, 0);
	}

	return -ETIMEDOUT;

got_response_start:
	while (rbits < SDIO_BB_MSG_BITS || data_started) {
		uint8_t nibble;

		k_busy_wait(SDIO_BB_CLK_DELAY_US);
		bb_gpio_write(SD_CLK_PIN, 1);

		if (rbits < SDIO_BB_MSG_BITS) {
			unsigned int byte = rbits / 8U;

			rsp[byte] = (rsp[byte] << 1) | bb_gpio_read(SD_CMD_PIN);
			rbits++;
		}

		nibble = bb_gpio_read4();
		if (!data_started && nibble == 0) {
			data_started = true;
		} else if (data_started) {
			if (buf != NULL && (dbits / 8U) < nbytes) {
				buf[dbits / 8U] = (buf[dbits / 8U] << SDIO_BB_DATA_PINS) | nibble;
			}
			crc = (crc >> SDIO_BB_DATA_PINS) ^
			      data->crc16r_table[(nibble ^ (uint8_t)crc) &
						 (BIT(SDIO_BB_DATA_PINS) - 1U)];
			dbits += SDIO_BB_DATA_PINS;
			if ((dbits / 8U) >= nbytes + (SDIO_BB_DATA_PINS * 2U)) {
				data_started = false;
			} else if (buf != NULL && (dbits / 8U) < nbytes && (dbits % 8U) == 0) {
				buf[dbits / 8U] = 0;
			}
		}

		k_busy_wait(SDIO_BB_CLK_DELAY_US);
		bb_gpio_write(SD_CLK_PIN, 0);
	}

	if (dbits < SDIO_BB_DATA_PINS * 2U * 8U) {
		return -EIO;
	}
	dbits -= SDIO_BB_DATA_PINS * 2U * 8U;
	if ((dbits / 8U) != nbytes) {
		return -EIO;
	}
	if (crc != 0) {
		LOG_DBG("bitbang cmd53 read CRC residue 0x%016llx", crc);
	}

	return 0;
}

static int sdio_bb_read_data_ack(uint8_t *ack)
{
	*ack = 0;

	for (int bit = 0; bit < 8; bit++) {
		k_busy_wait(SDIO_BB_CLK_DELAY_US);
		bb_gpio_write(SD_CLK_PIN, 1);
		*ack = (*ack << 1) | bb_gpio_read(SD_D0_PIN);
		k_busy_wait(SDIO_BB_CLK_DELAY_US);
		bb_gpio_write(SD_CLK_PIN, 0);
	}

	return 0;
}

static int sdio_bb_write_cmd53_data(const struct device *dev, const uint8_t *buf,
				    uint32_t nbytes)
{
	struct sdhc_bcm2835_data *data = dev->data;
	uint64_t crc = 0;
	uint8_t ack;

	sdio_bb_idle_clocks(1);
	bb_gpio_write4(0x0fU);
	bb_gpio_set_data_func(GPIO_FUNC_OUT);

	bb_gpio_write4(0);
	sdio_bb_clock_cycle();

	for (uint32_t i = 0; i < nbytes; i++) {
		uint8_t nibble = buf[i] >> 4;

		bb_gpio_write4(nibble);
		crc = (crc >> SDIO_BB_DATA_PINS) ^
		      data->crc16r_table[(nibble ^ (uint8_t)crc) &
					 (BIT(SDIO_BB_DATA_PINS) - 1U)];
		sdio_bb_clock_cycle();

		nibble = buf[i] & 0x0fU;
		bb_gpio_write4(nibble);
		crc = (crc >> SDIO_BB_DATA_PINS) ^
		      data->crc16r_table[(nibble ^ (uint8_t)crc) &
					 (BIT(SDIO_BB_DATA_PINS) - 1U)];
		sdio_bb_clock_cycle();
	}

	for (int i = 0; i < 16; i++) {
		bb_gpio_write4(crc & 0x0fU);
		crc >>= SDIO_BB_DATA_PINS;
		sdio_bb_clock_cycle();
	}

	bb_gpio_write4(0x0fU);
	bb_gpio_set_data_func(GPIO_FUNC_IN);
	sdio_bb_idle_clocks(1);
	sdio_bb_read_data_ack(&ack);
	LOG_DBG("bitbang cmd53 write ack=0x%02x", ack);

	return 0;
}

static int sdio_bb_request_cmd53(const struct device *dev, struct sdhc_command *cmd,
				 struct sdhc_data *xfer)
{
	uint8_t raw_cmd[SDIO_BB_MSG_BYTES] = {
		0x40U | (cmd->opcode & 0x3fU),
		(cmd->arg >> 24) & 0xffU,
		(cmd->arg >> 16) & 0xffU,
		(cmd->arg >> 8) & 0xffU,
		cmd->arg & 0xffU,
		0,
	};
	uint8_t rsp[SDIO_BB_MSG_BYTES];
	uint32_t total = xfer->block_size * xfer->blocks;
	bool read = (cmd->arg & BIT(31)) == 0;
	int ret;

	sdio_bb_setup(dev);
	raw_cmd[5] = sdio_bb_crc7(raw_cmd);

	sdio_bb_idle_clocks(2);
	sdio_bb_write_cmd(raw_cmd);

	if (read) {
		ret = sdio_bb_read_cmd53(dev, rsp, xfer->data, total);
	} else {
		int bits = sdio_bb_read_rsp(rsp);

		if (bits != SDIO_BB_MSG_BITS) {
			ret = -ETIMEDOUT;
		} else {
			ret = sdio_bb_write_cmd53_data(dev, xfer->data, total);
		}
	}
	sdio_bb_idle_clocks(1);
	if (ret) {
		return ret;
	}

	cmd->response[0] = ((uint32_t)rsp[1] << 24) | ((uint32_t)rsp[2] << 16) |
			   ((uint32_t)rsp[3] << 8) | rsp[4];
	xfer->bytes_xfered = total;

	LOG_DBG("bitbang cmd53 %s arg=0x%08x resp=0x%08x len=%u raw=%02x %02x %02x %02x %02x %02x",
		read ? "read" : "write", cmd->arg, cmd->response[0], total,
		rsp[0], rsp[1], rsp[2], rsp[3], rsp[4], rsp[5]);

	return 0;
}

static int sdio_bb_request(const struct device *dev, struct sdhc_command *cmd)
{
	uint8_t raw_cmd[SDIO_BB_MSG_BYTES] = {
		0x40U | (cmd->opcode & 0x3fU),
		(cmd->arg >> 24) & 0xffU,
		(cmd->arg >> 16) & 0xffU,
		(cmd->arg >> 8) & 0xffU,
		cmd->arg & 0xffU,
		0,
	};
	uint8_t rsp[SDIO_BB_MSG_BYTES];
	int bits;

	sdio_bb_setup(dev);
	sdio_bb_soft_reset_if_needed(dev, cmd);
	raw_cmd[5] = sdio_bb_crc7(raw_cmd);

	sdio_bb_idle_clocks(2);
	sdio_bb_write_cmd(raw_cmd);

	if ((cmd->response_type & SDHC_NATIVE_RESPONSE_MASK) == SD_RSP_TYPE_NONE) {
		sdio_bb_idle_clocks(1);
		return 0;
	}

	bits = sdio_bb_read_rsp(rsp);
	sdio_bb_idle_clocks(1);
	if (bits != SDIO_BB_MSG_BITS) {
		LOG_DBG("bitbang cmd%u no response", cmd->opcode);
		return -ETIMEDOUT;
	}

	cmd->response[0] = ((uint32_t)rsp[1] << 24) | ((uint32_t)rsp[2] << 16) |
			   ((uint32_t)rsp[3] << 8) | rsp[4];

	LOG_DBG("bitbang cmd%u arg=0x%08x resp=0x%08x raw=%02x %02x %02x %02x %02x %02x",
		cmd->opcode, cmd->arg, cmd->response[0],
		rsp[0], rsp[1], rsp[2], rsp[3], rsp[4], rsp[5]);

	return 0;
}

static int sdhc_bcm2835_reset(const struct device *dev)
{
	reg_write8(dev, SDHCI_RESET_ALL, SDHCI_SOFTWARE_RESET);

	return wait_for_mask8(dev, SDHCI_SOFTWARE_RESET, SDHCI_RESET_ALL, 0, 100000);
}

static int sdhc_bcm2835_set_clock(const struct device *dev, uint32_t clock)
{
	const struct sdhc_bcm2835_config *cfg = dev->config;
	struct sdhc_bcm2835_data *data = dev->data;
	uint32_t div;
	uint16_t clk = 0;
	int ret;

	reg_write16(dev, 0, SDHCI_CLOCK_CONTROL);
	data->clock = 0;

	if (clock == 0) {
		return 0;
	}

	if (cfg->input_clk <= clock) {
		div = 1;
	} else {
		for (div = 2; div < 2046; div += 2) {
			if ((cfg->input_clk / div) <= clock) {
				break;
			}
		}
	}

	data->clock = cfg->input_clk / div;
	div >>= 1;
	clk = ((div & 0xffU) << SDHCI_DIVIDER_SHIFT) |
	      (((div >> 8) & 0x3U) << SDHCI_DIVIDER_HI_SHIFT) |
	      SDHCI_CLOCK_INT_EN;
	reg_write16(dev, clk, SDHCI_CLOCK_CONTROL);

	ret = wait_for_mask16(dev, SDHCI_CLOCK_CONTROL, SDHCI_CLOCK_INT_STABLE,
			      SDHCI_CLOCK_INT_STABLE, 20000);
	if (ret) {
		return ret;
	}

	reg_write16(dev, clk | SDHCI_CLOCK_CARD_EN, SDHCI_CLOCK_CONTROL);
	return 0;
}

static int sdhc_bcm2835_set_io(const struct device *dev, struct sdhc_io *ios)
{
	const struct sdhc_bcm2835_config *cfg = dev->config;
	uint8_t ctrl;
	int ret;

	if (ios->signal_voltage && ios->signal_voltage != SD_VOL_3_3_V) {
		return -ENOTSUP;
	}

	if (ios->bus_width == SDHC_BUS_WIDTH8BIT ||
	    (ios->bus_width == SDHC_BUS_WIDTH4BIT && cfg->bus_width < 4)) {
		return -ENOTSUP;
	}

	ret = sdhc_bcm2835_set_clock(dev, ios->clock);
	if (ret) {
		return ret;
	}

	if (ios->power_mode == SDHC_POWER_OFF) {
		reg_write8(dev, 0, SDHCI_POWER_CONTROL);
		if (cfg->reset_gpio.port != NULL) {
			(void)gpio_pin_set_dt(&cfg->reset_gpio, 1);
		}
		return 0;
	}

	if (ios->power_mode == SDHC_POWER_ON) {
		if (cfg->reset_gpio.port != NULL) {
			(void)gpio_pin_set_dt(&cfg->reset_gpio, 0);
		}
		reg_write8(dev, SDHCI_POWER_330 | SDHCI_POWER_ON, SDHCI_POWER_CONTROL);
	}

	ctrl = reg_read8(dev, SDHCI_HOST_CONTROL);
	ctrl &= ~(SDHCI_CTRL_4BITBUS | SDHCI_CTRL_HISPD);
	if (ios->bus_width == SDHC_BUS_WIDTH4BIT) {
		ctrl |= SDHCI_CTRL_4BITBUS;
	}
	if (ios->timing == SDHC_TIMING_HS) {
		ctrl |= SDHCI_CTRL_HISPD;
	} else if (ios->timing != 0 && ios->timing != SDHC_TIMING_LEGACY) {
		return -ENOTSUP;
	}
	reg_write8(dev, ctrl, SDHCI_HOST_CONTROL);

	return 0;
}

static uint16_t command_flags(uint32_t response_type, bool has_data)
{
	uint8_t nrt = response_type & SDHC_NATIVE_RESPONSE_MASK;
	uint16_t flags;

	switch (nrt) {
	case SD_RSP_TYPE_NONE:
		flags = SDHCI_CMD_RESP_NONE;
		break;
	case SD_RSP_TYPE_R2:
		flags = SDHCI_CMD_RESP_LONG | SDHCI_CMD_CRC;
		break;
	case SD_RSP_TYPE_R3:
	case SD_RSP_TYPE_R4:
		flags = SDHCI_CMD_RESP_SHORT;
		break;
	case SD_RSP_TYPE_R1b:
	case SD_RSP_TYPE_R5b:
		flags = SDHCI_CMD_RESP_BUSY | SDHCI_CMD_CRC | SDHCI_CMD_INDEX;
		break;
	default:
		flags = SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX;
		break;
	}

	if (has_data) {
		flags |= SDHCI_CMD_DATA;
	}

	return flags;
}

static int transfer_data(const struct device *dev, struct sdhc_data *data, bool read)
{
	uint8_t *buf = data->data;
	uint32_t total = data->block_size * data->blocks;
	uint32_t done = 0;
	uint32_t state_mask = read ? SDHCI_DATA_AVAILABLE : SDHCI_SPACE_AVAILABLE;

	while (done < total) {
		int ret = wait_for_mask(dev, SDHCI_PRESENT_STATE, state_mask, state_mask,
					data->timeout_ms * 1000);
		if (ret) {
			return ret;
		}

		for (uint32_t i = 0; i < data->block_size && done < total; i += 4) {
			if (read) {
				uint32_t word = reg_read32(dev, SDHCI_BUFFER);

				for (uint32_t b = 0; b < 4 && done < total; b++, done++) {
					buf[done] = (word >> (8 * b)) & 0xffU;
				}
			} else {
				uint32_t word = 0;

				for (uint32_t b = 0; b < 4 && done < total; b++, done++) {
					word |= (uint32_t)buf[done] << (8 * b);
				}
				reg_write32(dev, word, SDHCI_BUFFER);
			}
		}
	}

	return 0;
}

static int sdhc_bcm2835_request(const struct device *dev, struct sdhc_command *cmd,
				struct sdhc_data *data)
{
	struct sdhc_bcm2835_data *priv = dev->data;
	uint32_t inhibit = SDHCI_CMD_INHIBIT;
	uint32_t int_status;
	uint16_t mode = 0;
	bool read = false;
	int ret;

	k_mutex_lock(&priv->lock, K_FOREVER);

	if (sdio_bb_supported_cmd(cmd, data)) {
		if (data != NULL) {
			ret = sdio_bb_request_cmd53(dev, cmd, data);
		} else {
			ret = sdio_bb_request(dev, cmd);
		}
		goto out_unlock;
	}

	if (data != NULL) {
		inhibit |= SDHCI_DATA_INHIBIT;
		if (cmd->opcode == SDIO_RW_EXTENDED) {
			read = (cmd->arg & BIT(31)) == 0;
		} else {
			read = (cmd->opcode == SD_READ_SINGLE_BLOCK ||
				cmd->opcode == SD_READ_MULTIPLE_BLOCK ||
				cmd->opcode == SD_APP_SEND_SCR ||
				cmd->opcode == SD_SWITCH);
		}
	}

	ret = wait_for_mask(dev, SDHCI_PRESENT_STATE, inhibit, 0, 10000);
	if (ret) {
		goto out;
	}

	reg_write32(dev, UINT32_MAX, SDHCI_INT_STATUS);
	reg_write32(dev, 0, SDHCI_DMA_ADDR);

	if (data != NULL) {
		reg_write16(dev, 0x7000 | data->block_size, SDHCI_BLOCK_SIZE);
		reg_write16(dev, data->blocks, SDHCI_BLOCK_COUNT);
		mode = SDHCI_TRNS_BLK_CNT_EN;
		if (data->blocks > 1) {
			mode |= SDHCI_TRNS_MULTI;
		}
		if (read) {
			mode |= SDHCI_TRNS_READ;
		}
		priv->trns_shadow = mode;
	} else {
		priv->trns_shadow = 0;
	}

	reg_write8(dev, 0x0e, SDHCI_TIMEOUT_CONTROL);
	reg_write32(dev, cmd->arg, SDHCI_ARG);
	/*
	 * BCM2835 SDHCI starts command execution on any write to the 32-bit
	 * word at SDHCI_TRANSFER_MODE/SDHCI_COMMAND (offset 0x0c).  A
	 * separate write to SDHCI_TRANSFER_MODE would re-fire the stale
	 * command from bits[31:16].  Write both fields together in one
	 * 32-bit write, as Linux and U-Boot do with a shadow register.
	 */
	reg_write32(dev,
		    ((uint32_t)((cmd->opcode << 8) |
				command_flags(cmd->response_type, data != NULL)) << 16) |
		    priv->trns_shadow,
		    SDHCI_TRANSFER_MODE);

	if ((cmd->response_type & SDHC_NATIVE_RESPONSE_MASK) == SD_RSP_TYPE_NONE) {
		k_busy_wait(1000);
		ret = 0;
		goto out;
	}

	ret = wait_for_mask(dev, SDHCI_INT_STATUS,
			    SDHCI_INT_RESPONSE | SDHCI_INT_ERROR,
			    SDHCI_INT_RESPONSE, cmd->timeout_ms * 1000);
	int_status = reg_read32(dev, SDHCI_INT_STATUS);
	if (ret || (int_status & SDHCI_INT_ERROR)) {
		printk("sdhc cmd%u failed: ret=%d int=0x%08x present=0x%08x clk=0x%04x ctl=0x%02x pwr=0x%02x cmctl=0x%08x cmdiv=0x%08x\n",
		       cmd->opcode, ret, int_status, reg_read32(dev, SDHCI_PRESENT_STATE),
		       reg_read16(dev, SDHCI_CLOCK_CONTROL), reg_read8(dev, SDHCI_HOST_CONTROL),
		       reg_read8(dev, SDHCI_POWER_CONTROL),
		       sys_read32(BCM2835_CPRMAN_BASE + CM_EMMCCTL),
		       sys_read32(BCM2835_CPRMAN_BASE + CM_EMMCDIV));
		reg_write8(dev, SDHCI_RESET_CMD | SDHCI_RESET_DATA, SDHCI_SOFTWARE_RESET);
		wait_for_mask8(dev, SDHCI_SOFTWARE_RESET,
			       SDHCI_RESET_CMD | SDHCI_RESET_DATA, 0, 100000);
		ret = ret ? ret : -EIO;
		goto out;
	}

	if ((cmd->response_type & SDHC_NATIVE_RESPONSE_MASK) == SD_RSP_TYPE_R2) {
		cmd->response[0] = reg_read32(dev, SDHCI_RESPONSE + 12) << 8;
		cmd->response[1] = reg_read32(dev, SDHCI_RESPONSE + 8);
		cmd->response[2] = reg_read32(dev, SDHCI_RESPONSE + 4);
		cmd->response[3] = reg_read32(dev, SDHCI_RESPONSE);
	} else if ((cmd->response_type & SDHC_NATIVE_RESPONSE_MASK) != SD_RSP_TYPE_NONE) {
		cmd->response[0] = reg_read32(dev, SDHCI_RESPONSE);
	}

	if (data != NULL) {
		ret = transfer_data(dev, data, read);
		if (ret) {
			goto out;
		}

		ret = wait_for_mask(dev, SDHCI_INT_STATUS,
				    SDHCI_INT_DATA_END | SDHCI_INT_ERROR,
				    SDHCI_INT_DATA_END, data->timeout_ms * 1000);
		int_status = reg_read32(dev, SDHCI_INT_STATUS);
		if (ret || (int_status & SDHCI_INT_ERROR)) {
			LOG_DBG("data failed: int_status=0x%08x", int_status);
			ret = ret ? ret : -EIO;
			goto out;
		}
		data->bytes_xfered = data->block_size * data->blocks;
	}

	ret = 0;

out:
	reg_write32(dev, UINT32_MAX, SDHCI_INT_STATUS);
out_unlock:
	k_mutex_unlock(&priv->lock);
	return ret;
}

static int sdhc_bcm2835_card_present(const struct device *dev)
{
	ARG_UNUSED(dev);

	return 1;
}

static int sdhc_bcm2835_card_busy(const struct device *dev)
{
	return (reg_read32(dev, SDHCI_PRESENT_STATE) & SDHCI_DAT0_LEVEL) == 0;
}

static int sdhc_bcm2835_get_host_props(const struct device *dev, struct sdhc_host_props *props)
{
	const struct sdhc_bcm2835_config *cfg = dev->config;

	memset(props, 0, sizeof(*props));
	props->f_max = cfg->max_freq;
	props->f_min = cfg->min_freq;
	props->power_delay = cfg->power_delay_ms;
	props->host_caps.vol_330_support = true;
	props->host_caps.high_spd_support = true;
	props->host_caps.max_blk_len = 0;
	props->bus_4_bit_support = cfg->bus_width >= 4;
	props->max_current_330 = DT_INST_PROP(0, max_current_330);
	props->is_spi = false;

	return 0;
}

static int sdhc_bcm2835_execute_tuning(const struct device *dev)
{
	ARG_UNUSED(dev);

	return -ENOTSUP;
}

static int sdhc_bcm2835_init(const struct device *dev)
{
	const struct sdhc_bcm2835_config *cfg = dev->config;
	struct sdhc_bcm2835_data *data = dev->data;
	int ret;

	k_mutex_init(&data->lock);

	sys_write32(CM_PASSWORD | (sys_read32(BCM2835_CPRMAN_BASE + CM_EMMCCTL) & ~CM_ENABLE),
		    BCM2835_CPRMAN_BASE + CM_EMMCCTL);
	for (int i = 0; i < 1000; i++) {
		if ((sys_read32(BCM2835_CPRMAN_BASE + CM_EMMCCTL) & CM_BUSY) == 0) {
			break;
		}
		k_busy_wait(10);
	}
	sys_write32(CM_PASSWORD | (2U << CM_DIV_FRAC_BITS), BCM2835_CPRMAN_BASE + CM_EMMCDIV);
	sys_write32(CM_PASSWORD | CM_SRC_PLLD_PER | CM_ENABLE | CM_GATE,
		    BCM2835_CPRMAN_BASE + CM_EMMCCTL);
	sys_write32(CM_PASSWORD | (sys_read32(BCM2835_CPRMAN_BASE + CM_GP2CTL) & ~CM_ENABLE),
		    BCM2835_CPRMAN_BASE + CM_GP2CTL);
	for (int i = 0; i < 1000; i++) {
		if ((sys_read32(BCM2835_CPRMAN_BASE + CM_GP2CTL) & CM_BUSY) == 0) {
			break;
		}
		k_busy_wait(10);
	}
	sys_write32(CM_PASSWORD | 0x249f00U, BCM2835_CPRMAN_BASE + CM_GP2DIV);
	sys_write32(CM_PASSWORD | CM_SRC_OSC | CM_ENABLE | CM_GATE,
		    BCM2835_CPRMAN_BASE + CM_GP2CTL);

	ret = pinctrl_apply_state(cfg->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret) {
		return ret;
	}

	if (cfg->reset_gpio.port != NULL) {
		if (!gpio_is_ready_dt(&cfg->reset_gpio)) {
			return -ENODEV;
		}

		ret = gpio_pin_configure_dt(&cfg->reset_gpio, GPIO_OUTPUT_INACTIVE);
		if (ret) {
			return ret;
		}
		k_sleep(K_MSEC(20));
		ret = gpio_pin_set_dt(&cfg->reset_gpio, 0);
		if (ret) {
			return ret;
		}
		k_sleep(K_MSEC(cfg->power_delay_ms));
	}

	ret = sdhc_bcm2835_reset(dev);
	if (ret) {
		return ret;
	}

	reg_write32(dev, SDHCI_INT_CARD_INT | SDHCI_INT_RESPONSE | SDHCI_INT_DATA_END |
		    SDHCI_INT_DATA_AVAIL | SDHCI_INT_SPACE_AVAIL | SDHCI_INT_ERROR |
		    SDHCI_INT_TIMEOUT | SDHCI_INT_CRC | SDHCI_INT_END_BIT |
		    SDHCI_INT_INDEX | SDHCI_INT_DATA_TIMEOUT | SDHCI_INT_DATA_CRC |
		    SDHCI_INT_DATA_END_BIT, SDHCI_INT_ENABLE);
	reg_write32(dev, 0, SDHCI_SIGNAL_ENABLE);

	return 0;
}

static DEVICE_API(sdhc, sdhc_bcm2835_api) = {
	.reset = sdhc_bcm2835_reset,
	.request = sdhc_bcm2835_request,
	.set_io = sdhc_bcm2835_set_io,
	.get_card_present = sdhc_bcm2835_card_present,
	.execute_tuning = sdhc_bcm2835_execute_tuning,
	.card_busy = sdhc_bcm2835_card_busy,
	.get_host_props = sdhc_bcm2835_get_host_props,
};

PINCTRL_DT_INST_DEFINE(0);

static const struct sdhc_bcm2835_config sdhc_bcm2835_cfg = {
	.base = DT_INST_REG_ADDR(0),
	.input_clk = DT_INST_PROP(0, clock_frequency),
	.max_freq = DT_INST_PROP(0, max_bus_freq),
	.min_freq = DT_INST_PROP(0, min_bus_freq),
	.power_delay_ms = DT_INST_PROP(0, power_delay_ms),
	.bus_width = DT_INST_PROP(0, bus_width),
	.reset_gpio = GPIO_DT_SPEC_INST_GET_OR(0, reset_gpios, {}),
	.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(0),
};

static struct sdhc_bcm2835_data sdhc_bcm2835_data;

DEVICE_DT_INST_DEFINE(0, sdhc_bcm2835_init, NULL, &sdhc_bcm2835_data,
		      &sdhc_bcm2835_cfg, POST_KERNEL, CONFIG_SDHC_INIT_PRIORITY,
		      &sdhc_bcm2835_api);
