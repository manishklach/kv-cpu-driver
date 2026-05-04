# Linux Kernel Integration Map

This document outlines the exact touchpoints and kernel subsystems the KV-CPU driver interacts with to enable hardware-accelerated KV-cache management.

## 1. Device Model & Location
- **Location:** Proposed for `drivers/misc/kv_cpu/` (Reference implementation currently in `src/`).
- **Identity:** PCIe/CXL Type 1+3 Device. Uses the standard `pci_driver` framework for discovery and MMIO BAR mapping.

## 2. Character Device (Control Interface)
- **Node:** `/dev/kvcpuN`
- **Purpose:** Primary UAPI entry point for `ioctl()` commands. Used for out-of-band control signals like decode-step advancement and telemetry collection.

## 3. UAPI Header
- **Path:** `include/uapi/linux/kv_cpu.h`
- **Purpose:** Defines the contract between userspace LLM runtimes and the kernel driver. Includes command opcodes and bit-fields for hardware descriptors.

## 4. madvise() Extensions
- **Behaviors:** `MADV_KV_HOT`, `MADV_KV_EVICT`, `MADV_KV_PREFETCH`.
- **Purpose:** Leverages existing virtual memory semantics to pass lifecycle hints to hardware. The driver hooks into the `madvise` path to translate VMA ranges into physical hardware registers.

## 5. io_uring (Conceptual)
- **Opcodes:** `IORING_OP_KV_STAGE`, `IORING_OP_KV_EVICT`.
- **Purpose:** Enables asynchronous, non-blocking KV-block migrations. Runtimes can queue tiering operations without the overhead of synchronous syscalls.

## 6. NUMA & HMAT (Memory Tiering)
- **Subsystem:** `mm/memory-tiers.c`
- **Purpose:** The driver registers the KV-CPU's on-board memory (T1) as a distinct NUMA node. It uses ACPI HMAT-like descriptors to rank T1 between GPU HBM (Tier 0) and host DRAM (Tier 2).

## 7. DMA Mapping APIs
- **Subsystem:** `linux/dma-mapping.h`
- **Purpose:** Standard `dma_alloc_coherent` and `dma_map_page` calls are used to manage the submission/completion queues for the Near-Memory Compute Engine (NMCE).

## Summary Table

| Kernel Component | Interaction Type | Role in KV-CPU |
| :--- | :--- | :--- |
| `pci_driver` | Callback | Device probe, BAR0/BAR2 mapping. |
| `file_operations` | Entry point | `ioctl` handling for `KV_CPU_STEP_ADVANCE`. |
| `mmap` | Entry point | Exposing the HEPC doorbell for sub-100ns step writes. |
| `memory_hotplug` | API | Adding T1 LPDDR5X to the system memory map. |
| `ida` | API | Managing minor numbers for multiple KV-CPU instances. |
