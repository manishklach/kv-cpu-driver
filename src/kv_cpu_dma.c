/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * kv_cpu_dma.c — DMA engine stubs and descriptor management
 */
#include "kv_cpu_internal.h"

int kvcpu_dma_evict(struct kvcpu_dev *kv, phys_addr_t pa, size_t len)
{
	/* 
	 * REFERENCE LOGIC:
	 * 1. Allocate DMA descriptor from hardware ring buffer.
	 * 2. Set src = pa, dst = T2_phys_addr, len = len.
	 * 3. Set opcode = KV_DMA_OP_EVICT.
	 * 4. Write doorbell to trigger hardware DMA engine.
	 */
	dev_dbg(kv->dev, "DMA: enqueued eviction for PA 0x%llx (len %zu)\n", 
		(u64)pa, len);
	
	/* For reference, we just write to the conceptual EVICT register */
	kvcpu_mmio_write64(kv, KVCPU_REG_IMEVICT_ADDR, (u64)pa);
	kvcpu_mmio_write64(kv, KVCPU_REG_IMEVICT_LEN,  (u64)len);
	
	return 0;
}

int kvcpu_dma_prefetch(struct kvcpu_dev *kv, phys_addr_t pa, size_t len)
{
	/* 
	 * REFERENCE LOGIC:
	 * Similar to eviction, but with KV_DMA_OP_STAGE opcode to move
	 * blocks from host memory into the KV-CPU T1 LPDDR5X.
	 */
	dev_dbg(kv->dev, "DMA: enqueued prefetch for PA 0x%llx (len %zu)\n", 
		(u64)pa, len);
	return 0;
}
