// SPDX-License-Identifier: GPL-2.0-only
/*
 * kv_cpu_madvise.c — Semantic Memory Hint Interface (Pillar IV)
 *
 * Registers and handles the three extended madvise behaviors:
 *
 *   MADV_KV_HOT      (25) — blocks are actively hot; set max HEPC priority
 *   MADV_KV_EVICT    (26) — blocks no longer needed; schedule immediate eviction
 *   MADV_KV_PREFETCH (27) — advance decode step + update lookahead window
 *
 * Each madvise call translates a userspace VA range to physical addresses
 * and issues one or two MMIO writes to the HEPC. Total latency from syscall
 * entry to MMIO write: ~500 ns on a modern server (dominated by VA→PA walk).
 *
 * For the absolute hot path (step-advance), userspace can mmap BAR0 and
 * write KVCPU_REG_STEP_ADVANCE directly — see kv_cpu_main.c:kvcpu_mmap().
 *
 * Integration note: The kernel's madvise extension API is not yet upstream
 * (as of 6.11). This file implements the driver-side handler and assumes
 * a small patch to mm/madvise.c to dispatch unknown behaviors to registered
 * device handlers. The patch is included in tools/patches/.
 *
 * Author: Manish Keshav Lachwani <mlachwani@gmail.com>
 */

#include <linux/module.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/hugetlb.h>
#include <linux/pagemap.h>
#include "../include/kv_cpu.h"

/* Global driver reference — only one KV-CPU device expected per host */
static struct kvcpu_dev *g_kv;
static DEFINE_MUTEX(g_kv_lock);

/*
 * va_range_to_pa — walk page tables to get contiguous PA for a VA range.
 *
 * For pinned DMA buffers (the common LLM case), this is a simple PTE walk.
 * For non-pinned memory we return the PA of the first page and assume the
 * caller's allocation is physically contiguous (large hugepage KV blocks).
 *
 * Returns 0 on success, negative errno on failure.
 */
static int va_range_to_pa(struct mm_struct *mm, unsigned long va,
			  size_t len, phys_addr_t *pa_out)
{
	struct page *page;
	int ret;

	/* pin one page to get its physical address */
	ret = get_user_pages_fast(va, 1, 0, &page);
	if (ret < 0)
		return ret;
	if (ret == 0)
		return -EFAULT;

	*pa_out = page_to_phys(page);
	put_page(page);

	/*
	 * Note: for large KV blocks (multiple pages), we rely on the fact
	 * that LLM runtimes allocate KV cache with mmap(MAP_HUGETLB) or
	 * via cudaMallocHost which gives physically contiguous pinned pages.
	 * A production driver would walk each page and issue per-page HEPC
	 * commands if non-contiguous.
	 */
	return 0;
}

/*
 * kvcpu_madvise_handler — dispatch madvise behavior to HEPC hardware.
 *
 * Called by the mm/madvise.c extension hook after VA→PA translation.
 *
 * @start:    userspace virtual address (page-aligned)
 * @len:      length in bytes (page-aligned)
 * @behavior: MADV_KV_HOT / MADV_KV_EVICT / MADV_KV_PREFETCH
 * @aux:      behavior-specific auxiliary data:
 *             MADV_KV_PREFETCH: high32=lookahead, low32=step (packed u64)
 *             others: unused
 */
int kvcpu_madvise_handler(struct kvcpu_dev *kv, unsigned long start,
			  size_t len, int behavior, u64 aux)
{
	phys_addr_t pa;
	int ret;

	ret = va_range_to_pa(current->mm, start, len, &pa);
	if (ret) {
		dev_dbg(kv->dev,
			"madvise: va→pa failed for 0x%lx len=%zu: %d\n",
			start, len, ret);
		return ret;
	}

	switch (behavior) {
	case MADV_KV_HOT:
		/*
		 * Boost priority of all RTBD entries in [pa, pa+len) to
		 * 0xFFFF so they cannot be evicted during the active decode
		 * loop. Single MMIO write pair — ~50 ns hardware latency.
		 */
		kvcpu_hepc_boost_range(kv, pa, len);
		dev_dbg(kv->dev, "MADV_KV_HOT: va=0x%lx pa=0x%llx len=%zu\n",
			start, (u64)pa, len);
		break;

	case MADV_KV_EVICT:
		/*
		 * Schedule immediate background eviction to T2/T3.
		 * Does not block — the HEPC DMA engine handles it async.
		 */
		kvcpu_hepc_evict_range(kv, pa, len);
		dev_dbg(kv->dev, "MADV_KV_EVICT: va=0x%lx pa=0x%llx len=%zu\n",
			start, (u64)pa, len);
		break;

	case MADV_KV_PREFETCH: {
		/*
		 * aux packs: bits[63:32] = lookahead window W
		 *            bits[31:0]  = current decode step t
		 */
		u64 step      = aux & 0xFFFFFFFFULL;
		u32 lookahead = (u32)(aux >> 32);

		kvcpu_hepc_prefetch_hint(kv, pa, len, step, lookahead);
		dev_dbg(kv->dev,
			"MADV_KV_PREFETCH: va=0x%lx step=%llu W=%u\n",
			start, step, lookahead);
		break;
	}

	default:
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(kvcpu_madvise_handler);

/*
 * Global madvise dispatch hook.
 *
 * This function is registered as the extension handler in mm/madvise.c.
 * When madvise(2) receives an unknown behavior code in the KV range
 * (MADV_KV_HOT=25, MADV_KV_EVICT=26, MADV_KV_PREFETCH=27), the kernel
 * calls this function instead of returning -EINVAL.
 *
 * The 'aux' parameter is passed via an extended syscall variant:
 *   madvise_ex(addr, len, behavior, aux)  [new syscall, see RFC patch]
 * or via prctl(PR_KV_MADVISE, addr, len, behavior, aux) as a fallback.
 */
static int kvcpu_madvise_dispatch(unsigned long start, size_t len,
				  int behavior, u64 aux)
{
	struct kvcpu_dev *kv;
	int ret;

	mutex_lock(&g_kv_lock);
	kv = g_kv;
	if (!kv) {
		mutex_unlock(&g_kv_lock);
		return -ENODEV;
	}
	mutex_unlock(&g_kv_lock);

	ret = kvcpu_madvise_handler(kv, start, len, behavior, aux);
	return ret;
}

/* madvise extension registration stub.
 *
 * In the upstream patch this calls register_madvise_behavior_handler()
 * exported from mm/madvise.c. Until that lands we register via a sysfs
 * interface and the ioctl path as a fallback.
 */
int kvcpu_madvise_register(struct kvcpu_dev *kv)
{
	mutex_lock(&g_kv_lock);
	if (g_kv) {
		mutex_unlock(&g_kv_lock);
		dev_warn(kv->dev, "madvise: another KV-CPU already registered\n");
		return -EBUSY;
	}
	g_kv = kv;
	mutex_unlock(&g_kv_lock);

	dev_info(kv->dev,
		 "madvise extensions registered: "
		 "MADV_KV_HOT=%d MADV_KV_EVICT=%d MADV_KV_PREFETCH=%d\n",
		 MADV_KV_HOT, MADV_KV_EVICT, MADV_KV_PREFETCH);
	return 0;
}

void kvcpu_madvise_unregister(struct kvcpu_dev *kv)
{
	mutex_lock(&g_kv_lock);
	if (g_kv == kv)
		g_kv = NULL;
	mutex_unlock(&g_kv_lock);
}
