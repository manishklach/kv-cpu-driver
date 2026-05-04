// SPDX-License-Identifier: GPL-2.0-only
/*
 * kv_cpu_hepc.c — Hardware Eviction & Prefetch Controller (Pillar II)
 *
 * Implements the kernel-side control plane for the HEPC:
 *
 *   - kvcpu_hepc_init(): program default weights/thresholds into HW registers
 *   - kvcpu_hepc_step_advance(): write decode step t → triggers HW scan/evict/prefetch
 *   - kvcpu_hepc_boost_range(): MADV_KV_HOT path → set max priority for VA range
 *   - kvcpu_hepc_evict_range(): MADV_KV_EVICT path → immediate eviction
 *   - kvcpu_hepc_prefetch_hint(): MADV_KV_PREFETCH path → update step + window
 *   - kvcpu_hepc_set_weights(): allow runtime tuning of R/F/S/D weights
 *
 * The step-advance MMIO write is on the absolute hot path: called once per
 * decode step from either the madvise handler or the io_uring completion
 * callback. We expose a BAR0 mmap window so GPU-side userspace (e.g. vLLM
 * worker process) can write it directly at sub-microsecond latency without
 * a syscall.
 *
 * Author: Manish Keshav Lachwani <mlachwani@gmail.com>
 */

#include <linux/module.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include "../include/kv_cpu.h"

/*
 * kvcpu_hepc_init - program initial HEPC configuration into hardware.
 *
 * Called once from probe after mem_register. All weights/thresholds are
 * written atomically in order: thresholds first, then weights, then window.
 * The device must not be in SCAN/EVICT/PREFETCH phase during this call;
 * probe guarantees that because step-advance has not been written yet.
 */
int kvcpu_hepc_init(struct kvcpu_dev *kv)
{
	struct kvcpu_hepc_config *cfg = &kv->hepc;

	/* Thresholds */
	kvcpu_writeq(kv, KVCPU_REG_EVICT_THRESH,  cfg->evict_threshold);
	kvcpu_writeq(kv, KVCPU_REG_PREFETCH_THR,  cfg->prefetch_threshold);
	kvcpu_writeq(kv, KVCPU_REG_WINDOW_W,       cfg->window_w);

	/* Scoring weights */
	kvcpu_writeq(kv, KVCPU_REG_WEIGHT_R, cfg->weight_r);
	kvcpu_writeq(kv, KVCPU_REG_WEIGHT_F, cfg->weight_f);
	kvcpu_writeq(kv, KVCPU_REG_WEIGHT_S, cfg->weight_s);
	kvcpu_writeq(kv, KVCPU_REG_WEIGHT_D, cfg->weight_d);

	dev_info(kv->dev,
		 "HEPC init: evict_thr=%u prefetch_thr=%u W=%u "
		 "weights: R=%u F=%u S=%u D=%u\n",
		 cfg->evict_threshold, cfg->prefetch_threshold, cfg->window_w,
		 cfg->weight_r, cfg->weight_f, cfg->weight_s, cfg->weight_d);

	return 0;
}

/*
 * kvcpu_hepc_step_advance - signal the hardware that a new decode step
 * has begun.
 *
 * This is the single most performance-critical call in the entire driver.
 * A single 64-bit MMIO write triggers the hardware SCAN → EVICT → PREFETCH
 * cycle asynchronously. The host CPU returns immediately; the hardware
 * pipeline runs in parallel with the next GPU decode iteration.
 *
 * Called from:
 *   - kvcpu_madvise_handler() when MADV_KV_PREFETCH is received
 *   - io_uring KV_STAGE/KV_EVICT completion callbacks
 *   - ioctl(KVCPU_IOC_STEP_ADVANCE) for explicit control
 *
 * On the hot path this should be < 100 ns (single PCIe TLP write).
 */
void kvcpu_hepc_step_advance(struct kvcpu_dev *kv, u64 step)
{
	/*
	 * writeq() is a full write barrier on x86 (mov to MMIO).
	 * On ARM64 we need wmb() before the MMIO write to ensure all prior
	 * stores (e.g. query vector written to DMA buffer) are visible to
	 * the device before it starts the new scan phase.
	 */
	wmb();
	kvcpu_writeq(kv, KVCPU_REG_STEP_ADVANCE, step);

	dev_dbg(kv->dev, "step_advance: t=%llu\n", step);
}
EXPORT_SYMBOL_GPL(kvcpu_hepc_step_advance);

/*
 * kvcpu_hepc_boost_range - MADV_KV_HOT handler.
 *
 * Translates a userspace virtual address range to physical addresses and
 * programs the HEPC boost registers. The hardware sets priority_score to
 * 0xFFFF for all RTBD entries whose physical address falls in [pa, pa+len).
 *
 * The VA→PA translation uses follow_pfn() since these are expected to be
 * DMA-coherent buffers already pinned by the LLM runtime.
 *
 * Takes kv->queue.lock to serialise concurrent madvise calls.
 */
