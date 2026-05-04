// SPDX-License-Identifier: GPL-2.0-only
/*
 * kv_cpu_rtbd.c — Request-Tagged Block Directory Interface (Pillar III)
 *
 * Provides the kernel-side API for managing the hardware RTBD:
 *
 *   kvcpu_rtbd_share()   — increment ref_count for a shared prefix block
 *   kvcpu_rtbd_release() — decrement ref_count; allows eviction when zero
 *   kvcpu_rtbd_query()   — read an RTBD entry (for sysfs / debug)
 *   kvcpu_rtbd_flush()   — evict all blocks for a terminated request
 *
 * All operations are single MMIO write sequences to the RTBD command
 * registers. The hardware CAM lookup is in the device; the driver just
 * issues commands and reads back results.
 *
 * Author: Manish Keshav Lachwani <mlachwani@gmail.com>
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include "../include/kv_cpu.h"

/* Timeout for RTBD command completion polling (microseconds) */
#define RTBD_CMD_TIMEOUT_US  500

static int rtbd_wait_ready(struct kvcpu_dev *kv)
{
	int i;
	for (i = 0; i < RTBD_CMD_TIMEOUT_US; i++) {
		u64 st = kvcpu_readq(kv, KVCPU_REG_RTBD_STATUS);
		if (st == 0)
			return 0;
		udelay(1);
	}
	dev_err(kv->dev, "RTBD command timeout after %d µs\n",
		RTBD_CMD_TIMEOUT_US);
	return -ETIMEDOUT;
}

int kvcpu_rtbd_share(struct kvcpu_dev *kv, phys_addr_t block_pa, u16 req_id)
{
	int ret;

	kvcpu_writeq(kv, KVCPU_REG_RTBD_BLKADDR, (u64)block_pa);
	kvcpu_writeq(kv, KVCPU_REG_RTBD_REQ_ID,  req_id);
	kvcpu_writeq(kv, KVCPU_REG_RTBD_CMD,      RTBD_CMD_SHARE);

	ret = rtbd_wait_ready(kv);
	if (ret)
		return ret;

	dev_dbg(kv->dev, "RTBD share: pa=0x%llx req=%u\n",
		(u64)block_pa, req_id);
	return 0;
}

int kvcpu_rtbd_release(struct kvcpu_dev *kv, phys_addr_t block_pa, u16 req_id)
{
	int ret;

	kvcpu_writeq(kv, KVCPU_REG_RTBD_BLKADDR, (u64)block_pa);
	kvcpu_writeq(kv, KVCPU_REG_RTBD_REQ_ID,  req_id);
	kvcpu_writeq(kv, KVCPU_REG_RTBD_CMD,      RTBD_CMD_RELEASE);

	ret = rtbd_wait_ready(kv);
	if (ret)
		return ret;

	dev_dbg(kv->dev, "RTBD release: pa=0x%llx req=%u\n",
		(u64)block_pa, req_id);
	return 0;
}

int kvcpu_rtbd_query(struct kvcpu_dev *kv, phys_addr_t block_pa,
		     struct kvcpu_rtbd_entry *out)
{
	int ret;

	kvcpu_writeq(kv, KVCPU_REG_RTBD_BLKADDR, (u64)block_pa);
	kvcpu_writeq(kv, KVCPU_REG_RTBD_CMD,      RTBD_CMD_QUERY);

	ret = rtbd_wait_ready(kv);
	if (ret)
		return ret;

	out->tier_location  = (u8)kvcpu_readq(kv, KVCPU_REG_RTBD_TIER);
	out->priority_score = (u16)kvcpu_readq(kv, KVCPU_REG_RTBD_PRIO);
	out->ref_count      = (u8)kvcpu_readq(kv, KVCPU_REG_RTBD_REFCNT);
	out->access_step    = (u32)kvcpu_readq(kv, KVCPU_REG_RTBD_ACCSTEP);
	out->phys_addr      = (u64)block_pa;

	return 0;
}

int kvcpu_rtbd_flush(struct kvcpu_dev *kv, u16 req_id)
{
	int ret;

	kvcpu_writeq(kv, KVCPU_REG_RTBD_REQ_ID, req_id);
	kvcpu_writeq(kv, KVCPU_REG_RTBD_CMD,     RTBD_CMD_FLUSH);

	ret = rtbd_wait_ready(kv);
	if (ret)
		return ret;

	dev_dbg(kv->dev, "RTBD flush: req=%u\n", req_id);
	return 0;
}
