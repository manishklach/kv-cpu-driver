/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * kv_cpu_mmio.c — Hardware register abstraction layer
 */
#include <linux/io.h>
#include "kv_cpu_internal.h"

void kvcpu_mmio_write64(struct kvcpu_dev *kv, u32 offset, u64 val)
{
	/* 
	 * In mock mode, we write to a memory-backed buffer.
	 * In hardware mode, we perform a PCIe/CXL MMIO write.
	 */
	if (unlikely(kv->is_mock)) {
		*(u64 *)(kv->mock_bar + offset) = val;
		return;
	}
	writeq(val, kv->bar0 + offset);
}

u64 kvcpu_mmio_read64(struct kvcpu_dev *kv, u32 offset)
{
	if (unlikely(kv->is_mock))
		return *(u64 *)(kv->mock_bar + offset);
	return readq(kv->bar0 + offset);
}
