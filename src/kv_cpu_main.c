// SPDX-License-Identifier: GPL-2.0-only
/*
 * kv_cpu_main.c — KV-CPU CXL Device Driver: PCI probe / remove / IRQ
 *
 * Handles:
 *   - PCI device enumeration and BAR0 MMIO mapping
 *   - MSI-X interrupt setup
 *   - Coordinated init/teardown of all subsystems
 *   - Character device for userspace ioctl interface
 *
 * Author: Manish Keshav Lachwani <mlachwani@gmail.com>
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include "../include/kv_cpu.h"
#include "kv_cpu_ioctl.h"

#define DRIVER_NAME     "kv_cpu"
#define DRIVER_VERSION  "0.1.0"

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Manish Keshav Lachwani <mlachwani@gmail.com>");
MODULE_DESCRIPTION("KV-Cache Companion Processing Unit (KV-CPU) driver");
MODULE_VERSION(DRIVER_VERSION);

static bool mock = false;
module_param(mock, bool, 0444);
MODULE_PARM_DESC(mock, "Enable hardware emulation mode (no physical device required)");

/* ── Global device class ─────────────────────────────────────────────────── */
static struct class   *kvcpu_class;
static dev_t           kvcpu_devt;
static DEFINE_IDA(kvcpu_ida);

/* For mock mode tracking */
static struct kvcpu_dev *mock_kv_dev;

