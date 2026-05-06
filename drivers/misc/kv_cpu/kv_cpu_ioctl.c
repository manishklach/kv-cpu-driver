/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * KV-CPU Control Plane Driver - ioctl handling
 *
 * Copyright (C) 2026 Manish KL
 */

#include <linux/uaccess.h>
#include <linux/file.h>
#include "kv_cpu.h"

static int kv_cpu_validate_block(const struct kv_cpu_block_info *block)
{
	if (!block->len)
		return -EINVAL;

	return 0;
}

/**
 * kv_cpu_ioctl - Main ioctl dispatcher
 * @file: Pointer to the open file
 * @cmd: ioctl command number
 * @arg: userspace argument pointer
 *
 * Translates semantic hints into hardware register writes.
 */
long kv_cpu_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct kv_cpu_device *kv = file->private_data;
	struct kv_cpu_step_info step;
	struct kv_cpu_block_info block;
	unsigned long flags;

	if (!kv)
		return -ENODEV;

	switch (cmd) {
	case KV_CPU_STEP_ADVANCE:
		if (copy_from_user(&step, (void __user *)arg, sizeof(step)))
			return -EFAULT;
		
		dev_dbg(kv->dev, "STEP %llu\n", step.step);
		spin_lock_irqsave(&kv->cmd_lock, flags);
		kv_cpu_cmd_step(kv, step.step);
		spin_unlock_irqrestore(&kv->cmd_lock, flags);
		break;

	case KV_CPU_MARK_HOT:
		if (copy_from_user(&block, (void __user *)arg, sizeof(block)))
			return -EFAULT;
		if (kv_cpu_validate_block(&block))
			return -EINVAL;
		
		dev_dbg(kv->dev, "HOT range 0x%llx len %llu\n", block.va, block.len);
		spin_lock_irqsave(&kv->cmd_lock, flags);
		kv_cpu_cmd_hot(kv, block.va, block.len);
		spin_unlock_irqrestore(&kv->cmd_lock, flags);
		break;

	case KV_CPU_EVICT:
		if (copy_from_user(&block, (void __user *)arg, sizeof(block)))
			return -EFAULT;
		if (kv_cpu_validate_block(&block))
			return -EINVAL;
		
		dev_dbg(kv->dev, "EVICT range 0x%llx len %llu\n", block.va, block.len);
		spin_lock_irqsave(&kv->cmd_lock, flags);
		kv_cpu_cmd_evict(kv, block.va, block.len);
		spin_unlock_irqrestore(&kv->cmd_lock, flags);
		break;

	case KV_CPU_PREFETCH:
		if (copy_from_user(&block, (void __user *)arg, sizeof(block)))
			return -EFAULT;
		if (kv_cpu_validate_block(&block))
			return -EINVAL;
		
		dev_dbg(kv->dev, "PREFETCH range 0x%llx len %llu target %llu\n", 
			block.va, block.len, block.target_step);
		spin_lock_irqsave(&kv->cmd_lock, flags);
		kv_cpu_cmd_prefetch(kv, block.va, block.len, block.target_step);
		spin_unlock_irqrestore(&kv->cmd_lock, flags);
		break;

	case KV_CPU_SHARE_PREFIX:
		if (copy_from_user(&block, (void __user *)arg, sizeof(block)))
			return -EFAULT;
		if (kv_cpu_validate_block(&block))
			return -EINVAL;
		
		dev_dbg(kv->dev, "SHARE range 0x%llx len %llu\n", block.va, block.len);
		spin_lock_irqsave(&kv->cmd_lock, flags);
		kv_cpu_cmd_share(kv, block.va, block.len);
		spin_unlock_irqrestore(&kv->cmd_lock, flags);
		break;

	default:
		return -ENOTTY;
	}

	return 0;
}
