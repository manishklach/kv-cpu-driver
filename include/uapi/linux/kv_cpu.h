/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _UAPI_LINUX_KV_CPU_H
#define _UAPI_LINUX_KV_CPU_H

#include <linux/types.h>
#include <linux/ioctl.h>

/**
 * KV-CPU UAPI
 * 
 * Address Semantics:
 * In this reference driver, 'va' fields refer to the Userspace Virtual Address
 * of the KV blocks. A production implementation would perform GUP (Get User Pages)
 * and DMA mapping, but for this control-plane prototype, we use VAs to 
 * demonstrate semantic signaling.
 */

struct kv_cpu_step_info {
	__u64 step;        /* Current decode iteration */
};

struct kv_cpu_block_info {
	__u64 va;          /* Userspace virtual address of the KV block */
	__u64 len;         /* Length in bytes */
	__u64 target_step; /* Used for PREFETCH: when this block will be needed */
};

#define KV_CPU_MAGIC 'K'

/* Signal that the inference runtime has advanced to a new decode step */
#define KV_CPU_STEP_ADVANCE    _IOW(KV_CPU_MAGIC, 0x01, struct kv_cpu_step_info)

/* Mark a memory range as 'Hot' (boost priority in hardware HEPC) */
#define KV_CPU_MARK_HOT        _IOW(KV_CPU_MAGIC, 0x02, struct kv_cpu_block_info)

/* Signal immediate eviction candidate */
#define KV_CPU_EVICT           _IOW(KV_CPU_MAGIC, 0x03, struct kv_cpu_block_info)

/* Hint that a block will be needed at target_step */
#define KV_CPU_PREFETCH        _IOW(KV_CPU_MAGIC, 0x04, struct kv_cpu_block_info)

/* Signal that a prefix is shared (increment hardware refcount) */
#define KV_CPU_SHARE_PREFIX    _IOW(KV_CPU_MAGIC, 0x05, struct kv_cpu_block_info)

#endif /* _UAPI_LINUX_KV_CPU_H */
