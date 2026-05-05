/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * KV-CPU Control Plane Driver - MMIO register helpers
 *
 * Copyright (C) 2026 Manish KL
 */

#include <linux/io.h>
#include "kv_cpu.h"

/**
 * kv_cpu_write_reg - Helper to write 64-bit register
 * @kv: Pointer to device state
 * @offset: Register offset in BAR0
 * @val: 64-bit value to write
 */
void kv_cpu_write_reg(struct kv_cpu_device *kv, u32 offset, u64 val)
{
	if (unlikely(kv->is_mock)) {
		u64 *reg = (u64 *)(kv->mock_bar + offset);
		*reg = val;
		return;
	}

	writeq(val, kv->bar0 + offset);
}

void kv_cpu_cmd_step(struct kv_cpu_device *kv, u64 step)
{
	kv_cpu_write_reg(kv, KVCPU_REG_STEP_ADVANCE, step);
}

void kv_cpu_cmd_hot(struct kv_cpu_device *kv, u64 va, u64 len)
{
	kv_cpu_write_reg(kv, KVCPU_REG_BOOST_ADDR, va);
	kv_cpu_write_reg(kv, KVCPU_REG_BOOST_LEN, len);
}

void kv_cpu_cmd_evict(struct kv_cpu_device *kv, u64 va, u64 len)
{
	kv_cpu_write_reg(kv, KVCPU_REG_IMEVICT_ADDR, va);
	kv_cpu_write_reg(kv, KVCPU_REG_IMEVICT_LEN, len);
}

void kv_cpu_cmd_prefetch(struct kv_cpu_device *kv, u64 va, u64 len, u64 step)
{
	/* Reference implementation of prefetch metadata doorbell */
	kv_cpu_write_reg(kv, KVCPU_REG_PREFETCH_ADDR, va);
	kv_cpu_write_reg(kv, KVCPU_REG_PREFETCH_LEN, len);
	kv_cpu_write_reg(kv, KVCPU_REG_PREFETCH_STEP, step);
}

void kv_cpu_cmd_share(struct kv_cpu_device *kv, u64 va, u64 len)
{
	/* Reference implementation of prefix sharing signal */
	kv_cpu_write_reg(kv, KVCPU_REG_SHARE_ADDR, va);
	kv_cpu_write_reg(kv, KVCPU_REG_SHARE_LEN, len);
}
