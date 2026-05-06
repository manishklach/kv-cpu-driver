/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * KV-CPU control plane driver - Internal Header
 *
 * Copyright (C) 2026 Manish KL
 */

#ifndef _KV_CPU_H_
#define _KV_CPU_H_

#include <linux/pci.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/spinlock.h>
#include "../../../include/uapi/linux/kv_cpu.h"

#define DRIVER_NAME "kv_cpu"

/* BAR0 Register Offsets */
#define KVCPU_REG_STEP_ADVANCE  0x0100
#define KVCPU_REG_BOOST_ADDR    0x0200
#define KVCPU_REG_BOOST_LEN     0x0208
#define KVCPU_REG_IMEVICT_ADDR  0x0210
#define KVCPU_REG_IMEVICT_LEN   0x0218
#define KVCPU_REG_PREFETCH_ADDR 0x0300
#define KVCPU_REG_PREFETCH_LEN  0x0308
#define KVCPU_REG_PREFETCH_STEP 0x0310
#define KVCPU_REG_SHARE_ADDR    0x0400
#define KVCPU_REG_SHARE_LEN     0x0408

/**
 * struct kv_cpu_device - Main driver state
 * @pdev: Pointer to the PCI device (NULL in mock mode)
 * @bar0: MMIO base for BAR0
 * @cdev: Character device structure
 * @dev: Pointer to the device created in sysfs
 * @is_mock: True if running without hardware
 * @mock_bar: Virtual memory used for MMIO in mock mode
 * @cmd_lock: Serializes command submission to the MMIO window
 */
struct kv_cpu_device {
	struct pci_dev		*pdev;
	void __iomem		*bar0;
	struct cdev		cdev;
	struct device		*dev;
	bool			is_mock;
	void			*mock_bar;
	spinlock_t		cmd_lock;
};

/* MMIO register access helpers */
void kv_cpu_write_reg(struct kv_cpu_device *kv, u32 offset, u64 val);

/* Command execution hooks */
void kv_cpu_cmd_step(struct kv_cpu_device *kv, u64 step);
void kv_cpu_cmd_hot(struct kv_cpu_device *kv, u64 va, u64 len);
void kv_cpu_cmd_evict(struct kv_cpu_device *kv, u64 va, u64 len);
void kv_cpu_cmd_prefetch(struct kv_cpu_device *kv, u64 va, u64 len, u64 step);
void kv_cpu_cmd_share(struct kv_cpu_device *kv, u64 va, u64 len);

/* ioctl dispatcher */
long kv_cpu_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

#endif /* _KV_CPU_H_ */
