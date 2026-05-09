/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/arch/arm/cortex_a_r/sys_io.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sdhc.h>
#include <zephyr/kernel.h>
#include <zephyr/sd/sd.h>
#include <zephyr/sd/sdio.h>
#include <zephyr/sd/sd_spec.h>
#include <zephyr/sys/printk.h>

#include <stdbool.h>
#include <string.h>

#define GPIO_BASE	0x20200000U
#define GPIO_GPFSEL0	(GPIO_BASE + 0x00U)
#define GPIO_GPFSEL1	(GPIO_BASE + 0x04U)
#define GPIO_GPFSEL3	(GPIO_BASE + 0x0cU)
#define GPIO_GPFSEL4	(GPIO_BASE + 0x10U)
#define GPIO_GPSET1	(GPIO_BASE + 0x20U)
#define GPIO_GPCLR1	(GPIO_BASE + 0x2cU)
#define GPIO_GPLEV0	(GPIO_BASE + 0x34U)
#define GPIO_GPLEV1	(GPIO_BASE + 0x38U)
#define GPIO_GPPUD	(GPIO_BASE + 0x94U)
#define GPIO_GPPUDCLK1	(GPIO_BASE + 0x9cU)
#define CPRMAN_BASE	0x20101000U
#define CM_GP2CTL	(CPRMAN_BASE + 0x080U)
#define CM_GP2DIV	(CPRMAN_BASE + 0x084U)
#define CM_EMMCCTL	(CPRMAN_BASE + 0x1c0U)
#define CM_EMMCDIV	(CPRMAN_BASE + 0x1c4U)
#define SDHCI_BASE	0x20300000U
#define SDHCI_PRESENT	(SDHCI_BASE + 0x24U)
#define SDHCI_CLKCTL	(SDHCI_BASE + 0x2cU)
#define SDHCI_PWRCTL	(SDHCI_BASE + 0x29U)

/* CPRMAN password required in upper byte of every clock register write */
#define CM_PASSWD	0x5a000000U

#define GPIO_FUNC_IN	0U
#define GPIO_FUNC_OUT	1U
#define GPIO_FUNC_ALT3	7U
#define GPIO_PULL_NONE	0U
#define GPIO_PULL_UP	2U

#define SD_CLK_PIN	34U
#define SD_CMD_PIN	35U
#define SD_D0_PIN	36U
#define SD_D1_PIN	37U
#define SD_D2_PIN	38U
#define SD_D3_PIN	39U

#define SDIO_MSG_BYTES	6U
#define SDIO_MSG_BITS	48U
#define SDIO_CLK_DELAY_US 1U
#define SDIO_RSP_WAIT_CYCLES 200U
#define SDIO_CMD52_IO_ABORT 0x06U

#define BRCM_BUS_IOEN_REG		0x002U
#define BRCM_BUS_IORDY_REG		0x003U
#define BRCM_BUS_BI_CTRL_REG		0x007U
#define BRCM_BUS_SPEED_CTRL_REG	0x013U
#define BRCM_BUS_BAK_BLKSIZE_REG	0x110U
#define BRCM_BAK_WIN_ADDR_REG		0x1000aU
#define BRCM_BAK_CHIP_CLOCK_CSR_REG	0x1000eU
#define BRCM_BACKPLANE_BASE		0x18000000U
#define BRCM_SB_32BIT_WIN		0x8000U
#define BRCM_FUNC_BACKPLANE		1U
#define BRCM_BACKPLANE_BLOCK_SIZE	64U

/*
 * Route GPCLK2 to GPIO6 (header pin 31) so a logic analyzer can measure
 * the WIFI_CLK frequency on a free pin, without touching the SDIO bus
 * (GPIO34-39) or the JTAG pins (GPIO22-27).
 *
 * GPIO6 ALT0 = GPCLK2 — same clock source as GPIO43.
 * GPFSEL0 bits [20:18] control GPIO6; ALT0 = 4 (0b100).
 */
static void gpio6_enable_gpclk2(void)
{
	uint32_t fsel = sys_read32(GPIO_GPFSEL0);

	fsel &= ~(7U << 18);   /* clear GPIO6 function field */
	fsel |=  (4U << 18);   /* set ALT0 = GPCLK2 */
	sys_write32(fsel, GPIO_GPFSEL0);

	printk("GPIO6 set to ALT0 (GPCLK2): GPFSEL0=0x%08x  GPIO6_fsel=%u\n",
	       sys_read32(GPIO_GPFSEL0), (sys_read32(GPIO_GPFSEL0) >> 18) & 7U);
}