/* ── PCI ID table ────────────────────────────────────────────────────────── */
static const struct pci_device_id kvcpu_pci_ids[] = {
	{ PCI_DEVICE_SUB(KV_CPU_VENDOR_ID, KV_CPU_DEVICE_ID,
			 KV_CPU_SUBSYS_VENDOR, KV_CPU_SUBSYS_ID) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, kvcpu_pci_ids);

/* ── IRQ handler ─────────────────────────────────────────────────────────── */
static irqreturn_t kvcpu_irq_handler(int irq, void *data)
{
	struct kvcpu_dev *kv = data;
	u64 status;

	status = kvcpu_readq(kv, KVCPU_REG_IRQ_STATUS);
	if (!status)
		return IRQ_NONE;

	/* Clear interrupts (write-1-to-clear) */
	kvcpu_writeq(kv, KVCPU_REG_IRQ_STATUS, status);

	if (status & KVCPU_IRQ_EVICT_DONE) {
		kv->stat_evictions++;
		dev_dbg(kv->dev, "eviction DMA complete (total=%llu)\n",
			kv->stat_evictions);
	}

	if (status & KVCPU_IRQ_PREFETCH_DN) {
		kv->stat_prefetches++;
		dev_dbg(kv->dev, "prefetch DMA complete (total=%llu)\n",
			kv->stat_prefetches);
	}

	if (status & KVCPU_IRQ_NMCE_CQ) {
		/* NMCE completion queue has new entries — wake any waiters */
		kv->stat_nmce_ops++;
		/* In a full driver, we'd call the CQ consumer here */
	}

	if (status & KVCPU_IRQ_ERROR) {
		dev_err(kv->dev, "hardware error interrupt — status=0x%llx\n",
			status);
		/* TODO: trigger device reset / error recovery */
	}

	return IRQ_HANDLED;
}

/* ── Hardware identity check ─────────────────────────────────────────────── */
static int kvcpu_check_identity(struct kvcpu_dev *kv)
{
	u64 ident   = kvcpu_readq(kv, KVCPU_REG_IDENT);
	u64 version = kvcpu_readq(kv, KVCPU_REG_VERSION);
	u64 cap     = kvcpu_readq(kv, KVCPU_REG_CAP);

	if (ident != 0x4B564350555F4350ULL) {  /* "KVCPU_CP" */
		dev_err(kv->dev,
			"identity mismatch: got 0x%llx, expected 0x%llx\n",
			ident, 0x4B564350555F4350ULL);
		return -ENODEV;
	}

	dev_info(kv->dev,
		 "KV-CPU v%llu.%llu detected | caps: NMCE=%d HEPC=%d RTBD=%d PREFIX=%d IORING=%d\n",
		 (version >> 16) & 0xFFFF, version & 0xFFFF,
		 !!(cap & KVCPU_CAP_NMCE),
		 !!(cap & KVCPU_CAP_HEPC),
		 !!(cap & KVCPU_CAP_RTBD),
		 !!(cap & KVCPU_CAP_PREFIX),
		 !!(cap & KVCPU_CAP_IORING));

	/* Log T1 memory geometry */
	dev_info(kv->dev,
		 "T1 LPDDR5X: base=0x%llx size=%llu GiB\n",
		 kvcpu_readq(kv, KVCPU_REG_MEM_BASE),
		 kvcpu_readq(kv, KVCPU_REG_MEM_SIZE) >> 30);

	return 0;
}

/* ── char device file ops ────────────────────────────────────────────────── */
static long kvcpu_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct kvcpu_dev *kv = file->private_data;

	switch (cmd) {
	case KVCPU_IOC_STEP_ADVANCE: {
		u64 step;
		if (copy_from_user(&step, (void __user *)arg, sizeof(step)))
			return -EFAULT;
		kvcpu_hepc_step_advance(kv, step);
		return 0;
	}
	case KVCPU_IOC_GET_TELEMETRY: {
		struct kvcpu_telemetry tel = {
			.evictions  = kvcpu_readq(kv, KVCPU_REG_EVICT_COUNT),
			.prefetches = kvcpu_readq(kv, KVCPU_REG_PREFETCH_CNT),
			.nmce_ops   = kvcpu_readq(kv, KVCPU_REG_NMCE_OPS),
			.t1_used    = kvcpu_readq(kv, KVCPU_REG_T1_USED),
			.t1_free    = kvcpu_readq(kv, KVCPU_REG_T1_FREE),
			.nmce_bytes_in  = kvcpu_readq(kv, KVCPU_REG_NMCE_BYTES_IN),
			.nmce_bytes_out = kvcpu_readq(kv, KVCPU_REG_NMCE_BYTES_OUT),
		};
		if (copy_to_user((void __user *)arg, &tel, sizeof(tel)))
			return -EFAULT;
		return 0;
	}
	case KVCPU_IOC_RTBD_SHARE: {
		struct kvcpu_rtbd_cmd cmd_arg;
		if (copy_from_user(&cmd_arg, (void __user *)arg, sizeof(cmd_arg)))
			return -EFAULT;
		return kvcpu_rtbd_share(kv, cmd_arg.block_pa, cmd_arg.req_id);
	}
	case KVCPU_IOC_RTBD_RELEASE: {
		struct kvcpu_rtbd_cmd cmd_arg;
		if (copy_from_user(&cmd_arg, (void __user *)arg, sizeof(cmd_arg)))
			return -EFAULT;
		return kvcpu_rtbd_release(kv, cmd_arg.block_pa, cmd_arg.req_id);
	}
	case KVCPU_IOC_RTBD_QUERY: {
		struct kvcpu_rtbd_query_arg qa;
		struct kvcpu_rtbd_entry entry;
		int ret;
		if (copy_from_user(&qa, (void __user *)arg, sizeof(qa)))
			return -EFAULT;
		ret = kvcpu_rtbd_query(kv, qa.block_pa, &entry);
		if (ret)
			return ret;
		if (copy_to_user((void __user *)qa.entry_out, &entry, sizeof(entry)))
			return -EFAULT;
		return 0;
	}
	case KVCPU_IOC_SET_WEIGHTS: {
		struct kvcpu_hepc_config cfg;
		if (copy_from_user(&cfg, (void __user *)arg, sizeof(cfg)))
			return -EFAULT;
		return kvcpu_hepc_set_weights(kv, &cfg);
	}
	case KVCPU_IOC_SUBMIT_NMCE: {
		struct kvcpu_sqe sqe;
		if (copy_from_user(&sqe, (void __user *)arg, sizeof(sqe)))
			return -EFAULT;
		return kvcpu_nmce_submit(kv, &sqe);
	}
	default:
		return -ENOTTY;
	}
}

static int kvcpu_open(struct inode *inode, struct file *file)
{
	struct kvcpu_dev *kv =
		container_of(inode->i_cdev, struct kvcpu_dev, cdev);
	file->private_data = kv;
	return 0;
}

static int kvcpu_release(struct inode *inode, struct file *file)
{
	return 0;
}

/*
 * mmap: let userspace map BAR0 registers directly for low-latency
 * step-advance writes (avoids ioctl overhead on the hot path).
 * We only expose the HEPC control register window (offsets 0x100–0x1FF).
 */
static int kvcpu_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct kvcpu_dev *kv = file->private_data;
	unsigned long size   = vma->vm_end - vma->vm_start;
	unsigned long off    = vma->vm_pgoff << PAGE_SHIFT;

	/* Only allow mapping of the HEPC control window */
	if (off < 0x100 || off + size > 0x200) {
		dev_warn(kv->dev, "mmap: only HEPC window (0x100-0x1FF) is mappable\n");
		return -EPERM;
	}

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	return io_remap_pfn_range(vma, vma->vm_start,
				  (pci_resource_start(kv->pdev, 0) + off) >> PAGE_SHIFT,
				  size, vma->vm_page_prot);
}

