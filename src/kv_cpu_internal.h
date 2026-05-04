/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _KV_CPU_INTERNAL_H
#define _KV_CPU_INTERNAL_H

#include <linux/pci.h>
#include <linux/cdev.h>
#include "../include/uapi/linux/kv_cpu.h"

/* ── BAR0 register offsets (Reference Model) ────────────────────────────── */
#define KVCPU_REG_IDENT         0x0000   /* RO: "KVCPU_CP" identity                 */
#define KVCPU_REG_STEP_ADVANCE  0x0100   /* WO: signal current decode step t        */
#define KVCPU_REG_BOOST_ADDR    0x0200   /* WO: phys addr for priority boost        */
#define KVCPU_REG_BOOST_LEN     0x0208   /* WO: length of boost range               */
#define KVCPU_REG_IMEVICT_ADDR  0x0210   /* WO: phys addr for immediate eviction    */
#define KVCPU_REG_IMEVICT_LEN   0x0218   /* WO: length for immediate eviction       */

struct kvcpu_dev {
	struct pci_dev    *pdev;
	void __iomem      *bar0;
	struct cdev        cdev;
	struct device     *dev;
	int                minor;
	bool               is_mock;
	void              *mock_bar; /* Fake MMIO memory */
};

/* Internal function prototypes */
int  kvcpu_open(struct inode *inode, struct file *file);
long kvcpu_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

/* MMIO Helpers */
void kv_cpu_write_reg(struct kvcpu_dev *kv, u32 offset, u64 val);
void kv_cpu_cmd_step(struct kvcpu_dev *kv, u64 step);
void kv_cpu_cmd_hot(struct kvcpu_dev *kv, u64 va, u64 len);
void kv_cpu_cmd_evict(struct kvcpu_dev *kv, u64 va, u64 len);
void kv_cpu_cmd_prefetch(struct kvcpu_dev *kv, u64 va, u64 len, u64 step);
void kv_cpu_cmd_share(struct kvcpu_dev *kv, u64 va, u64 len);

#endif /* _KV_CPU_INTERNAL_H */