/*
 * Explicitly (re-)enable GPCLK2 from the oscillator (19.2 MHz) with
 * an integer divider of 585 → ~32.8 kHz output.  The SDHCI driver
 * sets this up at init time, but calling it here lets us verify the
 * clock is running before device_is_ready().
 *
 * Divider encoding: DIVI field [23:12], DIVF field [11:0].
 * Integer-only: DIVI=585, DIVF=0.  Source 1 = oscillator.
 */
static void gpclk2_start(void)
{
	/* stop the clock before changing source/divider */
	sys_write32(CM_PASSWD | 0x20U, CM_GP2CTL); /* ENAB=0, SRC=0 */
	k_busy_wait(10);

	/* set divider: DIVI=585 (0x249) → 19.2 MHz / 585 ≈ 32.8 kHz */
	sys_write32(CM_PASSWD | (585U << 12), CM_GP2DIV);

	/* enable: SRC=1 (oscillator), ENAB=1 */
	sys_write32(CM_PASSWD | 0x11U, CM_GP2CTL);

	printk("GPCLK2 start: GP2CTL=0x%08x GP2DIV=0x%08x\n",
	       sys_read32(CM_GP2CTL), sys_read32(CM_GP2DIV));
}

static void gpio_set_func(unsigned int pin, unsigned int func)
{
	uint32_t reg = GPIO_GPFSEL0 + ((pin / 10U) * 4U);
	uint32_t shift = (pin % 10U) * 3U;
	uint32_t val = sys_read32(reg);

	val &= ~(7U << shift);
	val |= (func & 7U) << shift;
	sys_write32(val, reg);
}

static void gpio_set_pull(unsigned int pin, unsigned int pull)
{
	sys_write32(pull, GPIO_GPPUD);
	k_busy_wait(5);
	sys_write32(BIT(pin - 32U), GPIO_GPPUDCLK1);
	k_busy_wait(5);
	sys_write32(0, GPIO_GPPUD);
	sys_write32(0, GPIO_GPPUDCLK1);
}

static void gpio_write_pin(unsigned int pin, unsigned int val)
{
	sys_write32(BIT(pin - 32U), val ? GPIO_GPSET1 : GPIO_GPCLR1);
}

static unsigned int gpio_read_pin(unsigned int pin)
{
	return (sys_read32(GPIO_GPLEV1) >> (pin - 32U)) & 1U;
}

static uint8_t crc7_byte(uint8_t data)
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

static uint8_t crc7_msg(uint8_t *data)
{
	uint8_t crc = 0;

	for (int i = 0; i < SDIO_MSG_BYTES - 1; i++) {
		crc = crc7_byte(crc ^ data[i]);
	}

	return crc | 1U;
}

static void sdio_bb_clk_cycle(void)
{
	k_busy_wait(SDIO_CLK_DELAY_US);
	gpio_write_pin(SD_CLK_PIN, 1);
	k_busy_wait(SDIO_CLK_DELAY_US);
	gpio_write_pin(SD_CLK_PIN, 0);
}

static void sdio_bb_idle_clocks(unsigned int cycles)
{
	for (unsigned int i = 0; i < cycles; i++) {
		sdio_bb_clk_cycle();
	}
}

static void sdio_bb_setup(void)
{
	gpio_set_func(SD_CLK_PIN, GPIO_FUNC_OUT);
	gpio_set_pull(SD_CLK_PIN, GPIO_PULL_NONE);
	gpio_write_pin(SD_CLK_PIN, 0);

	gpio_set_func(SD_CMD_PIN, GPIO_FUNC_IN);
	gpio_set_func(SD_D0_PIN, GPIO_FUNC_IN);
	gpio_set_func(SD_D1_PIN, GPIO_FUNC_IN);
	gpio_set_func(SD_D2_PIN, GPIO_FUNC_IN);
	gpio_set_func(SD_D3_PIN, GPIO_FUNC_IN);

	gpio_set_pull(SD_CMD_PIN, GPIO_PULL_UP);
	gpio_set_pull(SD_D0_PIN, GPIO_PULL_UP);
	gpio_set_pull(SD_D1_PIN, GPIO_PULL_UP);
	gpio_set_pull(SD_D2_PIN, GPIO_PULL_UP);
	gpio_set_pull(SD_D3_PIN, GPIO_PULL_UP);

	printk("[bb] GPIO34-39 raw mode: GPFSEL3=0x%08x GPLEV1=0x%08x\n",
	       sys_read32(GPIO_GPFSEL3), sys_read32(GPIO_GPLEV1));
}

