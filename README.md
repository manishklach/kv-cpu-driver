# KV-CPU Linux Control Plane (Reference Driver)

This repository implements a reference Linux kernel enablement layer for the **KV-Cache Companion Processing Unit (KV-CPU)**. It demonstrates the implementation of a **semantic memory control plane** designed to bridge the gap between high-level transformer inference runtimes and hardware-accelerated memory tiering.

> [!NOTE]
> **Patent Information:**  
> Docket No 65779 | App Number 202641056309  
> Reference Number TEMP/E1/61503/2026-CHE | CBR Number 37184  
> Country: India | Status: Patent Pending

---

## The Problem: The Semantic Gap in Memory Orchestration

Modern Large Language Model (LLM) inference is primarily a memory orchestration challenge. As context windows expand, the **KV-cache** has become the primary memory bottleneck in inference systems. Modern OS kernels and memory subsystems are "semantic-blind" to inference workloads:

1.  **KV Cache Explosion:** Memory scale requirements exceed GPU HBM capacity, forcing data into slower tiers (DRAM/NVMe) without intelligent placement.
2.  **Lack of Semantic Awareness:** Standard kernel LRU policies do not understand autoregressive **decode steps** or prefix-sharing. This leads to inefficient data migration and high-latency page faults on the hot path.

## What This Reference Implementation Demonstrates

The KV-CPU introduces a control plane that allows LLM runtimes to communicate inference-specific intent directly to the hardware via the kernel:

-   **Decode-Step Signaling:** A low-latency path for runtimes to signal the current global decode iteration ($t$), enabling hardware to calculate block "proximity" for prefetching.
-   **KV-Block Lifecycle Hints:** Standardized mechanisms to mark memory ranges as `HOT` (protected prefix), `EVICTABLE` (candidate for offload), or `PREFETCHABLE` (future context).
-   **Semantic Mapping:** Translation of high-level LLM metadata (Request IDs, Layer indices) into hardware-level priority scores and DMA descriptors.

## Architecture Overview

The KV-CPU is integrated as a CXL Type 1+3 device, exposing on-board LPDDR5X as a NUMA node while accepting control commands via MMIO.

### 1. System-Level Architecture: Memory Tiering
```text
    +----------------+       +----------------+       +----------------+
    |      GPU       | <---> |     KV-CPU     | <---> |   Host DRAM    |
    |  (Tier 0: HBM) |  CXL  | (Tier 1: LP5X) |  PCIe | (Tier 2: DDR5) |
    +----------------+       +----------------+       +----------------+
                                     |
                                     v
                             +----------------+
                             |   NVMe SSD     |
                             | (Tier 3: Swap) |
                             +----------------+
```

### 2. Control Plane Flow: Semantic Signal Path
```text
    USER SPACE       (LLM Runtime)  |  vLLM / SGLang / TensorRT-LLM
         |                          |
    [ madvise() / ioctl() ]         |  Semantic signals (step, hot, evict)
         |                          |
    ================================|=====================================
    KERNEL SPACE     (kv_cpu)       |  Linux Kernel Context
         |                          |
    [ Translate VA -> PA ]          |  UAPI translation & validation
         |                          |
    [   MMIO Write   ]              |  Register access (Doorbell)
         |                          |
    ================================|=====================================
    HARDWARE         (KV-CPU)       |  Silicon Logic (HEPC / RTBD)
         |                          |
    [ Trigger Scan ]                |  Hardware scan of block priorities
```

## Kernel Integration

-   **Device Model:** Implemented as a `pci_driver` for PCIe/CXL device discovery and BAR mapping.
-   **UAPI Interface:** Exposes a clean `/dev/kvcpu` character device for out-of-band `ioctl()` control.
-   **Memory Tiering:** Hooks into the Linux memory-tiering subsystem to register on-board LPDDR5X as a distinct NUMA tier.
-   **MMIO Interface:** Provides a thin abstraction layer for register access, supporting both real hardware and software emulation (mock mode).

## Example Flow: Decode Step Synchronization

1.  **Userspace:** The LLM runtime completes decode step $N$ and advances to $N+1$.
2.  **Kernel:** The runtime calls `ioctl(fd, KV_CPU_STEP_ADVANCE, &step_info)`.
3.  **Hardware:** The driver writes the new step counter to the `KVCPU_REG_STEP_ADVANCE` MMIO register.
4.  **Silicon:** The **HEPC (Hardware Eviction Policy Controller)** triggers a scan to re-prioritize cached blocks based on their proximity to the new step.

## Implementation Status

| Feature | Status | Description |
| :--- | :--- | :--- |
| **Kernel Driver Skeleton** | **Implemented** | PCI probe, char device, and driver lifecycle. |
| **UAPI Definition** | **Implemented** | Standardized ioctl commands and data structures. |
| **MMIO Abstraction** | **Implemented** | Register access layer with mock support. |
| **Lifecycle Hooks** | **Implemented** | Policy hooks for HOT/EVICT/PREFETCH signals. |
| **DMA Engines** | **Stubbed** | Reference logic for asynchronous block movement. |
| **Hardware Emulation** | **Implemented** | Software mock for testing without physical CXL hardware. |

## How to Build & Run

### 1. Build the Driver
```bash
make
```

### 2. Load the Module (Mock Mode)
```bash
sudo insmod kv_cpu.ko mock=1
```

### 3. Use the Control Tool
```bash
# Signal decode step 256
sudo ./tools/kvctl step 256

# Mark a range as HOT
sudo ./tools/kvctl hot 0x7f001000 0x1000
```

## Disclaimer

This is an **experimental reference implementation** and architectural prototype. It is intended to demonstrate the Linux kernel integration patterns required for semantic memory controllers. It is NOT intended for production deployment or actual performance benchmarking without physical KV-CPU hardware.
