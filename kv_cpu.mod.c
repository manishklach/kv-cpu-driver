#include <linux/module.h>
#define INCLUDE_VERMAGIC
#include <linux/build-salt.h>
#include <linux/elfnote-lto.h>
#include <linux/export-internal.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

#ifdef CONFIG_UNWINDER_ORC
#include <asm/orc_header.h>
ORC_HEADER;
#endif

BUILD_SALT;
BUILD_LTO_INFO;

MODULE_INFO(vermagic, VERMAGIC_STRING);
MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

#ifdef CONFIG_RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif

KSYMTAB_FUNC(kvcpu_hepc_step_advance, "_gpl", "");
KSYMTAB_FUNC(kvcpu_madvise_handler, "_gpl", "");

SYMBOL_CRC(kvcpu_hepc_step_advance, 0x7f3ab940, "_gpl");
SYMBOL_CRC(kvcpu_madvise_handler, 0x3666e893, "_gpl");

static const struct modversion_info ____versions[]
__used __section("__versions") = {
	{ 0x638a9653, "memory_add_physaddr_to_nid" },
	{ 0xfcbfec70, "add_memory_driver_managed" },
	{ 0x3d55706c, "_dev_info" },
	{ 0x3af130aa, "cdev_add" },
	{ 0xbcb36fe4, "hugetlb_optimize_vmemmap_key" },
	{ 0x997e4f89, "pcim_iomap_regions" },
	{ 0x5949bc4b, "alloc_memory_type" },
	{ 0x98378a1d, "cc_mkdec" },
	{ 0x4c31241f, "_dev_err" },
	{ 0xe8962364, "device_create" },
	{ 0xf8bc0beb, "clear_node_memory_type" },
	{ 0x40df682c, "class_create" },
	{ 0x4dfa8d4b, "mutex_lock" },
	{ 0x214ed4f7, "dma_alloc_attrs" },
	{ 0xffb7c514, "ida_free" },
	{ 0xfc33777d, "sysfs_create_group" },
	{ 0xd35cce70, "_raw_spin_unlock_irqrestore" },
	{ 0xce43cea3, "_dev_warn" },
	{ 0x5c3c7387, "kstrtoull" },
	{ 0x92862837, "pci_set_master" },
	{ 0x5b8239ca, "__x86_return_thunk" },
	{ 0x6b10bee1, "_copy_to_user" },
	{ 0xbbdc9b2, "remove_memory" },
	{ 0x74e6f0f, "dma_set_coherent_mask" },
	{ 0x1303e726, "sysfs_remove_group" },
	{ 0x97651e6c, "vmemmap_base" },
	{ 0xfcc1edd3, "memory_block_size_bytes" },
	{ 0xc2b50c95, "dma_free_attrs" },
	{ 0x6091b333, "unregister_chrdev_region" },
	{ 0x3213f038, "mutex_unlock" },
	{ 0xeae3dfd6, "__const_udelay" },
	{ 0x69ffffe3, "__devm_release_region" },
	{ 0xaed843d0, "__folio_put" },
	{ 0xb39b608e, "device_destroy" },
	{ 0xaff12a85, "boot_cpu_data" },
	{ 0x3d88eee0, "dma_set_mask" },
	{ 0x7304cc1, "pcim_iomap_table" },
	{ 0x46cf10eb, "cachemode2protval" },
	{ 0x77358855, "iomem_resource" },
	{ 0xea267576, "pcim_enable_device" },
	{ 0xd63fd76f, "pci_free_irq_vectors" },
	{ 0x73066ebc, "cdev_init" },
	{ 0x728365e3, "cdev_del" },
	{ 0x448d9e3d, "put_memory_type" },
	{ 0x587f22d7, "devmap_managed_key" },
	{ 0xe7a02573, "ida_alloc_range" },
	{ 0x64ffb0e8, "get_user_pages_fast" },
	{ 0xe3ec2f2b, "alloc_chrdev_region" },
	{ 0x90ca55e9, "devm_request_threaded_irq" },
	{ 0x13c49cc2, "_copy_from_user" },
	{ 0x80841722, "devm_kmalloc" },
	{ 0x31a8357f, "pci_alloc_irq_vectors" },
	{ 0x4b7b5a1a, "class_destroy" },
	{ 0x4b9a68c0, "__pci_register_driver" },
	{ 0x7b37d4a7, "_find_first_zero_bit" },
	{ 0x675ecb2b, "remap_pfn_range" },
	{ 0xa75feaf4, "__put_devmap_managed_page_refs" },
	{ 0x39bf59a9, "init_node_memory_type" },
	{ 0x7e6926f3, "pci_irq_vector" },
	{ 0x34db050b, "_raw_spin_lock_irqsave" },
	{ 0x3eeca751, "__dynamic_dev_dbg" },
	{ 0x9493fc86, "node_states" },
	{ 0xd39d0b01, "pci_unregister_driver" },
	{ 0xbdfb6dbb, "__fentry__" },
	{ 0xe783e261, "sysfs_emit" },
	{ 0xf2164707, "__devm_request_region" },
	{ 0x122c3a7e, "_printk" },
	{ 0xf0fdf6cb, "__stack_chk_fail" },
	{ 0xb08e71bf, "module_layout" },
};

MODULE_INFO(depends, "");

MODULE_ALIAS("pci:v00001DE5d00000A10sv00001DE5sd00000001bc*sc*i*");

MODULE_INFO(srcversion, "B98FE1344E5FB34F89799AC");