static void sdio_bb_restore_alt3(void)
{
	for (unsigned int pin = SD_CLK_PIN; pin <= SD_D3_PIN; pin++) {
		gpio_set_func(pin, GPIO_FUNC_ALT3);
	}
	for (unsigned int pin = SD_CMD_PIN; pin <= SD_D3_PIN; pin++) {
		gpio_set_pull(pin, GPIO_PULL_UP);
	}
	printk("[bb] GPIO34-39 restored ALT3: GPFSEL3=0x%08x\n", sys_read32(GPIO_GPFSEL3));
}

static void sdio_bb_write_cmd(uint8_t *cmd)
{
	gpio_set_func(SD_CMD_PIN, GPIO_FUNC_OUT);

	for (int bit = 0; bit < SDIO_MSG_BITS; bit++) {
		unsigned int byte = bit / 8;
		unsigned int mask = BIT(7 - (bit % 8));

		gpio_write_pin(SD_CMD_PIN, (cmd[byte] & mask) != 0);
		sdio_bb_clk_cycle();
	}

	gpio_set_func(SD_CMD_PIN, GPIO_FUNC_IN);
	gpio_set_pull(SD_CMD_PIN, GPIO_PULL_UP);
}

static int sdio_bb_read_rsp(uint8_t *rsp)
{
	memset(rsp, 0, SDIO_MSG_BYTES);

	for (int wait = 0; wait < SDIO_RSP_WAIT_CYCLES; wait++) {
		k_busy_wait(SDIO_CLK_DELAY_US);
		gpio_write_pin(SD_CLK_PIN, 1);
		if (gpio_read_pin(SD_CMD_PIN) == 0) {
			k_busy_wait(SDIO_CLK_DELAY_US);
			gpio_write_pin(SD_CLK_PIN, 0);
			goto got_start;
		}
		k_busy_wait(SDIO_CLK_DELAY_US);
		gpio_write_pin(SD_CLK_PIN, 0);
	}

	return 0;

got_start:
	for (int bit = 1; bit < SDIO_MSG_BITS; bit++) {
		unsigned int byte = bit / 8;

		k_busy_wait(SDIO_CLK_DELAY_US);
		gpio_write_pin(SD_CLK_PIN, 1);
		rsp[byte] = (rsp[byte] << 1) | gpio_read_pin(SD_CMD_PIN);
		k_busy_wait(SDIO_CLK_DELAY_US);
		gpio_write_pin(SD_CLK_PIN, 0);
	}

	return SDIO_MSG_BITS;
}

static int sdio_bb_cmd(unsigned int opcode, uint32_t arg, uint8_t *rsp)
{
	uint8_t cmd[SDIO_MSG_BYTES] = {
		0x40U | (opcode & 0x3fU),
		(arg >> 24) & 0xffU,
		(arg >> 16) & 0xffU,
		(arg >> 8) & 0xffU,
		arg & 0xffU,
		0,
	};
	int bits;

	cmd[5] = crc7_msg(cmd);
	sdio_bb_idle_clocks(2);
	sdio_bb_write_cmd(cmd);
	bits = sdio_bb_read_rsp(rsp);
	sdio_bb_idle_clocks(1);

	printk("[bb] CMD%u arg=0x%08x rsp_bits=%d rsp=%02x %02x %02x %02x %02x %02x\n",
	       opcode, arg, bits, rsp[0], rsp[1], rsp[2], rsp[3], rsp[4], rsp[5]);

	return bits;
}

static uint32_t sdio_cmd52_arg(unsigned int func, unsigned int addr, uint8_t data, bool write)
{
	return (write ? BIT(31) : 0U) | ((func & 7U) << 28) |
	       ((addr & 0x1ffffU) << 9) | data;
}

