/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * kv_cpu_ioctl.h — ioctl interface for /dev/kvcpuN
 *
 * Shared between kernel driver and userspace tools.
 * Safe to include from both C and C++ userspace.
 *
 * Author: Manish Keshav Lachwani <mlachwani@gmail.com>
 */

#ifndef _KV_CPU_IOCTL_H
#define _KV_CPU_IOCTL_H

#include <linux/types.h>
#include <linux/ioctl.h>

#define KVCPU_IOC_MAGIC  'K'

/* Telemetry snapshot — read all counters atomically */
struct kvcpu_telemetry {
	__u64 evictions;
	__u64 prefetches;
	__u64 nmce_ops;
	__u64 t1_used;
	__u64 t1_free;
	__u64 nmce_bytes_in;
	__u64 nmce_bytes_out;
};

/* RTBD share / release command */
struct kvcpu_rtbd_cmd {
	__u64 block_pa;   /* physical address of the KV block */
	__u16 req_id;     /* inference request ID             */
	__u16 _pad[3];
};

/* RTBD query — returns entry fields for a given block PA */
struct kvcpu_rtbd_query_arg {
	__u64 block_pa;
	struct kvcpu_rtbd_entry __user *entry_out;
};

/* ioctl command codes */
#define KVCPU_IOC_STEP_ADVANCE    _IOW(KVCPU_IOC_MAGIC, 1,  __u64)
#define KVCPU_IOC_GET_TELEMETRY   _IOR(KVCPU_IOC_MAGIC, 2,  struct kvcpu_telemetry)
#define KVCPU_IOC_RTBD_SHARE      _IOW(KVCPU_IOC_MAGIC, 3,  struct kvcpu_rtbd_cmd)
#define KVCPU_IOC_RTBD_RELEASE    _IOW(KVCPU_IOC_MAGIC, 4,  struct kvcpu_rtbd_cmd)
#define KVCPU_IOC_RTBD_QUERY      _IOWR(KVCPU_IOC_MAGIC, 5, struct kvcpu_rtbd_query_arg)
#define KVCPU_IOC_SET_WEIGHTS     _IOW(KVCPU_IOC_MAGIC, 6,  struct kvcpu_hepc_config)
#define KVCPU_IOC_SUBMIT_NMCE     _IOW(KVCPU_IOC_MAGIC, 7,  struct kvcpu_sqe)
#define KVCPU_IOC_RTBD_FLUSH      _IOW(KVCPU_IOC_MAGIC, 8,  __u16)

#endif /* _KV_CPU_IOCTL_H */
