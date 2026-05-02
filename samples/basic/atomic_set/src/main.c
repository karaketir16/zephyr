/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/kernel/mm.h>
#include <zephyr/kernel/internal/mm.h>
#include <zephyr/sys/atomic.h>

#define BUILD_MARKER 13
#define PROBE_VALUE 42

static atomic_t low_test_value = ATOMIC_INIT(1);
static uint8_t __aligned(CONFIG_MMU_PAGE_SIZE) backing_page[CONFIG_MMU_PAGE_SIZE];

struct strex_probe_result {
	atomic_val_t old;
	unsigned int status;
	atomic_val_t current;
};

extern void atomic_strex_probe(atomic_t *target, atomic_val_t value,
			       struct strex_probe_result *out);
extern int arch_page_phys_get(void *virt, uintptr_t *phys);

static void dump_target_info(const char *label, atomic_t *target)
{
	uintptr_t page_base = (uintptr_t)target & ~(CONFIG_MMU_PAGE_SIZE - 1U);
	uintptr_t phys = 0;
	int rc;

	rc = arch_page_phys_get((void *)page_base, &phys);

	printf("%s info addr=%p page=%p offset=0x%lx page_phys_rc=%d page_phys=0x%08lx value=%ld\n",
	       label, target, (void *)page_base,
	       (unsigned long)((uintptr_t)target - page_base),
	       rc, (unsigned long)phys, (long)atomic_get(target));
}

static void run_probe_test(const char *label, atomic_t *target, bool lock_irqs)
{
	struct strex_probe_result result = {0};
	unsigned int key = 0;
	atomic_val_t before_reset;
	atomic_val_t after_reset;
	atomic_val_t after_probe;

	before_reset = atomic_get(target);
	atomic_set(target, 1);
	after_reset = atomic_get(target);

	if (lock_irqs) {
		key = arch_irq_lock();
	}

	atomic_strex_probe(target, PROBE_VALUE, &result);

	if (lock_irqs) {
		arch_irq_unlock(key);
	}

	after_probe = atomic_get(target);

	printf("%s addr=%p before_reset=%ld after_reset=%ld old=%ld "
	       "status=%u current=%ld after_probe=%ld\n",
	       label, target, (long)before_reset, (long)after_reset,
	       (long)result.old, result.status, (long)result.current,
	       (long)after_probe);
}

static void run_target_pair(const char *label, atomic_t *target)
{
	char info_label[48];
	char irq_on_label[48];
	char irq_off_label[48];

	snprintk(info_label, sizeof(info_label), "%s", label);
	snprintk(irq_on_label, sizeof(irq_on_label), "%s irq-on ", label);
	snprintk(irq_off_label, sizeof(irq_off_label), "%s irq-off", label);

	dump_target_info(info_label, target);
	run_probe_test(irq_on_label, target, false);
	run_probe_test(irq_off_label, target, true);
}

static void update_page_and_run(const char *label, atomic_t *target)
{
	uintptr_t page_base = (uintptr_t)target & ~(CONFIG_MMU_PAGE_SIZE - 1U);
	int rc;

	rc = k_mem_update_flags((void *)page_base, CONFIG_MMU_PAGE_SIZE,
				K_MEM_CACHE_WB | K_MEM_PERM_RW);
	printf("%s update_flags rc=%d page=%p\n", label, rc, (void *)page_base);

	run_target_pair(label, target);
}

int main(void)
{
	void *anon_page = NULL;
	atomic_t *direct_page_atomic = (atomic_t *)&backing_page[0];
	atomic_t *anon_atomic;

	printf("atomic_set sample build=%d page_size=%u free_mem=%zu\n",
	       BUILD_MARKER, CONFIG_MMU_PAGE_SIZE, k_mem_free_get());

	printf("low_test_value permanent_phys=0x%08lx\n",
	       (unsigned long)k_mem_phys_addr(&low_test_value));

	printf("backing_page base=%p permanent_phys=0x%08lx\n",
	       backing_page, (unsigned long)k_mem_phys_addr(backing_page));

	run_target_pair("low-static", &low_test_value);
	update_page_and_run("low-static-upd", &low_test_value);

	*direct_page_atomic = 1;
	run_target_pair("direct-page", direct_page_atomic);
	update_page_and_run("direct-page-upd", direct_page_atomic);

#if defined(CONFIG_ARMV6_ARM1176)
	printf("mapped-alias skipped on ARM1176: cacheable RAM alias probe is diagnostic only\n");
#else
	uint8_t *mapped_alias = NULL;
	atomic_t *mapped_alias_atomic;
	uintptr_t mapped_alias_phys = 0;
	int mapped_alias_phys_rc;

	k_mem_map_phys_bare(&mapped_alias, k_mem_phys_addr(backing_page),
			    CONFIG_MMU_PAGE_SIZE, K_MEM_CACHE_WB | K_MEM_PERM_RW);
	mapped_alias_atomic = (atomic_t *)mapped_alias;
	printf("mapped-alias base=%p\n", mapped_alias);
	mapped_alias_phys_rc = arch_page_phys_get(mapped_alias, &mapped_alias_phys);
	printf("mapped-alias pre info addr=%p page=%p offset=0x0 page_phys_rc=%d page_phys=0x%08lx\n",
	       mapped_alias, mapped_alias, mapped_alias_phys_rc,
	       (unsigned long)mapped_alias_phys);
	*mapped_alias_atomic = 1;
	run_target_pair("mapped-alias", mapped_alias_atomic);
	k_mem_unmap_phys_bare(mapped_alias, CONFIG_MMU_PAGE_SIZE);
#endif

	anon_page = k_mem_map(CONFIG_MMU_PAGE_SIZE, K_MEM_PERM_RW);
	anon_atomic = (atomic_t *)anon_page;
	printf("anon-page base=%p\n", anon_page);
	*anon_atomic = 1;
	run_target_pair("anon-page  ", anon_atomic);
	k_mem_unmap(anon_page, CONFIG_MMU_PAGE_SIZE);

	printf("atomic_set sample done free_mem=%zu\n", k_mem_free_get());

	return 0;
}