static void sdio_bb_probe(void)
{
	uint8_t rsp[SDIO_MSG_BYTES];
	uint32_t rca;

	printk("--- bit-banged SDIO CMD5 probe (zerowi-style GPIO mode) ---\n");
	sdio_bb_setup();
	sdio_bb_idle_clocks(16);
	sdio_bb_cmd(52, sdio_cmd52_arg(0, SDIO_CMD52_IO_ABORT, 0, false), rsp);
	k_sleep(K_MSEC(20));
	sdio_bb_cmd(52, sdio_cmd52_arg(0, SDIO_CMD52_IO_ABORT, 8, true), rsp);
	k_sleep(K_MSEC(20));
	sdio_bb_cmd(0, 0, rsp);
	sdio_bb_cmd(8, 0x1aa, rsp);
	sdio_bb_cmd(5, 0, rsp);
	sdio_bb_cmd(5, 0x00200000U, rsp);
	if (sdio_bb_cmd(3, 0, rsp) == SDIO_MSG_BITS) {
		rca = ((uint32_t)rsp[1] << 24) | ((uint32_t)rsp[2] << 16);
		printk("[bb] CMD3 selected RCA arg=0x%08x\n", rca);
		if (sdio_bb_cmd(7, rca, rsp) == SDIO_MSG_BITS) {
			sdio_bb_cmd(52, sdio_cmd52_arg(0, 0x00, 0, false), rsp);
			printk("[bb] CMD52 CCCR/SDIO rev data=0x%02x\n", rsp[4]);
			sdio_bb_cmd(52, sdio_cmd52_arg(0, 0x08, 0, false), rsp);
			printk("[bb] CMD52 card capability data=0x%02x\n", rsp[4]);
		}
	}
}

static void dump_hw_state(const char *tag)
{
	uint32_t gpfsel0 = sys_read32(GPIO_GPFSEL0);
	uint32_t gpfsel3 = sys_read32(GPIO_GPFSEL3);
	uint32_t gpfsel4 = sys_read32(GPIO_GPFSEL4);
	uint32_t gplev0  = sys_read32(GPIO_GPLEV0);
	uint32_t gplev1  = sys_read32(GPIO_GPLEV1);
	uint32_t gp2ctl  = sys_read32(CM_GP2CTL);
	uint32_t gp2div  = sys_read32(CM_GP2DIV);
	uint32_t emmcctl = sys_read32(CM_EMMCCTL);
	uint32_t emmcdiv = sys_read32(CM_EMMCDIV);
	uint32_t present = sys_read32(SDHCI_PRESENT);
	/* read 32-bit word containing CLKCTL(16-bit) and TIMEOUT+SWRST(8-bit) */
	uint32_t clkpwr  = sys_read32(SDHCI_BASE + 0x2cU);

	printk("[%s] GPFSEL0=0x%08x  GPIO6_fsel=%u (4=ALT0=GPCLK2)\n",
	       tag, gpfsel0, (gpfsel0 >> 18) & 7U);
	printk("[%s] GPFSEL3=0x%08x GPFSEL4=0x%08x\n", tag, gpfsel3, gpfsel4);
	printk("[%s] GPLEV0=0x%08x GPLEV1=0x%08x\n", tag, gplev0, gplev1);
	printk("[%s] GPIO6(GPCLK2_probe)=%u GPIO41(WL_ON)=%u GPIO43(GPCLK2_pin)=%u\n",
	       tag,
	       (gplev0 >> 6) & 1U,
	       (gplev1 >> (41 - 32)) & 1U,
	       (gplev1 >> (43 - 32)) & 1U);
	printk("[%s] GPFSEL3[GPIO34-39]=%u%u%u%u%u%u (expect 7=ALT3)\n",
	       tag,
	       (gpfsel3 >> 12) & 7U, (gpfsel3 >> 15) & 7U,
	       (gpfsel3 >> 18) & 7U, (gpfsel3 >> 21) & 7U,
	       (gpfsel3 >> 24) & 7U, (gpfsel3 >> 27) & 7U);
	printk("[%s] GPFSEL4[GPIO43]=%u (expect 4=ALT0)\n",
	       tag, (gpfsel4 >> 9) & 7U);
	printk("[%s] GP2CTL=0x%08x GP2DIV=0x%08x\n", tag, gp2ctl, gp2div);
	printk("[%s] EMMCCTL=0x%08x EMMCDIV=0x%08x\n", tag, emmcctl, emmcdiv);
	printk("[%s] SDHCI present=0x%08x clkpwr32=0x%08x\n",
	       tag, present, clkpwr);
}

static struct sd_card card;
static struct sdio_func func1;

