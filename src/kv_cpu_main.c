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

static bool mock = false;
module_param(mock, bool, 0444);
MODULE_PARM_DESC(mock, "Enable mock mode (software-only emulation)");

static struct kvcpu_dev *mock_dev;
static struct class *kvcpu_class;
static dev_t kvcpu_devt;
static DEFINE_IDA(kvcpu_ida);

int kvcpu_open(struct inode *inode, struct file *file)
{
	struct kvcpu_dev *kv = container_of(inode->i_cdev, struct kvcpu_dev, cdev);
	file->private_data = kv;
	return 0;
}

static const struct file_operations kvcpu_fops = {
	.owner          = THIS_MODULE,
	.open           = kvcpu_open,
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

	ret = alloc_chrdev_region(&kvcpu_devt, 0, 64, "kvcpu");
	if (ret < 0)
		return ret;

	kvcpu_class = class_create("kvcpu");
	if (IS_ERR(kvcpu_class)) {
		unregister_chrdev_region(kvcpu_devt, 64);
		return PTR_ERR(kvcpu_class);
	}

	if (mock) {
		pr_info("kv_cpu: running in mock mode\n");
		mock_dev = kzalloc(sizeof(*mock_dev), GFP_KERNEL);
		if (!mock_dev) {
			ret = -ENOMEM;
			goto err_class;
		}

		mock_dev->is_mock = true;
		mock_dev->mock_bar = kzalloc(0x4000, GFP_KERNEL);
		if (!mock_dev->mock_bar) {
			ret = -ENOMEM;
			goto err_free_dev;
		}

		cdev_init(&mock_dev->cdev, &kvcpu_fops);
		mock_dev->cdev.owner = THIS_MODULE;
		ret = cdev_add(&mock_dev->cdev, kvcpu_devt, 1);
		if (ret)
			goto err_free_bar;

		mock_dev->dev = device_create(kvcpu_class, NULL, kvcpu_devt, NULL, "kvcpu0");
		if (IS_ERR(mock_dev->dev)) {
			ret = PTR_ERR(mock_dev->dev);
			goto err_cdev;
		}
		return 0;
	}

	return pci_register_driver(&kvcpu_pci_driver);

err_cdev:
	cdev_del(&mock_dev->cdev);
err_free_bar:
	kfree(mock_dev->mock_bar);
err_free_dev:
	kfree(mock_dev);
err_class:
	class_destroy(kvcpu_class);
	unregister_chrdev_region(kvcpu_devt, 64);
	return ret;
}

static void __exit kvcpu_exit(void)
{
	if (mock && mock_dev) {
		device_destroy(kvcpu_class, kvcpu_devt);
		cdev_del(&mock_dev->cdev);
		kfree(mock_dev->mock_bar);
		kfree(mock_dev);
	} else {
		pci_unregister_driver(&kvcpu_pci_driver);
	}

	class_destroy(kvcpu_class);
	unregister_chrdev_region(kvcpu_devt, 64);
}

module_init(kvcpu_init);
module_exit(kvcpu_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Manish KL <manishklach@gmail.com>");
MODULE_DESCRIPTION("KV-CPU Reference Control Plane Driver");
