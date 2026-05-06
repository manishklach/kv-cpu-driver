/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * KV-CPU Control Plane Driver
 *
 * Userspace conveys semantic hints (decode step, KV hotness)
 * via ioctl. Driver translates these to device register writes.
 *
 * Copyright (C) 2026 Manish KL
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/idr.h>
#include "kv_cpu.h"

static bool mock;
module_param(mock, bool, 0444);
MODULE_PARM_DESC(mock, "Enable software mock mode (fallback if no hardware found)");

static dev_t kv_cpu_devt;
static struct class *kv_cpu_class;
static DEFINE_IDA(kv_cpu_ida);
static struct kv_cpu_device *mock_inst;

static void kv_cpu_init_runtime_defaults(struct kv_cpu_device *kv)
{
	kv->runtime.w_r = 1;
	kv->runtime.w_f = 1;
	kv->runtime.w_s = 2;
	kv->runtime.w_d = 200;
	kv->runtime.evict_thresh = 0x1000;
	kv->runtime.prefetch_thresh = 0xE000;
}

static int kv_cpu_open(struct inode *inode, struct file *file)
{
	struct kv_cpu_device *kv = container_of(inode->i_cdev, struct kv_cpu_device, cdev);

	file->private_data = kv;
	return 0;
}

static int kv_cpu_release(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct file_operations kv_cpu_fops = {
	.owner		= THIS_MODULE,
	.open		= kv_cpu_open,
	.release	= kv_cpu_release,
	.unlocked_ioctl	= kv_cpu_ioctl,
};

static int kv_cpu_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct kv_cpu_device *kv;
	int ret, instance;

	kv = devm_kzalloc(&pdev->dev, sizeof(*kv), GFP_KERNEL);
	if (!kv)
		return -ENOMEM;

	kv->pdev = pdev;
	spin_lock_init(&kv->cmd_lock);
	kv_cpu_init_runtime_defaults(kv);
	pci_set_drvdata(pdev, kv);

	ret = pci_enable_device(pdev);
	if (ret)
		return ret;

	ret = pci_request_regions(pdev, DRIVER_NAME);
	if (ret)
		goto err_disable;

	pci_set_master(pdev);

	kv->bar0 = pci_iomap(pdev, 0, 0);
	if (!kv->bar0) {
		ret = -ENOMEM;
		goto err_release_regions;
	}

	instance = ida_alloc(&kv_cpu_ida, GFP_KERNEL);
	if (instance < 0) {
		ret = instance;
		goto err_iounmap;
	}

	cdev_init(&kv->cdev, &kv_cpu_fops);
	kv->cdev.owner = THIS_MODULE;
	ret = cdev_add(&kv->cdev, MKDEV(MAJOR(kv_cpu_devt), instance), 1);
	if (ret)
		goto err_ida;

	kv->dev = device_create(kv_cpu_class, &pdev->dev, 
				MKDEV(MAJOR(kv_cpu_devt), instance), 
				NULL, "kvcpu%d", instance);
	if (IS_ERR(kv->dev)) {
		ret = PTR_ERR(kv->dev);
		goto err_cdev;
	}

	dev_set_drvdata(kv->dev, kv);
	ret = kv_cpu_sysfs_create(kv);
	if (ret)
		goto err_device;

	dev_info(&pdev->dev, "KV-CPU device probed successfully\n");
	return 0;

err_device:
	device_destroy(kv_cpu_class, kv->dev->devt);
err_cdev:
	cdev_del(&kv->cdev);
err_ida:
	ida_free(&kv_cpu_ida, instance);
err_iounmap:
	pci_iounmap(pdev, kv->bar0);
err_release_regions:
	pci_release_regions(pdev);
err_disable:
	pci_disable_device(pdev);
	return ret;
}

