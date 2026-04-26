/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal VideoCore property mailbox for BCM2835 SDHCI bring-up. Layout and
 * tag values follow the Raspberry Pi firmware mailbox protocol and U-Boot
 * arch/arm/mach-bcm283x/msg.c.
 */

#include "bcm2835_mbox.h"

#include <errno.h>

#include <zephyr/cache.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>
#include <zephyr/toolchain.h>
#include <string.h>

#define BCM2835_MBOX_BASE         0x2000B880UL
#define MBOX_READ_OFF             0x00U
#define MBOX_MAIL0_STATUS_OFF     0x18U
#define MBOX_WRITE_OFF            0x20U
#define MBOX_MAIL1_STATUS_OFF     0x38U
#define BCM2835_MBOX_PROP_CHAN    8U

#define MBOX_STATUS_WR_FULL  BIT(31)
#define MBOX_STATUS_RD_EMPTY BIT(30)

#define MBOX_CHAN_MASK 0xfU
#define MBOX_PACK(chan, addr)   (((addr) & ~MBOX_CHAN_MASK) | ((chan) & MBOX_CHAN_MASK))
#define MBOX_UNPACK_CHAN(val)   ((val) & MBOX_CHAN_MASK)
#define MBOX_UNPACK_ADDR(val)   ((val) & ~MBOX_CHAN_MASK)

#define MBOX_RESP_SUCCESS 0x80000000U
#define MBOX_TAG_VAL_RESP BIT(31)

#define BCM2835_PHYS_TO_BUS(pa) (0x40000000U | (uint32_t)(uintptr_t)(pa))

#define TAG_SET_POWER_STATE    0x00028001U
#define TAG_GET_CLOCK_RATE     0x00030002U
#define TAG_GET_MAX_CLOCK_RATE 0x00030004U

#define POWER_DEV_SDHCI 0U
#define POWER_REQ_ON    BIT(0)
#define POWER_REQ_WAIT  BIT(1)

struct mbox_hdr {
	uint32_t buf_size;
	uint32_t code;
};

struct mbox_tag_hdr {
	uint32_t tag;
	uint32_t val_buf_size;
	uint32_t val_len;
};

struct mbox_msg_power {
	struct mbox_hdr hdr;
	struct {
		struct mbox_tag_hdr tag_hdr;
		union {
			struct {
				uint32_t device_id;
				uint32_t state;
			} req;
			struct {
				uint32_t device_id;
				uint32_t state;
			} resp;
		} body;
	} set_power;
	uint32_t end_tag;
};

struct mbox_msg_clock {
	struct mbox_hdr hdr;
	struct {
		struct mbox_tag_hdr tag_hdr;
		union {
			struct {
				uint32_t clock_id;
			} req;
			struct {
				uint32_t clock_id;
				uint32_t rate_hz;
			} resp;
		} body;
	} get_clock;
	uint32_t end_tag;
};

static int mbox_call_prop(struct mbox_hdr *buf, size_t buf_size)
{
	const uintptr_t base = BCM2835_MBOX_BASE;
	const int64_t deadline = k_uptime_get() + 1000;

	sys_cache_data_flush_range(buf, buf_size);

	const uint32_t bus_addr = BCM2835_PHYS_TO_BUS(buf);

	/* Drain stale replies (mail0 read FIFO) */
	while ((sys_read32(base + MBOX_MAIL0_STATUS_OFF) & MBOX_STATUS_RD_EMPTY) == 0U) {
		if (k_uptime_get() > deadline) {
			return -ETIMEDOUT;
		}
		(void)sys_read32(base + MBOX_READ_OFF);
	}

	/* Wait until ARM -> VC queue accepts a message */
	while ((sys_read32(base + MBOX_MAIL1_STATUS_OFF) & MBOX_STATUS_WR_FULL) != 0U) {
		if (k_uptime_get() > deadline) {
			return -ETIMEDOUT;
		}
	}

	sys_write32(MBOX_PACK(BCM2835_MBOX_PROP_CHAN, bus_addr), base + MBOX_WRITE_OFF);

	while ((sys_read32(base + MBOX_MAIL0_STATUS_OFF) & MBOX_STATUS_RD_EMPTY) != 0U) {
		if (k_uptime_get() > deadline) {
			return -ETIMEDOUT;
		}
	}

	const uint32_t val = sys_read32(base + MBOX_READ_OFF);

	if (MBOX_UNPACK_CHAN(val) != BCM2835_MBOX_PROP_CHAN) {
		return -EIO;
	}
	if (MBOX_UNPACK_ADDR(val) != bus_addr) {
		return -EIO;
	}

	sys_cache_data_invd_range(buf, buf_size);

	if (buf->code != MBOX_RESP_SUCCESS) {
		return -EIO;
	}

	return 0;
}

