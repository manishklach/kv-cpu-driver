// SPDX-License-Identifier: GPL-2.0-only
/*
 * kv_cpu_sysfs.c — sysfs telemetry and control nodes
 *
 * Exposes KV-CPU device state under:
 *   /sys/bus/pci/devices/<BDF>/kvcpu/
 *
 * Read-only telemetry:
 *   t1_size_bytes     — total T1 capacity
 *   t1_used_bytes     — T1 bytes currently allocated
 *   t1_free_bytes     — T1 bytes free
 *   evictions_total   — blocks evicted since driver load
 *   prefetches_total  — blocks prefetched since driver load
 *   nmce_ops_total    — NMCE scoring ops completed
 *   nmce_bytes_saved  — PCIe bytes saved vs naive KV fetch (estimated)
 *   hepc_status       — current HEPC phase string
 *   rtbd_capacity     — max RTBD entries supported by hardware
 *   numa_node         — NUMA node ID assigned to T1
 *
 * Read-write control:
 *   hepc_evict_threshold   — [0..65535]
 *   hepc_prefetch_threshold
 *   hepc_window_w
 *   hepc_weight_r/f/s/d    — [0..255]
 *
 * Author: Manish Keshav Lachwani <mlachwani@gmail.com>
 */

#include <linux/module.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/slab.h>
#include "../include/kv_cpu.h"

/* ── telemetry show functions ─────────────────────────────────────────────── */

#define KVCPU_SYSFS_RO_REG(name, reg)					\
static ssize_t name##_show(struct kobject *kobj,			\
			    struct kobj_attribute *attr, char *buf)	\
{									\
	struct kvcpu_dev *kv =						\
		container_of(kobj, struct kvcpu_dev, kobj[0]);		\
	return sysfs_emit(buf, "%llu\n", kvcpu_readq(kv, reg));		\
}									\
static struct kobj_attribute attr_##name = __ATTR_RO(name)

KVCPU_SYSFS_RO_REG(t1_size_bytes,     KVCPU_REG_MEM_SIZE);
KVCPU_SYSFS_RO_REG(t1_used_bytes,     KVCPU_REG_T1_USED);
KVCPU_SYSFS_RO_REG(t1_free_bytes,     KVCPU_REG_T1_FREE);
KVCPU_SYSFS_RO_REG(evictions_total,   KVCPU_REG_EVICT_COUNT);
KVCPU_SYSFS_RO_REG(prefetches_total,  KVCPU_REG_PREFETCH_CNT);
KVCPU_SYSFS_RO_REG(nmce_ops_total,    KVCPU_REG_NMCE_OPS);
KVCPU_SYSFS_RO_REG(rtbd_capacity,     KVCPU_REG_RTBD_CAP);

static ssize_t nmce_bytes_saved_show(struct kobject *kobj,
				     struct kobj_attribute *attr, char *buf)
{
	struct kvcpu_dev *kv =
		container_of(kobj, struct kvcpu_dev, kobj[0]);
	/*
	 * Estimated bytes saved = ops * (avg_key_block_bytes - avg_score_bytes)
	 * For D=128, B=32: 8192 - 320 = 7872 bytes saved per op.
	 */
	u64 ops   = kvcpu_readq(kv, KVCPU_REG_NMCE_OPS);
	u64 saved = ops * 7872ULL;
	return sysfs_emit(buf, "%llu\n", saved);
}
static struct kobj_attribute attr_nmce_bytes_saved = __ATTR_RO(nmce_bytes_saved);

static ssize_t hepc_status_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	struct kvcpu_dev *kv =
		container_of(kobj, struct kvcpu_dev, kobj[0]);
	static const char * const phases[] = {
		"idle", "scan", "evict", "prefetch"
	};
	u64 st = kvcpu_readq(kv, KVCPU_REG_HEPC_STATUS) & 0x3;
	return sysfs_emit(buf, "%s\n", phases[st]);
}
static struct kobj_attribute attr_hepc_status = __ATTR_RO(hepc_status);

