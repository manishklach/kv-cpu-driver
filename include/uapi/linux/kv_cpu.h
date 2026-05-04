/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * KV-CPU User-Space API (UAPI)
 *
 * Defines the contract between LLM runtimes (vLLM, SGLang, etc.) and the
 * KV-CPU Linux kernel driver.
 *
 * Author: Manish KL <manishklach@gmail.com>
 */

#ifndef _UAPI_LINUX_KV_CPU_H
#define _UAPI_LINUX_KV_CPU_H

#include <linux/types.h>
#include <linux/ioctl.h>

/**
 * struct kv_block_range - Describes a range of KV-cache memory
 * @start: Userspace virtual address or physical address of the block
 * @len:   Length of the range in bytes
 */
struct kv_block_range {
	__u64 start;
	__u64 len;
};

/**
 * struct kv_step_info - Signal current decode step
 * @step: The current autoregressive decode iteration (t)
 */
struct kv_step_info {
	__u64 step;
};

/**
 * struct kv_prefetch_args - Arguments for hardware-assisted prefetch
 * @range:     The target memory range to prefetch into T1
 * @step:      The predicted step when this block will be needed
 * @lookahead: Number of steps before 'step' to start the DMA
 */
struct kv_prefetch_args {
	struct kv_block_range range;
	__u64 step;
	__u32 lookahead;
};

/* ── ioctl Opcodes ──────────────────────────────────────────────────────── */

#define KV_CPU_IOC_MAGIC 'K'

/* Signal global decode step advancement (Pillar II) */
#define KV_CPU_STEP_ADVANCE  _IOW(KV_CPU_IOC_MAGIC, 0x01, struct kv_step_info)

/* Mark block range as HOT (boost priority in RTBD) */
#define KV_CPU_MARK_HOT      _IOW(KV_CPU_IOC_MAGIC, 0x02, struct kv_block_range)

/* Explicitly trigger eviction of a block to slow tier (T2/T3) */
#define KV_CPU_EVICT         _IOW(KV_CPU_IOC_MAGIC, 0x03, struct kv_block_range)

/* Hint to hardware to prefetch a block into T1 LPDDR5X */
#define KV_CPU_PREFETCH      _IOW(KV_CPU_IOC_MAGIC, 0x04, struct kv_prefetch_args)

/* Increment hardware reference count for prefix sharing (Pillar III) */
#define KV_CPU_SHARE_PREFIX  _IOW(KV_CPU_IOC_MAGIC, 0x05, struct kv_block_range)

#endif /* _UAPI_LINUX_KV_CPU_H */
