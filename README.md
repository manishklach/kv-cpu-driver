# KV-CPU Linux Control Plane (Reference Driver)

A reference Linux kernel enablement layer for the **KV-Cache Companion Processing Unit (KV-CPU)**. This repository demonstrates how high-level transformer inference semantics (decode step, KV-block lifecycle) are mapped to a hardware control plane via standard Linux kernel interfaces.

> [!NOTE]
> **Patent Information:**  
> Docket No 65779 | App Number 202641056309  
> Reference Number TEMP/E1/61503/2026-CHE | CBR Number 37184  
> Country: India | Status: Patent Pending

---

## Problem Statement: The Semantic Gap

As LLM context windows grow, the **KV-cache** has become the primary memory bottleneck in inference systems. Modern OS kernels and memory subsystems are "semantic-blind" to inference workloads:
1. **Lifecycle Opacity:** The kernel does not know which memory blocks contain "hot" active prefixes versus "cold" evicted suffixes.
2. **Step Proximity:** The kernel cannot predict when a block will be needed next because it has no visibility into the autoregressive **decode step**.
3. **Ref-Count Complexity:** Prefix sharing across multiple requests is managed entirely in userspace, leading to inefficient memory tiering decisions.

## What This Reference Implementation Demonstrates

This driver provides a blueprint for bridging this gap by exposing LLM-specific control signals to a hardware accelerator:

- **Decode-Step Signaling:** Low-latency path for userspace runtimes (vLLM, SGLang) to signal the current global decode step `t`.
- **KV-Block Lifecycle Hints:** Standardized `madvise` and `ioctl` paths for marking blocks as `HOT`, `EVICTABLE`, or `PREFETCHABLE`.
- **Hardware Mapping:** Translation of these hints into hardware-level registers for the **HEPC** (Hardware Eviction Policy Controller) and **RTBD** (Request-Tagged Block Directory).
- **UAPI Design:** A clean, minimal Userspace API (UAPI) that feels like a native Linux kernel subsystem.

## Architecture Overview

The KV-CPU is integrated as a CXL Type 1+3 device, exposing on-board LPDDR5X as a NUMA node while accepting control commands via MMIO.

```text
   +---------------------------------------+
   |        User Space (LLM Runtime)       |
   |   (vLLM / SGLang / TensorRT-LLM)      |
   +---------------------------------------+
           |               |             |
     [madvise()]       [ioctl()]    [io_uring]
           |               |             |
           v               v             v
   +---------------------------------------+
   |        KV-CPU Linux Driver            |
   |  (Control Plane / Semantic Mapping)   |
   +---------------------------------------+
           |               |
     [MMIO Writes]   [DMA Descriptors]
           |               |
           v               v
   +---------------------------------------+
   |          KV-CPU Hardware              |
   |  [HEPC]      [RTBD]      [NMCE]       |
   +---------------------------------------+
```

## Implementation Status

| Feature | Status | Description |
| :--- | :--- | :--- |
| **Kernel Driver Skeleton** | **Implemented** | Core module, probe/remove, char device registration. |
| **UAPI Interface** | **Implemented** | Clean ioctl and struct definitions in `kv_cpu.h`. |
| **MMIO Abstraction** | **Implemented** | Register access layer with mock-mode support. |
| **Mock Emulation** | **Implemented** | Software simulation of HEPC lifecycle for testing. |
| **DMA Engines** | **Stubbed** | Placeholder logic for asynchronous block migration. |
| **Hardware HEPC** | **Conceptual** | Scoring algorithm described in RTL specifications. |
| **RTBD Directory** | **Conceptual** | Hardware CAM layout defined in design assets. |

## Quick Start

### Build
```bash
make
```

### Usage (Mock Mode)
Load the driver in emulation mode (no hardware required):
```bash
sudo insmod kv_cpu.ko mock=1
```

### Interaction
Use the provided control tool to signal the kernel:
```bash
# Signal decode step 128
sudo ./tools/kvctl step 128

# Mark a memory range as HOT
sudo ./tools/kvctl hot 0x7f0012340000 0x1000
```

## Documentation
- [Architecture Detail](docs/architecture.md): Deep dive into the control flow.
- [Linux Integration Map](docs/linux-integration-map.md): Exact kernel touchpoints and APIs used.
- [Hardware Design](Design/): RTL, Floorplans, and MMIO blueprints.

---

## Disclaimer
This is an **experimental reference implementation** intended for research and architectural demonstration. It is not currently intended for upstream Linux kernel inclusion. All performance-related code is simulated to demonstrate control flow rather than actual throughput.
