/* SPDX-License-Identifier: GPL-2.0-only */
#include <linux/uaccess.h>
#include "kv_cpu_internal.h"

long kvcpu_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct kvcpu_dev *kv = file->private_data;
	struct kv_cpu_step_info step;
	struct kv_cpu_block_info block;

	if (!kv)
		return -ENODEV;

	switch (cmd) {
	case KV_CPU_STEP_ADVANCE:
		if (copy_from_user(&step, (void __user *)arg, sizeof(step)))
			return -EFAULT;
		pr_info("kv_cpu: STEP %llu\n", step.step);
		kv_cpu_cmd_step(kv, step.step);
		break;

	case KV_CPU_MARK_HOT:
		if (copy_from_user(&block, (void __user *)arg, sizeof(block)))
			return -EFAULT;
		pr_info("kv_cpu: HOT range 0x%llx len %llu\n", block.va, block.len);
		kv_cpu_cmd_hot(kv, block.va, block.len);
		break;

	case KV_CPU_EVICT:
		if (copy_from_user(&block, (void __user *)arg, sizeof(block)))
			return -EFAULT;
		pr_info("kv_cpu: EVICT range 0x%llx len %llu\n", block.va, block.len);
		kv_cpu_cmd_evict(kv, block.va, block.len);
		break;

	case KV_CPU_PREFETCH:
		if (copy_from_user(&block, (void __user *)arg, sizeof(block)))
			return -EFAULT;
		pr_info("kv_cpu: PREFETCH range 0x%llx len %llu (target step %llu)\n", 
			block.va, block.len, block.target_step);
		kv_cpu_cmd_prefetch(kv, block.va, block.len, block.target_step);
		break;

	case KV_CPU_SHARE_PREFIX:
		if (copy_from_user(&block, (void __user *)arg, sizeof(block)))
			return -EFAULT;
		pr_info("kv_cpu: SHARE_PREFIX range 0x%llx len %llu\n", block.va, block.len);
		kv_cpu_cmd_share(kv, block.va, block.len);
		break;

	default:
		return -ENOTTY;
	}

	return 0;
}