static const struct file_operations kvcpu_fops = {
	.owner          = THIS_MODULE,
	.open           = kvcpu_open,
	.release        = kvcpu_release,
	.unlocked_ioctl = kvcpu_ioctl,
	.mmap           = kvcpu_mmap,
};

/* ── PCI probe ───────────────────────────────────────────────────────────── */
static int kvcpu_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct kvcpu_dev *kv;
	int ret, minor;

	kv = devm_kzalloc(&pdev->dev, sizeof(*kv), GFP_KERNEL);
	if (!kv)
		return -ENOMEM;

	kv->pdev = pdev;
	kv->dev  = &pdev->dev;
	pci_set_drvdata(pdev, kv);

	/* Enable PCI device */
	ret = pcim_enable_device(pdev);
	if (ret) {
		dev_err(kv->dev, "pcim_enable_device failed: %d\n", ret);
		return ret;
	}
	pci_set_master(pdev);

	/* Set 64-bit DMA mask */
	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (ret) {
		dev_err(kv->dev, "64-bit DMA not supported: %d\n", ret);
		return ret;
	}

	/* Map BAR0 */
	ret = pcim_iomap_regions(pdev, BIT(0), DRIVER_NAME);
	if (ret) {
		dev_err(kv->dev, "BAR0 iomap failed: %d\n", ret);
		return ret;
	}
	kv->bar = pcim_iomap_table(pdev)[0];

	/* Validate hardware identity */
	ret = kvcpu_check_identity(kv);
	if (ret)
		return ret;

	/* Set up MSI-X (single vector for now; expand for per-queue vectors) */
	ret = pci_alloc_irq_vectors(pdev, 1, 4, PCI_IRQ_MSIX | PCI_IRQ_MSI);
	if (ret < 0) {
		dev_warn(kv->dev, "MSI-X/MSI unavailable (%d), falling back to INTx\n", ret);
		kv->irq = pdev->irq;
	} else {
		kv->irq = pci_irq_vector(pdev, 0);
	}

	ret = devm_request_irq(&pdev->dev, kv->irq, kvcpu_irq_handler,
			       IRQF_SHARED, DRIVER_NAME, kv);
	if (ret) {
		dev_err(kv->dev, "request_irq failed: %d\n", ret);
		goto err_irq;
	}

	/* Enable all interrupts */
	kvcpu_writeq(kv, KVCPU_REG_IRQ_MASK,
		     KVCPU_IRQ_EVICT_DONE | KVCPU_IRQ_PREFETCH_DN |
		     KVCPU_IRQ_NMCE_CQ   | KVCPU_IRQ_ERROR);

	/* ── Subsystem init (order matters) ────────────────────────────────── */

	/* 1. Register T1 memory as NUMA tier */
	ret = kvcpu_mem_register(kv);
	if (ret) {
		dev_err(kv->dev, "memory tier registration failed: %d\n", ret);
		goto err_mem;
	}

	/* 2. HEPC: set default policy weights */
	kv->hepc = (struct kvcpu_hepc_config){
		.evict_threshold   = 10,
		.prefetch_threshold = 180,
		.window_w          = 128,
		.weight_r = 50, .weight_f = 30,
		.weight_s = 20, .weight_d = 200,
	};
	ret = kvcpu_hepc_init(kv);
	if (ret) {
		dev_err(kv->dev, "HEPC init failed: %d\n", ret);
		goto err_hepc;
	}

	/* 3. NMCE: set up submission / completion queues */
	ret = kvcpu_nmce_init(kv);
	if (ret) {
		dev_err(kv->dev, "NMCE init failed: %d\n", ret);
		goto err_nmce;
	}

	/* 4. madvise extensions */
	ret = kvcpu_madvise_register(kv);
	if (ret) {
		dev_warn(kv->dev, "madvise extensions unavailable: %d\n", ret);
		/* Non-fatal — driver still useful without madvise */
	}

	/* 5. io_uring opcodes */
	ret = kvcpu_ioring_register(kv);
	if (ret) {
		dev_warn(kv->dev, "io_uring opcode registration unavailable: %d\n", ret);
		/* Non-fatal */
	}

	/* 6. Char device */
	minor = ida_alloc(&kvcpu_ida, GFP_KERNEL);
	if (minor < 0) {
		ret = minor;
		goto err_cdev;
	}

	cdev_init(&kv->cdev, &kvcpu_fops);
	kv->cdev.owner = THIS_MODULE;
	ret = cdev_add(&kv->cdev, MKDEV(MAJOR(kvcpu_devt), minor), 1);
	if (ret)
		goto err_cdev_add;

	kv->dev = device_create(kvcpu_class, &pdev->dev,
				MKDEV(MAJOR(kvcpu_devt), minor),
				kv, "kvcpu%d", minor);
	if (IS_ERR(kv->dev)) {
		ret = PTR_ERR(kv->dev);
		goto err_dev_create;
	}

	/* 7. sysfs telemetry nodes */
	ret = kvcpu_sysfs_init(kv);
	if (ret)
		dev_warn(&pdev->dev, "sysfs init failed (non-fatal): %d\n", ret);

	dev_info(&pdev->dev,
		 "KV-CPU ready | T1=%llu GiB | RTBD_CAP=%llu | "
		 "NMCE=%s HEPC=%s IORING=%s\n",
		 kvcpu_readq(kv, KVCPU_REG_MEM_SIZE) >> 30,
		 kvcpu_readq(kv, KVCPU_REG_RTBD_CAP),
		 (kvcpu_readq(kv, KVCPU_REG_CAP) & KVCPU_CAP_NMCE) ? "yes" : "no",
		 (kvcpu_readq(kv, KVCPU_REG_CAP) & KVCPU_CAP_HEPC) ? "yes" : "no",
		 kv->ioring_registered ? "yes" : "no");

	return 0;

