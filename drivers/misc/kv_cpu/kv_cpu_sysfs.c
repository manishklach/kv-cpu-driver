/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * KV-CPU Control Plane Driver - sysfs interface
 *
 * Copyright (C) 2026 Manish KL
 */

#include <linux/kernel.h>
#include <linux/sysfs.h>
#include "kv_cpu.h"

static ssize_t current_step_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct kv_cpu_device *kv = dev_get_drvdata(dev);
	unsigned long flags;
	u64 value;

	spin_lock_irqsave(&kv->cmd_lock, flags);
	value = kv->telemetry.current_step;
	spin_unlock_irqrestore(&kv->cmd_lock, flags);

	return sysfs_emit(buf, "%llu\n", value);
}

static ssize_t kv_cpu_u32_show(struct device *dev, size_t offset, char *buf)
{
	struct kv_cpu_device *kv = dev_get_drvdata(dev);
	unsigned long flags;
	u32 value;

	spin_lock_irqsave(&kv->cmd_lock, flags);
	value = *(u32 *)((char *)&kv->runtime + offset);
	spin_unlock_irqrestore(&kv->cmd_lock, flags);

	return sysfs_emit(buf, "%u\n", value);
}

static ssize_t kv_cpu_u32_store(struct device *dev, size_t offset,
				const char *buf, size_t count)
{
	struct kv_cpu_device *kv = dev_get_drvdata(dev);
	struct kv_cpu_weights_info weights;
	unsigned long flags;
	u32 value;
	int ret;

	ret = kstrtou32(buf, 0, &value);
	if (ret)
		return ret;

	spin_lock_irqsave(&kv->cmd_lock, flags);
	*(u32 *)((char *)&kv->runtime + offset) = value;
	weights.w_r = kv->runtime.w_r;
	weights.w_f = kv->runtime.w_f;
	weights.w_s = kv->runtime.w_s;
	weights.w_d = kv->runtime.w_d;
	weights.evict_thresh = kv->runtime.evict_thresh;
	weights.prefetch_thresh = kv->runtime.prefetch_thresh;
	kv_cpu_cmd_set_weights(kv, &weights);
	spin_unlock_irqrestore(&kv->cmd_lock, flags);

	return count;
}

#define KV_CPU_RUNTIME_ATTR_RW(_name, _field)					\
static ssize_t _name##_show(struct device *dev,				\
			    struct device_attribute *attr, char *buf)		\
{									\
	return kv_cpu_u32_show(dev, offsetof(struct kv_cpu_runtime_state,	\
					     _field), buf);		\
}									\
static ssize_t _name##_store(struct device *dev,				\
			     struct device_attribute *attr,			\
			     const char *buf, size_t count)			\
{									\
	return kv_cpu_u32_store(dev, offsetof(struct kv_cpu_runtime_state,	\
					      _field), buf, count);	\
}									\
static DEVICE_ATTR_RW(_name)

static ssize_t telemetry_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct kv_cpu_device *kv = dev_get_drvdata(dev);
	struct kv_cpu_telemetry_state telemetry;
	struct kv_cpu_runtime_state runtime;
	unsigned long flags;

	spin_lock_irqsave(&kv->cmd_lock, flags);
	telemetry = kv->telemetry;
	runtime = kv->runtime;
	spin_unlock_irqrestore(&kv->cmd_lock, flags);

	return sysfs_emit(buf,
			  "current_step=%llu\n"
			  "step_count=%llu\n"
			  "hot_count=%llu\n"
			  "evict_count=%llu\n"
			  "prefetch_count=%llu\n"
			  "share_count=%llu\n"
			  "last_va=0x%llx\n"
			  "last_len=%llu\n"
			  "last_target_step=%llu\n"
			  "w_r=%u\n"
			  "w_f=%u\n"
			  "w_s=%u\n"
			  "w_d=%u\n"
			  "evict_thresh=%u\n"
			  "prefetch_thresh=%u\n",
			  telemetry.current_step,
			  telemetry.step_count,
			  telemetry.hot_count,
			  telemetry.evict_count,
			  telemetry.prefetch_count,
			  telemetry.share_count,
			  telemetry.last_va,
			  telemetry.last_len,
			  telemetry.last_target_step,
			  runtime.w_r,
			  runtime.w_f,
			  runtime.w_s,
			  runtime.w_d,
			  runtime.evict_thresh,
			  runtime.prefetch_thresh);
}

static DEVICE_ATTR_RO(current_step);
KV_CPU_RUNTIME_ATTR_RW(w_r, w_r);
KV_CPU_RUNTIME_ATTR_RW(w_f, w_f);
KV_CPU_RUNTIME_ATTR_RW(w_s, w_s);
KV_CPU_RUNTIME_ATTR_RW(w_d, w_d);
KV_CPU_RUNTIME_ATTR_RW(evict_thresh, evict_thresh);
KV_CPU_RUNTIME_ATTR_RW(prefetch_thresh, prefetch_thresh);
static DEVICE_ATTR_RO(telemetry);

static struct attribute *kv_cpu_attrs[] = {
	&dev_attr_current_step.attr,
	&dev_attr_w_r.attr,
	&dev_attr_w_f.attr,
	&dev_attr_w_s.attr,
	&dev_attr_w_d.attr,
	&dev_attr_evict_thresh.attr,
	&dev_attr_prefetch_thresh.attr,
	&dev_attr_telemetry.attr,
	NULL,
};

static const struct attribute_group kv_cpu_attr_group = {
	.attrs = kv_cpu_attrs,
};

int kv_cpu_sysfs_create(struct kv_cpu_device *kv)
{
	return sysfs_create_group(&kv->dev->kobj, &kv_cpu_attr_group);
}

void kv_cpu_sysfs_remove(struct kv_cpu_device *kv)
{
	sysfs_remove_group(&kv->dev->kobj, &kv_cpu_attr_group);
}