static int read_func1_cis_ptr(uint32_t *cis_ptr)
{
	uint8_t b0;
	uint8_t b1;
	uint8_t b2;
	int ret;

	ret = sdio_read_byte(&card.func0, 0x109, &b0);
	if (ret) {
		return ret;
	}
	ret = sdio_read_byte(&card.func0, 0x10a, &b1);
	if (ret) {
		return ret;
	}
	ret = sdio_read_byte(&card.func0, 0x10b, &b2);
	if (ret) {
		return ret;
	}

	*cis_ptr = (uint32_t)b0 | ((uint32_t)b1 << 8) | ((uint32_t)b2 << 16);
	return 0;
}

static void dump_func0_bytes(uint32_t addr, size_t len)
{
	uint8_t val;
	int ret;

	printk("func0 bytes @0x%06x:", addr);
	for (size_t i = 0; i < len; i++) {
		ret = sdio_read_byte(&card.func0, addr + i, &val);
		if (ret) {
			printk(" <err %d at +%u>", ret, (unsigned int)i);
			break;
		}
		printk(" %02x", val);
	}
	printk("\n");
}

static int brcm_write_func0(uint32_t reg, uint8_t val)
{
	int ret = sdio_write_byte(&card.func0, reg, val);

	if (ret) {
		printk("func0 write 0x%05x=0x%02x failed: %d\n", reg, val, ret);
	}
	return ret;
}

static int brcm_write_func1(uint32_t reg, uint8_t val)
{
	int ret = sdio_write_byte(&func1, reg, val);

	if (ret) {
		printk("func1 write 0x%05x=0x%02x failed: %d\n", reg, val, ret);
	}
	return ret;
}

static int brcm_backplane_window(uint32_t addr)
{
	addr &= ~0x7fffU;

	if (brcm_write_func1(BRCM_BAK_WIN_ADDR_REG, (addr >> 8) & 0xffU)) {
		return -EIO;
	}
	if (brcm_write_func1(BRCM_BAK_WIN_ADDR_REG + 1, (addr >> 16) & 0xffU)) {
		return -EIO;
	}
	if (brcm_write_func1(BRCM_BAK_WIN_ADDR_REG + 2, (addr >> 24) & 0xffU)) {
		return -EIO;
	}

	return 0;
}

static int brcm_probe_backplane(void)
{
	uint8_t ready = 0;
	uint8_t ioen = 0;
	uint8_t chipid[4] = { 0 };
	enum sdio_func_num backplane_func_num = SDIO_FUNC_NUM_1;
	int ret;

	func1.num = SDIO_FUNC_NUM_1;
	func1.card = &card;
	func1.cis.max_blk_size = BRCM_BACKPLANE_BLOCK_SIZE;
	func1.block_size = BRCM_BACKPLANE_BLOCK_SIZE;

	ret = brcm_write_func0(BRCM_BUS_SPEED_CTRL_REG, 0x03);
	if (ret) {
		return ret;
	}
	ret = brcm_write_func0(BRCM_BUS_BI_CTRL_REG, 0x42);
	if (ret) {
		return ret;
	}
	ret = brcm_write_func0(BRCM_BUS_BAK_BLKSIZE_REG, BRCM_BACKPLANE_BLOCK_SIZE);
	if (ret) {
		return ret;
	}
	ret = brcm_write_func0(BRCM_BUS_BAK_BLKSIZE_REG + 1, 0);
	if (ret) {
		return ret;
	}
	ret = brcm_write_func0(BRCM_BUS_IOEN_REG, BIT(BRCM_FUNC_BACKPLANE) | BIT(2));
	if (ret) {
		return ret;
	}
	ret = sdio_read_byte(&card.func0, BRCM_BUS_IOEN_REG, &ioen);
	if (ret) {
		return ret;
	}

	for (int i = 0; i < 100; i++) {
		ret = sdio_read_byte(&card.func0, BRCM_BUS_IORDY_REG, &ready);
		if (ret) {
			return ret;
		}
		if (ready & BIT(BRCM_FUNC_BACKPLANE)) {
			break;
		}
		k_sleep(K_MSEC(1));
	}
	printk("backplane IOEN=0x%02x IORDY=0x%02x\n", ioen, ready);
	if ((ready & BIT(BRCM_FUNC_BACKPLANE)) != 0) {
		backplane_func_num = SDIO_FUNC_NUM_1;
	} else if ((ready & BIT(2)) != 0) {
		backplane_func_num = SDIO_FUNC_NUM_2;
		printk("using SDIO function 2 for backplane probe because function 1 is not ready\n");
	} else {
		return -ETIMEDOUT;
	}
	func1.num = backplane_func_num;

	ret = brcm_backplane_window(BRCM_BACKPLANE_BASE);
	if (ret) {
		return ret;
	}

	ret = sdio_read_addr(&func1, BRCM_SB_32BIT_WIN, chipid, sizeof(chipid));
	if (ret) {
		printk("backplane chip-id CMD53 read failed: %d\n", ret);
		return ret;
	}
	printk("backplane chip-id raw: %02x %02x %02x %02x\n",
	       chipid[0], chipid[1], chipid[2], chipid[3]);

	ret = brcm_write_func1(BRCM_BAK_CHIP_CLOCK_CSR_REG, 0x28);
	if (ret) {
		return ret;
	}
	ret = sdio_read_byte(&func1, BRCM_BAK_CHIP_CLOCK_CSR_REG, &ready);
	if (ret) {
		return ret;
	}
	printk("backplane clock csr after request=0x%02x\n", ready);

	return 0;
}

