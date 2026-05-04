/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * kv_cpu_main.c — Driver lifecycle (init, probe, remove)
 */
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/slab.h>
#include "kv_cpu_internal.h"

#define DRIVER_NAME "kv_cpu"

static struct class *kvcpu_class;
static dev_t kvcpu_devt;
static DEFINE_IDA(kvcpu_ida);

static const struct file_operations kvcpu_fops = {
	.owner          = THIS_MODULE,
	.unlocked_ioctl = kvcpu_ioctl,
};

static int kvcpu_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct kvcpu_dev *kv;
	int ret, minor;

	kv = devm_kzalloc(&pdev->dev, sizeof(*kv), GFP_KERNEL);
	if (!kv) return -ENOMEM;

	kv->pdev = pdev;
	kv->dev = &pdev->dev;
	pci_set_drvdata(pdev, kv);

	ret = pcim_enable_device(pdev);
	if (ret) return ret;

	ret = pcim_iomap_regions(pdev, BIT(0), DRIVER_NAME);
	if (ret) return ret;
	kv->bar0 = pcim_iomap_table(pdev)[0];

	minor = ida_alloc(&kvcpu_ida, GFP_KERNEL);
	if (minor < 0) return minor;
	kv->minor = minor;

	cdev_init(&kv->cdev, &kvcpu_fops);
	ret = cdev_add(&kv->cdev, MKDEV(MAJOR(kvcpu_devt), minor), 1);
	if (ret) goto err_ida;

	device_create(kvcpu_class, &pdev->dev, MKDEV(MAJOR(kvcpu_devt), minor),
		      kv, "kvcpu%d", minor);

	dev_info(kv->dev, "KV-CPU Reference Driver initialized (kvcpu%d)\n", minor);
	return 0;

err_ida:
	ida_free(&kvcpu_ida, minor);
	return ret;
}

static void kvcpu_remove(struct pci_dev *pdev)
{
	struct kvcpu_dev *kv = pci_get_drvdata(pdev);
	device_destroy(kvcpu_class, MKDEV(MAJOR(kvcpu_devt), kv->minor));
	cdev_del(&kv->cdev);
	ida_free(&kvcpu_ida, kv->minor);
}

static const struct pci_device_id kvcpu_pci_ids[] = {
	{ PCI_DEVICE(KV_CPU_VENDOR_ID, KV_CPU_DEVICE_ID) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, kvcpu_pci_ids);

static struct pci_driver kvcpu_pci_driver = {
	.name     = DRIVER_NAME,
	.id_table = kvcpu_pci_ids,
	.probe    = kvcpu_probe,
	.remove   = kvcpu_remove,
};

static int __init kvcpu_init(void)
{
	int ret;
	ret = alloc_chrdev_region(&kvcpu_devt, 0, 64, DRIVER_NAME);
	if (ret) return ret;
	kvcpu_class = class_create(DRIVER_NAME);
	return pci_register_driver(&kvcpu_pci_driver);
}

static void __exit kvcpu_exit(void)
{
	pci_unregister_driver(&kvcpu_pci_driver);
	class_destroy(kvcpu_class);
	unregister_chrdev_region(kvcpu_devt, 64);
}

module_init(kvcpu_init);
module_exit(kvcpu_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Manish KL <manishklach@gmail.com>");
MODULE_DESCRIPTION("KV-CPU Reference Control Plane Driver");
