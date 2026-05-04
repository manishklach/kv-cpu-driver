/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * kv_cpu_policy.c — Lifecycle and HEPC hardware interaction hooks
 */
#include "kv_cpu_internal.h"

/**
 * kvcpu_policy_step_advance - Signal current decode step to hardware
 * 
 * In a real KV-CPU, this write triggers the Hardware Eviction Policy 
 * Controller (HEPC) to re-evaluate the priority scores of all 
 * tracked KV blocks based on their proximity to the current step.
 */
void kvcpu_policy_step_advance(struct kvcpu_dev *kv, u64 step)
{
	/* Hot-path doorbell write */
	kvcpu_mmio_write64(kv, KVCPU_REG_STEP_ADVANCE, step);
	
	dev_dbg(kv->dev, "Policy: Signal decode step t=%llu to HEPC\n", step);
}

/**
 * kvcpu_policy_mark_hot - Boost block priority
 * 
 * Tells the hardware that the following range of KV blocks are 
 * high-value (e.g. active prefix) and should be protected from eviction.
 */
void kvcpu_policy_mark_hot(struct kvcpu_dev *kv, phys_addr_t pa, size_t len)
{
	kvcpu_mmio_write64(kv, KVCPU_REG_BOOST_ADDR, (u64)pa);
	kvcpu_mmio_write64(kv, KVCPU_REG_BOOST_LEN,  (u64)len);
	
	dev_dbg(kv->dev, "Policy: Marked PA range 0x%llx hot (protected)\n", (u64)pa);
}
