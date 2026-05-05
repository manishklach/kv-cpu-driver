# KV-CPU Linux Control Plane (Reference Driver)

LLM inference is becoming **memory-orchestration bound**, not compute bound. As context windows and batch sizes expand, the bottleneck shifts from raw FLOPS to the efficient movement and placement of the KV-cache across memory tiers. 

This repository provides a reference Linux kernel driver that introduces a **semantic memory control plane** for a hypothetical KV-CPU device. It demonstrates how high-level transformer inference semantics can be exposed to hardware to enable intelligent, autonomous memory orchestration.

---

## The Problem

1.  **KV Cache Growth:** Modern LLMs require massive KV caches that often exceed available GPU HBM, forcing offloading to slower memory tiers.
2.  **Semantic Blindness:** Existing OS memory management (LRU, swapping) is "semantic-blind" to inference workloads. The kernel does not understand autoregressive **decode steps**, prefix sharing, or the predictable future access patterns of KV blocks, leading to inefficient eviction and high-latency page faults on the hot path.

## The Key Idea: Semantic Signaling

The KV-CPU architecture proposes a shift where the inference runtime (e.g., vLLM, TensorRT-LLM) signals high-level intent to the hardware via the Linux kernel:

-   **Decode Step Synchronization:** Informs the hardware of the current global iteration ($t$), allowing silicon-level logic to calculate block "freshness."
-   **Semantic Lifecycle Hints:** Explicitly identifies blocks as `HOT` (protected), `EVICTABLE` (candidate for offload), or `PREFETCHABLE` (needed in future steps).
-   **Hardware-Level Orchestration:** Moves the eviction policy from reactive software to an autonomous hardware controller (HEPC) that operates directly on the memory data plane.

## What This Repo Implements

This repository is a **control-plane prototype** and architectural reference. It implements:

-   **Kernel Driver Skeleton:** A standard Linux `pci_driver` with character device registration (`/dev/kvcpu0`).
-   **IOCTL Control Plane:** A standardized UAPI for signaling decode steps and block management.
-   **MMIO Abstraction:** A clean register-access layer with a robust **Mock Mode** for testing without physical hardware.
-   **Userspace Utility:** A reference tool (`kvctl`) to demonstrate interaction with the driver.
-   **Hardware Collateral:** Supporting RTL, MMIO, packaging, and detailed specification artifacts under [`hardware/`](./hardware).

## Implementation Status

The following table clarifies the scope of this reference implementation versus the conceptual hardware architecture.

| Feature | Status |
| :--- | :--- |
| **Kernel Driver** | **Implemented** (Reference skeleton & lifecycle) |
| **IOCTL Control Plane** | **Implemented** (UAPI and Dispatcher) |
| **MMIO Interface** | **Stub / Mock** (Fake register writes) |
| **DMA Engines** | **Not Implemented** (Signaling only) |
| **HEPC Eviction Logic** | **Conceptual** (Silicon-level logic) |
| **RTBD Directory** | **Conceptual** (Hardware tag storage) |
| **madvise Integration** | **Not Implemented** |
| **io_uring Integration** | **Not Implemented** |
| **NUMA / HMAT** | **Not Implemented** |

---

## Architecture Diagram

```text
  [ User Space ]          [ Kernel Space ]          [ Hardware Space ]
  (LLM Runtime)           (kv_cpu Driver)           (KV-CPU Silicon)
       |                        |                         |
       |--- ioctl(STEP) ------->|                         |
       |                        |--- MMIO Write --------->| [ HEPC Scorer ]
       |                        |                         |       |
       |--- ioctl(HOT/EVICT) -->|                         | [ DMA Engine ]
       |                        |--- MMIO Doorbell ------>|       |
       |                        |                         |       v
       |                        |                         | [ Memory Tiers ]
```

## Example Flow: Semantic Lifecycle

1.  **STEP:** Runtime signals `STEP 120` to the driver. Hardware HEPC re-evaluates all cached KV blocks based on their proximity to step 120.
2.  **HOT:** Runtime identifies a specific prefix range as `HOT`. Hardware boosts its priority to prevent eviction.
3.  **PREFETCH:** Runtime hints that a block will be needed at `STEP 256`. Hardware DMA autonomously moves the block from DRAM to LPDDR.
4.  **EVICT:** Runtime marks a completed request's cache as `EVICTABLE`. Hardware immediately reclaims the space.

In this reference implementation, these lifecycle operations are modeled as MMIO control signals only. No DMA submission, page pinning, or physical data movement is performed by the driver.

---

## Build & Run

### 1. Build the Driver and Tools
```bash
make
```

### 2. Load the Module (Mock Mode)
```bash
# Load without physical hardware requirement
sudo insmod kv_cpu.ko mock=1
```

### 3. Use the Reference Tool
```bash
# Signal a decode step
./tools/kvctl step 128

# Mark a range as HOT
./tools/kvctl hot 0x7f001000 4096
```

## Disclaimer

**This repository demonstrates a control-plane model for a KV-CPU device and is not a production-ready driver.** It is intended for systems architects and kernel engineers to evaluate the integration semantics of AI memory accelerators. No actual memory movement or performance simulation is performed by this driver.