err_dev_create:
	cdev_del(&kv->cdev);
err_cdev_add:
	ida_free(&kvcpu_ida, minor);
err_cdev:
	kvcpu_ioring_unregister(kv);
	kvcpu_madvise_unregister(kv);
	kvcpu_nmce_teardown(kv);
err_nmce:
err_hepc:
	kvcpu_mem_unregister(kv);
err_mem:
err_irq:
	pci_free_irq_vectors(pdev);
	return ret;
}

static void kvcpu_remove(struct pci_dev *pdev)
{
	struct kvcpu_dev *kv = pci_get_drvdata(pdev);
	int minor = MINOR(kv->cdev.dev);

	dev_info(kv->dev, "removing KV-CPU device\n");

	kvcpu_sysfs_teardown(kv);
	device_destroy(kvcpu_class, MKDEV(MAJOR(kvcpu_devt), minor));
	cdev_del(&kv->cdev);
	ida_free(&kvcpu_ida, minor);

	kvcpu_ioring_unregister(kv);
	kvcpu_madvise_unregister(kv);
	kvcpu_nmce_teardown(kv);
	kvcpu_mem_unregister(kv);

	/* Disable interrupts */
	kvcpu_writeq(kv, KVCPU_REG_IRQ_MASK, 0);
	pci_free_irq_vectors(pdev);
}

static struct pci_driver kvcpu_pci_driver = {
	.name     = DRIVER_NAME,
	.id_table = kvcpu_pci_ids,
	.probe    = kvcpu_probe,
	.remove   = kvcpu_remove,
};

