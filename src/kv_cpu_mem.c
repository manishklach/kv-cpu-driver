// SPDX-License-Identifier: GPL-2.0-only
/*
 * kv_cpu_mem.c — T1 LPDDR5X Memory Tier Registration (Pillar IV / HMAT)
 *
 * Registers the KV-CPU's on-board LPDDR5X memory with the Linux kernel
 * memory management subsystem as a distinct NUMA node, using:
 *
 *   - memory_hotplug: add_memory() to expose the T1 region to the kernel
 *   - node_set(): register the new NUMA node
 *   - memory_dev_type: describe the tier's latency/bandwidth via HMAT
 *
 * After registration, LLM runtimes can direct KV allocations to T1 via:
 *   mbind(addr, len, MPOL_BIND, &kvcpu_nodemask, ...)
 *   numactl --membind=<kvcpu_node> python vllm_serve.py
 *
 * The kernel's memory demotion/promotion machinery (numa_balancing,
 * memory_tier) will then manage T1 as a first-class tier between GPU HBM
 * (managed by the GPU driver as a DAX device or CXL.mem region) and
 * host DRAM (the normal NUMA nodes).
 *
 * Author: Manish Keshav Lachwani <mlachwani@gmail.com>
 */

#include <linux/module.h>
#include <linux/numa.h>
#include <linux/memory_hotplug.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/node.h>
#include <linux/nodemask.h>
#include "../include/kv_cpu.h"

/*
 * HMAT-style latency / bandwidth descriptors for T1 LPDDR5X.
 *
 * These match the hardware spec:
 *   Read latency:  150 ns local, 220 ns cross-socket
 *   Write latency: 155 ns local, 225 ns cross-socket
 *   Read BW:       200 GB/s (256 GB/s theoretical, 200 GB/s sustained)
 *
 * The kernel's memory_dev_type infrastructure (introduced in 5.18 for
 * heterogeneous memory tiering) uses these to rank tiers for demotion.
 * A lower tier_rank means "prefer to keep data here" —
 *   Tier 0: GPU HBM (fastest, most expensive)
 *   Tier 1: KV-CPU LPDDR5X (this device)
 *   Tier 2: Host DDR5 DRAM
 *   Tier 3: NVMe (managed by block layer, not memory tier)
 */
#define KVCPU_MEM_TIER_RANK     1          /* between GPU HBM and host DRAM   */
#define KVCPU_READ_LATENCY_NS   150        /* local read latency (nanoseconds) */
#define KVCPU_WRITE_LATENCY_NS  155
#define KVCPU_READ_BW_MBS       (200*1024) /* 200 GB/s in MB/s                */
#define KVCPU_WRITE_BW_MBS      (200*1024)

