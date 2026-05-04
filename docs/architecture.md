# KV-CPU Architecture: Semantic Control Plane

This document describes the architectural flow of the KV-CPU reference driver, focusing on how inference semantics are translated from userspace to hardware.

## High-Level Flow

The KV-CPU driver operates primarily as a **control-plane coordinator**. It does not sit in the data path of every memory access; instead, it provides the hardware with the "hints" necessary for the hardware to make autonomous data-movement decisions.

```text
User Space (LLM Runtime)
    |
    | (1) Semantic Signal (step=N, block=HOT)
    v
Kernel (kv_cpu driver)
    |
    | (2) Translate VA to PA
    | (3) Map semantic -> Hardware Register
    v
KV-CPU Hardware
    |
    | (4) HEPC re-calculates priorities
    | (5) DMA Engine schedules tier migration
    v
Memory Tiers (HBM / T1 / T2 / T3)
```

## Key Mechanisms

### 1. Decode-Step Signal Path
In autoregressive inference, the "temperature" or "value" of a KV-cache block changes every time a new token is generated.
- **Path:** The LLM runtime writes the current step counter to the driver via `KV_CPU_STEP_ADVANCE`.
- **Driver Action:** The driver performs an MMIO write to a hardware doorbell.
- **Hardware Action:** This triggers the **HEPC (Hardware Eviction Policy Controller)** to perform a new scan cycle across all cached blocks to update their step-proximity scores ($w_s \cdot S$).

### 2. Metadata Handling (RTBD)
The **RTBD (Request-Tagged Block Directory)** acts as a hardware-accelerated page table for KV-blocks.
- **Driver Role:** When a block is allocated or shared (prefix sharing), the driver updates the RTBD metadata (e.g., `ref_count`).
- **Semantic Mapping:** The driver maps "Request IDs" from the LLM runtime to hardware tags, allowing the hardware to group blocks belonging to the same inference stream.

### 3. Prefix Sharing Concept
Prefix sharing is handled via a **Reference Counting** mechanism exposed to the driver.
- When multiple requests share a common prefix (e.g., a long system prompt), the runtime calls `KV_CPU_SHARE_PREFIX`.
- The driver signals the RTBD to increment a hardware reference counter.
- The HEPC uses this counter as a "stickiness" weight ($w_d \cdot D$), preventing frequently shared prefixes from being evicted to slow tiers (T2/T3).

### 4. Separation of Control vs. Data Plane
- **Control Plane:** Handled by this driver (`madvise`, `ioctl`). Includes policy updates, step signals, and lifecycle hints.
- **Data Plane:** Handled by the **NMCE (Near-Memory Compute Engine)** and standard CXL.mem protocols. The driver sets up the descriptors, but the hardware autonomously moves data between T1 LPDDR5X and host memory without per-block kernel intervention.

## Summary of Design Philosophy
The driver follows the **"Kernel as Policy Manager"** philosophy. It provides the mechanism for userspace to express intent, while delegating the heavy lifting of block-by-block management to silicon-level controllers.