static ssize_t numa_node_show(struct kobject *kobj,
			      struct kobj_attribute *attr, char *buf)
{
	struct kvcpu_dev *kv =
		container_of(kobj, struct kvcpu_dev, kobj[0]);
	return sysfs_emit(buf, "%d\n", kv->mem.numa_node);
}
static struct kobj_attribute attr_numa_node = __ATTR_RO(numa_node);

/* ── read-write HEPC control ──────────────────────────────────────────────── */

#define KVCPU_SYSFS_RW_REG(name, reg, max_val)				\
static ssize_t name##_show(struct kobject *kobj,			\
			    struct kobj_attribute *attr, char *buf)	\
{									\
	struct kvcpu_dev *kv =						\
		container_of(kobj, struct kvcpu_dev, kobj[0]);		\
	return sysfs_emit(buf, "%llu\n", kvcpu_readq(kv, reg));		\
}									\
static ssize_t name##_store(struct kobject *kobj,			\
			     struct kobj_attribute *attr,		\
			     const char *buf, size_t count)		\
{									\
	struct kvcpu_dev *kv =						\
		container_of(kobj, struct kvcpu_dev, kobj[0]);		\
	u64 val;							\
	if (kstrtou64(buf, 0, &val))					\
		return -EINVAL;						\
	if (val > (max_val))						\
		return -ERANGE;						\
	kvcpu_writeq(kv, reg, val);					\
	return count;							\
}									\
static struct kobj_attribute attr_##name =				\
	__ATTR(name, 0644, name##_show, name##_store)

KVCPU_SYSFS_RW_REG(hepc_evict_threshold,    KVCPU_REG_EVICT_THRESH, 65535);
KVCPU_SYSFS_RW_REG(hepc_prefetch_threshold, KVCPU_REG_PREFETCH_THR, 65535);
KVCPU_SYSFS_RW_REG(hepc_window_w,           KVCPU_REG_WINDOW_W,     4096);
KVCPU_SYSFS_RW_REG(hepc_weight_r,           KVCPU_REG_WEIGHT_R,     255);
KVCPU_SYSFS_RW_REG(hepc_weight_f,           KVCPU_REG_WEIGHT_F,     255);
KVCPU_SYSFS_RW_REG(hepc_weight_s,           KVCPU_REG_WEIGHT_S,     255);
KVCPU_SYSFS_RW_REG(hepc_weight_d,           KVCPU_REG_WEIGHT_D,     255);

static struct attribute *kvcpu_attrs[] = {
	&attr_t1_size_bytes.attr,
	&attr_t1_used_bytes.attr,
	&attr_t1_free_bytes.attr,
	&attr_evictions_total.attr,
	&attr_prefetches_total.attr,
	&attr_nmce_ops_total.attr,
	&attr_nmce_bytes_saved.attr,
	&attr_rtbd_capacity.attr,
	&attr_hepc_status.attr,
	&attr_numa_node.attr,
	&attr_hepc_evict_threshold.attr,
	&attr_hepc_prefetch_threshold.attr,
	&attr_hepc_window_w.attr,
	&attr_hepc_weight_r.attr,
	&attr_hepc_weight_f.attr,
	&attr_hepc_weight_s.attr,
	&attr_hepc_weight_d.attr,
	NULL,
};

static const struct attribute_group kvcpu_attr_group = {
	.name  = "kvcpu",
	.attrs = kvcpu_attrs,
};

int kvcpu_sysfs_init(struct kvcpu_dev *kv)
{
	return sysfs_create_group(&kv->pdev->dev.kobj, &kvcpu_attr_group);
}

void kvcpu_sysfs_teardown(struct kvcpu_dev *kv)
{
	sysfs_remove_group(&kv->pdev->dev.kobj, &kvcpu_attr_group);
}