int main(void)
{
	const struct device *sdhc = DEVICE_DT_GET(DT_ALIAS(sdhc0));
	int ret;

	printk("=== Raspberry Pi Zero W SDIO / GPCLK2 probe ===\n");

	/*
	 * Route GPCLK2 to GPIO6 (header pin 31) before any driver init.
	 * This lets the logic analyzer measure WIFI_CLK frequency on a free
	 * pin.  The SDHCI driver will start GPCLK2 at device_is_ready time;
	 * we also call gpclk2_start() here so the pin is active even if
	 * the driver init is not reached.
	 */
	gpclk2_start();
	gpio6_enable_gpclk2();

	dump_hw_state("pre-init");

	if (!device_is_ready(sdhc)) {
		printk("SDHC device is not ready\n");
		/* keep running so we can still probe clock on GPIO6 */
		dump_hw_state("no-device");
		while (1) {
			k_sleep(K_SECONDS(5));
			printk("GPCLK2 GP2CTL=0x%08x GP2DIV=0x%08x\n",
			       sys_read32(CM_GP2CTL), sys_read32(CM_GP2DIV));
		}
	}

	dump_hw_state("post-init");

	dump_hw_state("pre-sd_init");

	ret = sd_init(sdhc, &card);
	if (ret) {
		printk("sd_init failed: %d\n", ret);
		dump_hw_state("after-sd_init-fail");
		printk("SDIO probe stopping after finite comparison run\n");
		return 0;
	}

	printk("card type=%u io_functions=%u ocr=0x%08x cccr=0x%08x\n",
	       card.type, card.num_io, card.ocr, card.cccr_flags);
	printk("func0 manf=0x%04x code=0x%04x func=0x%02x max_blk=%u max_speed=0x%02x\n",
	       card.func0.cis.manf_id, card.func0.cis.manf_code,
	       card.func0.cis.func_id, card.func0.cis.max_blk_size,
	       card.func0.cis.max_speed);

	if (card.num_io >= 1) {
		uint32_t func1_cis_ptr;

		ret = read_func1_cis_ptr(&func1_cis_ptr);
		if (ret) {
			printk("func1 CIS pointer read failed: %d\n", ret);
			return 0;
		}
		printk("func1 CIS pointer=0x%06x\n", func1_cis_ptr);
		dump_func0_bytes(func1_cis_ptr, 16);
		if (func1_cis_ptr == 0) {
			printk("func1 CIS pointer is zero; skipping sdio_init_func(1)\n");
			return 0;
		}

		ret = sdio_init_func(&card, &func1, SDIO_FUNC_NUM_1);
		if (ret) {
			printk("sdio_init_func(1) failed: %d\n", ret);
		} else {
			printk("func1 manf=0x%04x code=0x%04x func=0x%02x max_blk=%u ready_timeout=%u\n",
			       func1.cis.manf_id, func1.cis.manf_code, func1.cis.func_id,
			       func1.cis.max_blk_size, func1.cis.rdy_timeout);
		}

		ret = brcm_probe_backplane();
		printk("brcm_probe_backplane() returned %d\n", ret);
	}

	printk("SDIO probe complete\n");
	return 0;
}
