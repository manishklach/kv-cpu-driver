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

static void kv_cpu_record_block(struct kv_cpu_device *kv,
				struct kv_cpu_block_info *block,
				u64 *counter)
{
	*counter += 1;
	kv->telemetry.last_va = block->va;
	kv->telemetry.last_len = block->len;
	kv->telemetry.last_target_step = block->target_step;
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
	struct kv_cpu_weights_info weights;
	struct kv_cpu_telemetry_info telemetry;
	unsigned long flags;

	if (!kv)
		return -ENODEV;

	switch (cmd) {
	case KV_CPU_STEP_ADVANCE:
		if (copy_from_user(&step, (void __user *)arg, sizeof(step)))
			return -EFAULT;
		
		dev_dbg(kv->dev, "STEP %llu\n", step.step);
		spin_lock_irqsave(&kv->cmd_lock, flags);
		kv->telemetry.current_step = step.step;
		kv->telemetry.step_count += 1;
		kv->telemetry.last_target_step = step.step;
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
		kv_cpu_record_block(kv, &block, &kv->telemetry.hot_count);
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
		kv_cpu_record_block(kv, &block, &kv->telemetry.evict_count);
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
		kv_cpu_record_block(kv, &block, &kv->telemetry.prefetch_count);
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
		kv_cpu_record_block(kv, &block, &kv->telemetry.share_count);
		kv_cpu_cmd_share(kv, block.va, block.len);
		spin_unlock_irqrestore(&kv->cmd_lock, flags);
		break;

	case KV_CPU_SET_WEIGHTS:
		if (copy_from_user(&weights, (void __user *)arg, sizeof(weights)))
			return -EFAULT;

		dev_dbg(kv->dev,
			"WEIGHTS r=%u f=%u s=%u d=%u evict=%u prefetch=%u\n",
			weights.w_r, weights.w_f, weights.w_s, weights.w_d,
			weights.evict_thresh, weights.prefetch_thresh);
		spin_lock_irqsave(&kv->cmd_lock, flags);
		kv->runtime.w_r = weights.w_r;
		kv->runtime.w_f = weights.w_f;
		kv->runtime.w_s = weights.w_s;
		kv->runtime.w_d = weights.w_d;
		kv->runtime.evict_thresh = weights.evict_thresh;
		kv->runtime.prefetch_thresh = weights.prefetch_thresh;
		kv_cpu_cmd_set_weights(kv, &weights);
		spin_unlock_irqrestore(&kv->cmd_lock, flags);
		break;

	case KV_CPU_GET_TELEMETRY:
		spin_lock_irqsave(&kv->cmd_lock, flags);
		telemetry.current_step = kv->telemetry.current_step;
		telemetry.step_count = kv->telemetry.step_count;
		telemetry.hot_count = kv->telemetry.hot_count;
		telemetry.evict_count = kv->telemetry.evict_count;
		telemetry.prefetch_count = kv->telemetry.prefetch_count;
		telemetry.share_count = kv->telemetry.share_count;
		telemetry.last_va = kv->telemetry.last_va;
		telemetry.last_len = kv->telemetry.last_len;
		telemetry.last_target_step = kv->telemetry.last_target_step;
		telemetry.w_r = kv->runtime.w_r;
		telemetry.w_f = kv->runtime.w_f;
		telemetry.w_s = kv->runtime.w_s;
		telemetry.w_d = kv->runtime.w_d;
		telemetry.evict_thresh = kv->runtime.evict_thresh;
		telemetry.prefetch_thresh = kv->runtime.prefetch_thresh;
		spin_unlock_irqrestore(&kv->cmd_lock, flags);

		if (copy_to_user((void __user *)arg, &telemetry, sizeof(telemetry)))
			return -EFAULT;
		break;

	default:
		return -ENOTTY;
	}

	return 0;
}
