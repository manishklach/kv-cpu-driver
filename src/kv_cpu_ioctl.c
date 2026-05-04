/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * kv_cpu_ioctl.c — UAPI entry points and semantic translation
 */
#include <linux/uaccess.h>
#include <linux/fs.h>
#include "kv_cpu_internal.h"

long kvcpu_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct kvcpu_dev *kv = file->private_data;

	switch (cmd) {
	case KV_CPU_STEP_ADVANCE: {
		struct kv_step_info si;
		if (copy_from_user(&si, (void __user *)arg, sizeof(si)))
			return -EFAULT;
		
		/* Translate inference step to hardware doorbell */
		kvcpu_policy_step_advance(kv, si.step);
		return 0;
	}

	case KV_CPU_MARK_HOT: {
		struct kv_block_range br;
		if (copy_from_user(&br, (void __user *)arg, sizeof(br)))
			return -EFAULT;
		
		/* 
		 * Real implementation would perform VA->PA translation here 
		 * before signaling hardware boost registers.
		 */
		kvcpu_policy_mark_hot(kv, (phys_addr_t)br.start, br.len);
		return 0;
	}

	case KV_CPU_EVICT: {
		struct kv_block_range br;
		if (copy_from_user(&br, (void __user *)arg, sizeof(br)))
			return -EFAULT;
		
		/* Enqueue asynchronous DMA eviction descriptor */
		return kvcpu_dma_evict(kv, (phys_addr_t)br.start, br.len);
	}

	case KV_CPU_PREFETCH: {
		struct kv_prefetch_args pa;
		if (copy_from_user(&pa, (void __user *)arg, sizeof(pa)))
			return -EFAULT;
		
		/* Schedule lookahead prefetch into T1 memory */
		return kvcpu_dma_prefetch(kv, (phys_addr_t)pa.range.start, pa.range.len);
	}

	default:
		return -ENOTTY;
	}
}
