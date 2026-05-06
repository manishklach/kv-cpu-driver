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

struct kv_cpu_weights_info {
	__u32 w_r;             /* Recency weight */
	__u32 w_f;             /* Frequency weight */
	__u32 w_s;             /* Step-proximity weight */
	__u32 w_d;             /* Prefix / durability weight */
	__u32 evict_thresh;    /* Eviction threshold */
	__u32 prefetch_thresh; /* Prefetch threshold */
};

struct kv_cpu_telemetry_info {
	__u64 current_step;
	__u64 step_count;
	__u64 hot_count;
	__u64 evict_count;
	__u64 prefetch_count;
	__u64 share_count;
	__u64 last_va;
	__u64 last_len;
	__u64 last_target_step;
	__u32 w_r;
	__u32 w_f;
	__u32 w_s;
	__u32 w_d;
	__u32 evict_thresh;
	__u32 prefetch_thresh;
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

/* Program HEPC weights and thresholds */
#define KV_CPU_SET_WEIGHTS     _IOW(KV_CPU_MAGIC, 0x06, struct kv_cpu_weights_info)

/* Read back control-plane telemetry */
#define KV_CPU_GET_TELEMETRY   _IOR(KV_CPU_MAGIC, 0x07, struct kv_cpu_telemetry_info)

#endif /* _UAPI_LINUX_KV_CPU_H */
