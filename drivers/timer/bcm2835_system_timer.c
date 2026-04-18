/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT brcm_bcm2835_system_timer

#include <zephyr/drivers/timer/system_timer.h>
#include <zephyr/init.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys_clock.h>
#include <zephyr/sys/math_extras.h>
#include <zephyr/sys/sys_io.h>

#define BCM2835_STIMER_BASE	DT_INST_REG_ADDR(0)
#define BCM2835_STIMER_IRQN	DT_INST_IRQN(0)
#define BCM2835_STIMER_CS	(BCM2835_STIMER_BASE + 0x00)
#define BCM2835_STIMER_CLO	(BCM2835_STIMER_BASE + 0x04)
#define BCM2835_STIMER_CHI	(BCM2835_STIMER_BASE + 0x08)
#define BCM2835_STIMER_C0	(BCM2835_STIMER_BASE + 0x0c)
#define BCM2835_STIMER_C1	(BCM2835_STIMER_BASE + 0x10)
#define BCM2835_STIMER_C2	(BCM2835_STIMER_BASE + 0x14)
#define BCM2835_STIMER_C3	(BCM2835_STIMER_BASE + 0x18)
#define BCM2835_STIMER_MATCH3	BIT(3)

#if defined(CONFIG_TEST)
const int32_t z_sys_timer_irq_for_test = BCM2835_STIMER_IRQN;
#endif

static struct k_spinlock lock;
static uint32_t cycles_per_tick;
static uint32_t last_cycle;

static void bcm2835_system_timer_isr(const void *arg)
{
	ARG_UNUSED(arg);

	uint32_t now;
	uint32_t delta_cycles;
	uint32_t delta_ticks;
	k_spinlock_key_t key = k_spin_lock(&lock);

	sys_write32(BCM2835_STIMER_MATCH3, BCM2835_STIMER_CS);

	now = sys_read32(BCM2835_STIMER_CLO);
	delta_cycles = now - last_cycle;
	delta_ticks = delta_cycles / cycles_per_tick;
	if (delta_ticks == 0U) {
		delta_ticks = 1U;
	}

	last_cycle += delta_ticks * cycles_per_tick;
	sys_write32(last_cycle + cycles_per_tick, BCM2835_STIMER_C3);

	k_spin_unlock(&lock, key);

	sys_clock_announce(delta_ticks);
}

void sys_clock_set_timeout(int32_t ticks, bool idle)
{
	ARG_UNUSED(ticks);
	ARG_UNUSED(idle);
}

uint32_t sys_clock_elapsed(void)
{
	return 0U;
}

uint32_t sys_clock_cycle_get_32(void)
{
	return sys_read32(BCM2835_STIMER_CLO);
}

#ifdef CONFIG_TIMER_HAS_64BIT_CYCLE_COUNTER
uint64_t sys_clock_cycle_get_64(void)
{
	uint32_t hi0;
	uint32_t lo;
	uint32_t hi1;

	do {
		hi0 = sys_read32(BCM2835_STIMER_CHI);
		lo = sys_read32(BCM2835_STIMER_CLO);
		hi1 = sys_read32(BCM2835_STIMER_CHI);
	} while (hi0 != hi1);

	return ((uint64_t)hi1 << 32) | lo;
}
#endif

static int sys_clock_driver_init(void)
{
	cycles_per_tick = DT_INST_PROP(0, clock_frequency) / CONFIG_SYS_CLOCK_TICKS_PER_SEC;
	last_cycle = sys_read32(BCM2835_STIMER_CLO);

	IRQ_CONNECT(BCM2835_STIMER_IRQN, 0, bcm2835_system_timer_isr, NULL, 0);
	sys_write32(BCM2835_STIMER_MATCH3, BCM2835_STIMER_CS);
	sys_write32(last_cycle + cycles_per_tick, BCM2835_STIMER_C3);
	irq_enable(BCM2835_STIMER_IRQN);

	return 0;
}

SYS_INIT(sys_clock_driver_init, PRE_KERNEL_2, CONFIG_SYSTEM_CLOCK_INIT_PRIORITY);