/* ── Mock Device Initialization ────────────────────────────────────────── */
static int kvcpu_mock_probe(void)
{
	struct kvcpu_dev *kv;
	int ret, minor;

	pr_info("kv_cpu: initializing hardware emulation (mock mode)\n");

	kv = kzalloc(sizeof(*kv), GFP_KERNEL);
	if (!kv)
		return -ENOMEM;

	kv->is_mock = true;
	kv->mock_bar_mem = (void *)get_zeroed_page(GFP_KERNEL);
	if (!kv->mock_bar_mem) {
		ret = -ENOMEM;
		goto err_bar;
	}

	/* Initialize mock registers with identity and caps */
	*(u64 *)(kv->mock_bar_mem + KVCPU_REG_IDENT)   = 0x4B564350555F4350ULL;
	*(u64 *)(kv->mock_bar_mem + KVCPU_REG_VERSION) = 0x00010002ULL; /* v1.2 */
	*(u64 *)(kv->mock_bar_mem + KVCPU_REG_CAP)     = KVCPU_CAP_NMCE | KVCPU_CAP_HEPC | 
	                                                 KVCPU_CAP_RTBD | KVCPU_CAP_PREFIX;
	*(u64 *)(kv->mock_bar_mem + KVCPU_REG_MEM_SIZE) = 4ULL << 30; /* 4 GiB */
	*(u64 *)(kv->mock_bar_mem + KVCPU_REG_RTBD_CAP) = 65536;

	/* In mock mode, we use a vmalloc region for T1 memory */
	kv->mock_t1_mem = vmalloc(4ULL << 30);
	if (!kv->mock_t1_mem) {
		ret = -ENOMEM;
		goto err_t1;
	}

	/* ── Subsystem init ────────────────────────────────────────────────── */
	/* Note: kvcpu_mem_register will be updated to handle mock mode */
	ret = kvcpu_mem_register(kv);
	if (ret) goto err_mem;

	ret = kvcpu_hepc_init(kv);
	if (ret) goto err_hepc;

	ret = kvcpu_nmce_init(kv);
	if (ret) goto err_nmce;

	/* Char device */
	minor = ida_alloc(&kvcpu_ida, GFP_KERNEL);
	if (minor < 0) {
		ret = minor;
		goto err_cdev;
	}

	cdev_init(&kv->cdev, &kvcpu_fops);
	kv->cdev.owner = THIS_MODULE;
	ret = cdev_add(&kv->cdev, MKDEV(MAJOR(kvcpu_devt), minor), 1);
	if (ret) goto err_cdev_add;

	kv->dev = device_create(kvcpu_class, NULL, MKDEV(MAJOR(kvcpu_devt), minor),
				kv, "kvcpu%d", minor);
	if (IS_ERR(kv->dev)) {
		ret = PTR_ERR(kv->dev);
		goto err_dev_create;
	}

	ret = kvcpu_sysfs_init(kv);
	if (ret) dev_warn(kv->dev, "sysfs init failed: %d\n", ret);

	/* Store for cleanup */
	mock_kv_dev = kv;

	dev_info(kv->dev, "KV-CPU Mock Device ready | T1=4 GiB (Emulated)\n");
	return 0;

err_dev_create:
	cdev_del(&kv->cdev);
err_cdev_add:
	ida_free(&kvcpu_ida, minor);
err_cdev:
	kvcpu_nmce_teardown(kv);
err_nmce:
err_hepc:
	kvcpu_mem_unregister(kv);
err_mem:
	vfree(kv->mock_t1_mem);
err_t1:
	free_page((unsigned long)kv->mock_bar_mem);
err_bar:
	kfree(kv);
	return ret;
}

static void kvcpu_mock_remove(struct kvcpu_dev *kv)
{
	int minor = MINOR(kv->cdev.dev);

	kvcpu_sysfs_teardown(kv);
	device_destroy(kvcpu_class, MKDEV(MAJOR(kvcpu_devt), minor));
	cdev_del(&kv->cdev);
	ida_free(&kvcpu_ida, minor);

	kvcpu_nmce_teardown(kv);
	kvcpu_mem_unregister(kv);

	vfree(kv->mock_t1_mem);
	free_page((unsigned long)kv->mock_bar_mem);
	kfree(kv);
}

/* ── Module init / exit ──────────────────────────────────────────────────── */
static int __init kvcpu_init(void)
{
	int ret;

	ret = alloc_chrdev_region(&kvcpu_devt, 0, 64, DRIVER_NAME);
	if (ret) {
		pr_err("kv_cpu: failed to alloc chrdev region: %d\n", ret);
		return ret;
	}

	kvcpu_class = class_create(DRIVER_NAME);
	if (IS_ERR(kvcpu_class)) {
		ret = PTR_ERR(kvcpu_class);
		unregister_chrdev_region(kvcpu_devt, 64);
		return ret;
	}

	if (mock) {
		ret = kvcpu_mock_probe();
		if (ret) {
			class_destroy(kvcpu_class);
			unregister_chrdev_region(kvcpu_devt, 64);
			return ret;
		}
	} else {
		ret = pci_register_driver(&kvcpu_pci_driver);
		if (ret) {
			class_destroy(kvcpu_class);
			unregister_chrdev_region(kvcpu_devt, 64);
			return ret;
		}
	}

	pr_info("kv_cpu: driver loaded v%s (%s mode)\n", 
		DRIVER_VERSION, mock ? "emulation" : "hardware");
	return 0;
}

static void __exit kvcpu_exit(void)
{
	if (mock) {
		if (mock_kv_dev) kvcpu_mock_remove(mock_kv_dev);
	} else {
		pci_unregister_driver(&kvcpu_pci_driver);
	}
	class_destroy(kvcpu_class);
	unregister_chrdev_region(kvcpu_devt, 64);
	pr_info("kv_cpu: driver unloaded\n");
}

module_init(kvcpu_init);
module_exit(kvcpu_exit);
