// SPDX-License-Identifier: GPL-2.0-only
/*
 * kv_cpu_nmce.c — Near-Memory Compute Engine (Pillar I)
 *
 * Manages the NMCE submission/completion queue pair.
 * The GPU-side LLM runtime submits NMCE compute descriptors (SQEs)
 * describing attention scoring operations. The device computes dot-product
 * attention scores from key vectors resident in T1 LPDDR5X and DMA-writes
 * only the scalar score results back to GPU-mapped memory.
 *
 * Queue model: single producer (host), single consumer (device).
 *   - Host writes SQEs and advances SQ tail via MMIO
 *   - Device writes CQEs and advances CQ tail via MMIO / DMA
 *   - Host reads CQEs and advances CQ head via MMIO
 *
 * Author: Manish Keshav Lachwani <mlachwani@gmail.com>
 */

#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include "../include/kv_cpu.h"

int kvcpu_nmce_init(struct kvcpu_dev *kv)
{
	struct kvcpu_queue *q = &kv->queue;
	size_t sq_bytes = KVCPU_SQ_ENTRIES * sizeof(struct kvcpu_sqe);
	size_t cq_bytes = KVCPU_CQ_ENTRIES * sizeof(struct kvcpu_cqe);

	spin_lock_init(&q->lock);

	/* Allocate coherent DMA memory for SQ */
	q->sq = dma_alloc_coherent(&kv->pdev->dev, sq_bytes,
				   &q->sq_dma, GFP_KERNEL);
	if (!q->sq) {
		dev_err(kv->dev, "NMCE: SQ DMA alloc failed (%zu bytes)\n", sq_bytes);
		return -ENOMEM;
	}

	/* Allocate coherent DMA memory for CQ */
	q->cq = dma_alloc_coherent(&kv->pdev->dev, cq_bytes,
				   &q->cq_dma, GFP_KERNEL);
	if (!q->cq) {
		dev_err(kv->dev, "NMCE: CQ DMA alloc failed (%zu bytes)\n", cq_bytes);
		dma_free_coherent(&kv->pdev->dev, sq_bytes, q->sq, q->sq_dma);
		q->sq = NULL;
		return -ENOMEM;
	}

	q->sq_tail = 0;
	q->cq_head = 0;

	/* Program queue base addresses and sizes into hardware */
	kvcpu_writeq(kv, KVCPU_REG_SQ_BASE, (u64)q->sq_dma);
	kvcpu_writeq(kv, KVCPU_REG_SQ_SIZE, KVCPU_SQ_ENTRIES);
	kvcpu_writeq(kv, KVCPU_REG_CQ_BASE, (u64)q->cq_dma);
	kvcpu_writeq(kv, KVCPU_REG_CQ_SIZE, KVCPU_CQ_ENTRIES);

	/* Reset hardware head/tail pointers */
	kvcpu_writeq(kv, KVCPU_REG_SQ_TAIL, 0);
	kvcpu_writeq(kv, KVCPU_REG_CQ_HEAD, 0);

	dev_info(kv->dev,
		 "NMCE init: SQ@0x%llx CQ@0x%llx entries=%d/%d\n",
		 (u64)q->sq_dma, (u64)q->cq_dma,
		 KVCPU_SQ_ENTRIES, KVCPU_CQ_ENTRIES);

	return 0;
}

void kvcpu_nmce_teardown(struct kvcpu_dev *kv)
{
	struct kvcpu_queue *q = &kv->queue;

	if (!q->sq)
		return;

	/* Wait for any in-flight operations — poll NMCE_STATUS */
	int timeout = 1000;
	while (kvcpu_readq(kv, KVCPU_REG_NMCE_STATUS) != 0 && --timeout > 0)
		udelay(100);
	if (timeout == 0)
		dev_warn(kv->dev, "NMCE teardown: timeout waiting for idle\n");

	dma_free_coherent(&kv->pdev->dev,
			  KVCPU_SQ_ENTRIES * sizeof(struct kvcpu_sqe),
			  q->sq, q->sq_dma);
	dma_free_coherent(&kv->pdev->dev,
			  KVCPU_CQ_ENTRIES * sizeof(struct kvcpu_cqe),
			  q->cq, q->cq_dma);
	q->sq = NULL;
	q->cq = NULL;
}

/*
 * kvcpu_nmce_submit - enqueue one NMCE attention-score descriptor.
 *
 * Copies the SQE into the next free slot, then rings the doorbell by
 * writing the updated tail to KVCPU_REG_SQ_TAIL.
 *
 * Returns 0 on success, -ENOSPC if queue is full.
 */
int kvcpu_nmce_submit(struct kvcpu_dev *kv, struct kvcpu_sqe *sqe)
{
	struct kvcpu_queue *q = &kv->queue;
	unsigned long flags;
	u32 head, next_tail;

	spin_lock_irqsave(&q->lock, flags);

	head = (u32)kvcpu_readq(kv, KVCPU_REG_SQ_HEAD);
	next_tail = (q->sq_tail + 1) % KVCPU_SQ_ENTRIES;

	if (next_tail == head) {
		spin_unlock_irqrestore(&q->lock, flags);
		dev_warn(kv->dev, "NMCE SQ full — dropping descriptor\n");
		return -ENOSPC;
	}

	/* Write SQE into queue slot */
	memcpy(&q->sq[q->sq_tail], sqe, sizeof(*sqe));
	q->sq_tail = next_tail;

	/* Doorbell: write new tail — device starts processing immediately */
	kvcpu_writeq(kv, KVCPU_REG_SQ_TAIL, q->sq_tail);

	spin_unlock_irqrestore(&q->lock, flags);

	dev_dbg(kv->dev,
		"NMCE submit: req=%u layer=%u head=%u key_pa=0x%llx\n",
		sqe->request_id, sqe->layer_idx, sqe->head_idx,
		sqe->key_block_phys);

	return 0;
}
