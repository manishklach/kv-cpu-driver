/* SPDX-License-Identifier: GPL-2.0-only */
#include <linux/io.h>
#include "kv_cpu_internal.h"

/* 
 * kv_cpu_write_reg — Generic register write helper
 * In mock mode, this writes to a kmalloc-ed buffer.
 * In hardware mode, it writes to the PCI BAR.
 */
void kv_cpu_write_reg(struct kvcpu_dev *kv, u32 offset, u64 val)
{
	if (unlikely(kv->is_mock)) {
		u64 *reg = (u64 *)(kv->mock_bar + offset);
		*reg = val;
		return;
	}
	writeq(val, kv->bar0 + offset);
}

void kv_cpu_cmd_step(struct kvcpu_dev *kv, u64 step)
{
	kv_cpu_write_reg(kv, KVCPU_REG_STEP_ADVANCE, step);
}

/* 
 * NOTE: For this prototype, we log the userspace VA. 
 * A real driver would pin the memory and translate VA -> PA
 * before writing the physical address to the device registers.
 */
void kv_cpu_cmd_hot(struct kvcpu_dev *kv, u64 va, u64 len)
{
	kv_cpu_write_reg(kv, KVCPU_REG_BOOST_ADDR, va);
	kv_cpu_write_reg(kv, KVCPU_REG_BOOST_LEN, len);
}

void kv_cpu_cmd_evict(struct kvcpu_dev *kv, u64 va, u64 len)
{
	kv_cpu_write_reg(kv, KVCPU_REG_IMEVICT_ADDR, va);
	kv_cpu_write_reg(kv, KVCPU_REG_IMEVICT_LEN, len);
}

void kv_cpu_cmd_prefetch(struct kvcpu_dev *kv, u64 va, u64 len, u64 step)
{
	/* Concept: Target address + metadata written to doorbell registers */
	kv_cpu_write_reg(kv, 0x0300, va);
	kv_cpu_write_reg(kv, 0x0308, len);
	kv_cpu_write_reg(kv, 0x0310, step);
}

void kv_cpu_cmd_share(struct kvcpu_dev *kv, u64 va, u64 len)
{
	/* Concept: Signal hardware that this range has multi-tenant prefix sharing */
	kv_cpu_write_reg(kv, 0x0400, va);
	kv_cpu_write_reg(kv, 0x0408, len);
}