static void kv_cpu_remove(struct pci_dev *pdev)
{
	struct kv_cpu_device *kv = pci_get_drvdata(pdev);
	int instance = MINOR(kv->dev->devt);

	kv_cpu_sysfs_remove(kv);
	device_destroy(kv_cpu_class, kv->dev->devt);
	cdev_del(&kv->cdev);
	ida_free(&kv_cpu_ida, instance);
	pci_iounmap(pdev, kv->bar0);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

static const struct pci_device_id kv_cpu_pci_ids[] = {
	{ PCI_DEVICE(0x1DE5, 0x0A10) },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, kv_cpu_pci_ids);

static struct pci_driver kv_cpu_pci_driver = {
	.name		= DRIVER_NAME,
	.id_table	= kv_cpu_pci_ids,
	.probe		= kv_cpu_probe,
	.remove		= kv_cpu_remove,
};

static int __init kv_cpu_init(void)
{
	int ret;

	ret = alloc_chrdev_region(&kv_cpu_devt, 0, 64, DRIVER_NAME);
	if (ret)
		return ret;

	kv_cpu_class = class_create(DRIVER_NAME);
	if (IS_ERR(kv_cpu_class)) {
		ret = PTR_ERR(kv_cpu_class);
		goto err_chrdev;
	}

	if (mock) {
		pr_info("kv_cpu: loading in mock mode (no hardware required)\n");
		mock_inst = kzalloc(sizeof(*mock_inst), GFP_KERNEL);
		if (!mock_inst) {
			ret = -ENOMEM;
			goto err_class;
		}

		mock_inst->is_mock = true;
		spin_lock_init(&mock_inst->cmd_lock);
		kv_cpu_init_runtime_defaults(mock_inst);
		mock_inst->mock_bar = kzalloc(0x1000, GFP_KERNEL);
		if (!mock_inst->mock_bar) {
			ret = -ENOMEM;
			goto err_free_mock;
		}

		cdev_init(&mock_inst->cdev, &kv_cpu_fops);
		mock_inst->cdev.owner = THIS_MODULE;
		ret = cdev_add(&mock_inst->cdev, MKDEV(MAJOR(kv_cpu_devt), 0), 1);
		if (ret)
			goto err_free_bar;

		mock_inst->dev = device_create(kv_cpu_class, NULL, 
						MKDEV(MAJOR(kv_cpu_devt), 0), 
						NULL, "kvcpu0");
		if (IS_ERR(mock_inst->dev)) {
			ret = PTR_ERR(mock_inst->dev);
			goto err_cdev_mock;
		}
		dev_set_drvdata(mock_inst->dev, mock_inst);
		ret = kv_cpu_sysfs_create(mock_inst);
		if (ret)
			goto err_device_mock;
		return 0;
	}

	ret = pci_register_driver(&kv_cpu_pci_driver);
	if (ret)
		goto err_class;

	return 0;

err_device_mock:
	device_destroy(kv_cpu_class, MKDEV(MAJOR(kv_cpu_devt), 0));
err_cdev_mock:
	cdev_del(&mock_inst->cdev);
err_free_bar:
	kfree(mock_inst->mock_bar);
err_free_mock:
	kfree(mock_inst);
err_class:
	class_destroy(kv_cpu_class);
err_chrdev:
	unregister_chrdev_region(kv_cpu_devt, 64);
	return ret;
}

static void __exit kv_cpu_exit(void)
{
	if (mock && mock_inst) {
		kv_cpu_sysfs_remove(mock_inst);
		device_destroy(kv_cpu_class, MKDEV(MAJOR(kv_cpu_devt), 0));
		cdev_del(&mock_inst->cdev);
		kfree(mock_inst->mock_bar);
		kfree(mock_inst);
	} else {
		pci_unregister_driver(&kv_cpu_pci_driver);
	}

	class_destroy(kv_cpu_class);
	unregister_chrdev_region(kv_cpu_devt, 64);
}

module_init(kv_cpu_init);
module_exit(kv_cpu_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Manish KL <manishklach@gmail.com>");
MODULE_DESCRIPTION("KV-CPU Reference Control Plane Driver");