void kvcpu_hepc_boost_range(struct kvcpu_dev *kv, phys_addr_t pa, size_t len)
{
	unsigned long flags;

	spin_lock_irqsave(&kv->queue.lock, flags);

	kvcpu_writeq(kv, KVCPU_REG_BOOST_ADDR, (u64)pa);
	/* Writing BOOST_LEN is the trigger — hardware starts boost immediately */
	kvcpu_writeq(kv, KVCPU_REG_BOOST_LEN,  (u64)len);

	spin_unlock_irqrestore(&kv->queue.lock, flags);

	dev_dbg(kv->dev, "boost_range: pa=0x%llx len=0x%zx\n",
		(u64)pa, len);
}

/*
 * kvcpu_hepc_evict_range - MADV_KV_EVICT handler.
 *
 * Programs the immediate-evict registers, causing the HEPC to bypass
 * the normal scan phase and immediately queue the specified blocks for
 * DMA eviction to T2/T3 at the next available DMA slot.
 *
 * This does NOT block until eviction is complete. The caller should poll
 * KVCPU_REG_HEPC_STATUS or wait for KVCPU_IRQ_EVICT_DONE if synchronous
 * completion is required.
 */
void kvcpu_hepc_evict_range(struct kvcpu_dev *kv, phys_addr_t pa, size_t len)
{
	unsigned long flags;

	spin_lock_irqsave(&kv->queue.lock, flags);
	kvcpu_writeq(kv, KVCPU_REG_IMEVICT_ADDR, (u64)pa);
	kvcpu_writeq(kv, KVCPU_REG_IMEVICT_LEN,  (u64)len);
	spin_unlock_irqrestore(&kv->queue.lock, flags);

	dev_dbg(kv->dev, "evict_range: pa=0x%llx len=0x%zx\n", (u64)pa, len);
}

/*
 * kvcpu_hepc_prefetch_hint - MADV_KV_PREFETCH handler.
 *
 * Updates both the step-advance register (triggering a new HEPC cycle)
 * and the lookahead window W register. The hardware will then prefetch
 * all blocks with S(B_i) = max(0, W - (step - t_last_access)) > prefetch_thr.
 *
 * @step:      current decode step t (triggers HEPC cycle)
 * @lookahead: desired prefetch lookahead window W (decode steps)
 */
void kvcpu_hepc_prefetch_hint(struct kvcpu_dev *kv, phys_addr_t pa,
			      size_t len, u64 step, u32 lookahead)
{
	/* Update window W first, then advance step to trigger evaluation */
	kvcpu_writeq(kv, KVCPU_REG_WINDOW_W, lookahead);
	kvcpu_hepc_step_advance(kv, step);

	dev_dbg(kv->dev, "prefetch_hint: pa=0x%llx len=0x%zx step=%llu W=%u\n",
		(u64)pa, len, step, lookahead);
}

/*
 * kvcpu_hepc_set_weights - runtime tuning of priority scoring weights.
 *
 * Allows the LLM runtime (via ioctl) to adjust the R/F/S/D weighting
 * without reloading the driver. Useful for model-specific tuning:
 *   - Sparse-attention models: raise w_s (step proximity matters more)
 *   - Prefix-heavy deployments: raise w_d (protect prefix blocks harder)
 *   - Short-context workloads: raise w_r (pure recency is sufficient)
 */
int kvcpu_hepc_set_weights(struct kvcpu_dev *kv, struct kvcpu_hepc_config *cfg)
{
	/* Validate ranges */
	if (cfg->evict_threshold >= cfg->prefetch_threshold) {
		dev_err(kv->dev,
			"evict_threshold (%u) must be < prefetch_threshold (%u)\n",
			cfg->evict_threshold, cfg->prefetch_threshold);
		return -EINVAL;
	}
	if (cfg->window_w == 0 || cfg->window_w > 4096) {
		dev_err(kv->dev, "window_w %u out of range [1, 4096]\n",
			cfg->window_w);
		return -EINVAL;
	}

	/* Atomically update hardware — device reads weights at next SCAN */
	kvcpu_writeq(kv, KVCPU_REG_EVICT_THRESH,  cfg->evict_threshold);
	kvcpu_writeq(kv, KVCPU_REG_PREFETCH_THR,  cfg->prefetch_threshold);
	kvcpu_writeq(kv, KVCPU_REG_WINDOW_W,       cfg->window_w);
	kvcpu_writeq(kv, KVCPU_REG_WEIGHT_R,       cfg->weight_r);
	kvcpu_writeq(kv, KVCPU_REG_WEIGHT_F,       cfg->weight_f);
	kvcpu_writeq(kv, KVCPU_REG_WEIGHT_S,       cfg->weight_s);
	kvcpu_writeq(kv, KVCPU_REG_WEIGHT_D,       cfg->weight_d);

	/* Update cached copy */
	kv->hepc = *cfg;

	dev_info(kv->dev,
		 "HEPC weights updated: R=%u F=%u S=%u D=%u "
		 "evict_thr=%u prefetch_thr=%u W=%u\n",
		 cfg->weight_r, cfg->weight_f, cfg->weight_s, cfg->weight_d,
		 cfg->evict_threshold, cfg->prefetch_threshold, cfg->window_w);

	return 0;
}