static int mbox_set_power_sdhci_on(void)
{
	struct mbox_msg_power msg __aligned(16);

	memset(&msg, 0, sizeof(msg));
	msg.hdr.buf_size = sizeof(msg);
	msg.hdr.code = 0U;
	msg.set_power.tag_hdr.tag = TAG_SET_POWER_STATE;
	msg.set_power.tag_hdr.val_buf_size = sizeof(msg.set_power.body);
	msg.set_power.tag_hdr.val_len = sizeof(msg.set_power.body.req);
	msg.set_power.body.req.device_id = POWER_DEV_SDHCI;
	msg.set_power.body.req.state = POWER_REQ_ON | POWER_REQ_WAIT;
	msg.end_tag = 0U;

	int err = mbox_call_prop(&msg.hdr, sizeof(msg));

	if (err != 0) {
		return err;
	}
	if ((msg.set_power.tag_hdr.val_len & MBOX_TAG_VAL_RESP) == 0U) {
		return -EIO;
	}
	msg.set_power.tag_hdr.val_len &= ~MBOX_TAG_VAL_RESP;
	return 0;
}

static int mbox_get_clock_rate(uint32_t tag, uint32_t clock_id, uint32_t *rate_out)
{
	struct mbox_msg_clock msg __aligned(16);

	memset(&msg, 0, sizeof(msg));
	msg.hdr.buf_size = sizeof(msg);
	msg.hdr.code = 0U;
	msg.get_clock.tag_hdr.tag = tag;
	msg.get_clock.tag_hdr.val_buf_size = sizeof(msg.get_clock.body);
	msg.get_clock.tag_hdr.val_len = sizeof(msg.get_clock.body.req);
	msg.get_clock.body.req.clock_id = clock_id;
	msg.end_tag = 0U;

	int err = mbox_call_prop(&msg.hdr, sizeof(msg));

	if (err != 0) {
		return err;
	}
	if ((msg.get_clock.tag_hdr.val_len & MBOX_TAG_VAL_RESP) == 0U) {
		return -EIO;
	}
	msg.get_clock.tag_hdr.val_len &= ~MBOX_TAG_VAL_RESP;
	*rate_out = msg.get_clock.body.resp.rate_hz;
	return 0;
}

int bcm2835_mbox_mmc_prepare(uint32_t clock_id, uint32_t *rate_hz_out)
{
	uint32_t rate = 0U;
	int err;

	err = mbox_set_power_sdhci_on();
	if (err != 0) {
		return err;
	}

	err = mbox_get_clock_rate(TAG_GET_CLOCK_RATE, clock_id, &rate);
	if (err != 0) {
		return err;
	}
	if (rate == 0U) {
		err = mbox_get_clock_rate(TAG_GET_MAX_CLOCK_RATE, clock_id, &rate);
		if (err != 0) {
			return err;
		}
	}

	if (rate_hz_out != NULL) {
		*rate_hz_out = rate;
	}
	return 0;
}