int kvcpu_mem_register(struct kvcpu_dev *kv)
{
	phys_addr_t base;
	resource_size_t size;
	int ret, nid;

	base = (phys_addr_t)kvcpu_readq(kv, KVCPU_REG_MEM_BASE);
	size = (resource_size_t)kvcpu_readq(kv, KVCPU_REG_MEM_SIZE);

	if (!base || !size) {
		dev_err(kv->dev, "T1 memory region not advertised by hardware\n");
		return -ENODEV;
	}

	/* Align to section size (required by add_memory) */
	if (!IS_ALIGNED(base, memory_block_size_bytes()) ||
	    !IS_ALIGNED(size, memory_block_size_bytes())) {
		dev_err(kv->dev,
			"T1 region base=0x%llx size=0x%llx not section-aligned "
			"(section_size=0x%lx)\n",
			(u64)base, (u64)size,
			memory_block_size_bytes());
		return -EINVAL;
	}

	/*
	 * Request iomem resource so the region appears in /proc/iomem and
	 * cannot be claimed by another driver.
	 */
	kv->mem.res = devm_request_mem_region(&kv->pdev->dev, base, size,
					      "kvcpu-t1-lpddr5x");
	if (!kv->mem.res) {
		dev_err(kv->dev,
			"T1 region [0x%llx, 0x%llx] already claimed\n",
			(u64)base, (u64)(base + size - 1));
		return -EBUSY;
	}

	kv->mem.base = base;
	kv->mem.size = size;

	/*
	 * Register the memory device type (memory tiering ABI).
	 * alloc_memory_type() assigns a tier rank and associates latency/BW
	 * characteristics for use by the kernel's migrate_pages() and
	 * numa_balancing demote/promote paths.
	 *
	 * Note: The exact API has been evolving across kernel versions.
	 * This targets Linux 6.6+ where memory_dev_type and
	 * alloc_memory_type() are stable.
	 */
	kv->mem.mtype = alloc_memory_type(KVCPU_MEM_TIER_RANK);
	if (IS_ERR(kv->mem.mtype)) {
		ret = PTR_ERR(kv->mem.mtype);
		dev_err(kv->dev, "alloc_memory_type failed: %d\n", ret);
		goto err_mtype;
	}

	/*
	 * Hot-add the T1 region to the kernel's physical memory map.
	 *
	 * add_memory_driver_managed() is the correct function for device-
	 * managed memory (as opposed to system RAM). It:
	 *   - Creates struct pages for the region
	 *   - Assigns a new NUMA node ID (returned via nid output)
	 *   - Does NOT add pages to the buddy allocator yet (MHP_MEMBLOCK_API)
	 *
	 * We leave the pages offline initially and bring them online as
	 * ZONE_MOVABLE so the kernel can migrate data in/out freely.
	 */
	nid = memory_add_physaddr_to_nid(base);
	if (nid == NUMA_NO_NODE) {
		/*
		 * No existing node covers this range — allocate a new node.
		 * In practice this requires ACPI SRAT/SLIT tables or an
		 * explicit node_set() call. For emulation we use node_set
		 * to claim the next available node.
		 */
		nid = first_unset_node(node_online_map);
		if (nid >= MAX_NUMNODES) {
			dev_err(kv->dev, "no NUMA node slots available\n");
			ret = -ENOSPC;
			goto err_node;
		}
		node_set(nid, node_online_map);
		node_set(nid, node_possible_map);
	}
	kv->mem.numa_node = nid;

	ret = add_memory_driver_managed(nid, base, size,
					"kvcpu-t1", MHP_NID_IS_SETTED);
	if (ret) {
		dev_err(kv->dev, "add_memory_driver_managed failed: %d\n", ret);
		goto err_addmem;
	}

	/*
	 * Associate our memory_dev_type with this node.
	 * This makes the node appear in:
	 *   /sys/devices/system/node/nodeN/memory_tier
	 * and enables the kernel's tier-aware demotion to demote cold pages
	 * from host DRAM to here (if we're configured as a lower tier).
	 *
	 * For KV workloads we want the LLM runtime to control placement
	 * explicitly via mbind(), so we register the type but leave
	 * automatic migration off by default.
	 */
	init_node_memory_type(nid, kv->mem.mtype);

	dev_info(kv->dev,
		 "T1 LPDDR5X registered: node=%d base=0x%llx size=%llu GiB\n",
		 nid, (u64)base, (u64)size >> 30);
	dev_info(kv->dev,
		 "  → to allocate KV cache here: "
		 "mbind(ptr, len, MPOL_BIND, nodemask(%d), ...)\n", nid);
	dev_info(kv->dev,
		 "  → or: numactl --membind=%d <process>\n", nid);

	return 0;

err_addmem:
	if (kv->mem.numa_node != NUMA_NO_NODE) {
		node_clear(kv->mem.numa_node, node_online_map);
		node_clear(kv->mem.numa_node, node_possible_map);
	}
err_node:
	put_memory_type(kv->mem.mtype);
err_mtype:
	devm_release_mem_region(&kv->pdev->dev, base, size);
	return ret;
}

void kvcpu_mem_unregister(struct kvcpu_dev *kv)
{
	if (!kv->mem.base)
		return;

	/*
	 * Remove memory from kernel before releasing NUMA node.
	 * remove_memory() will migrate any pages still resident here
	 * to other nodes before taking the region offline.
	 */
	if (kv->mem.numa_node != NUMA_NO_NODE) {
		clear_node_memory_type(kv->mem.numa_node, kv->mem.mtype);

		remove_memory(kv->mem.numa_node, kv->mem.base, kv->mem.size);

		node_clear(kv->mem.numa_node, node_online_map);
		node_clear(kv->mem.numa_node, node_possible_map);
	}

	if (kv->mem.mtype)
		put_memory_type(kv->mem.mtype);

	devm_release_mem_region(&kv->pdev->dev, kv->mem.base, kv->mem.size);

	dev_info(kv->dev, "T1 memory region unregistered\n");
}
