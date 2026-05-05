# KV-CPU Architecture: Semantic Control Plane

This document describes the architectural flow of the KV-CPU reference driver, focusing on how inference semantics are translated from userspace to hardware.

## High-Level Flow

The KV-CPU driver operates primarily as a **control-plane coordinator**. It does not sit in the data path of every memory access; instead, it provides the hardware with the "hints" necessary for the hardware to make autonomous data-movement decisions.

This repository's current implementation is intentionally narrow: it forwards semantic hints to MMIO registers and does not perform VA-to-PA translation, GUP pinning, DMA descriptor setup, or RTBD entry management in software.

```text
User Space (LLM Runtime)
    |
    | (1) Semantic Signal (step=N, block=HOT)
    v
Kernel (kv_cpu driver)
    |
    | (2) Validate userspace payload
    | (3) Map semantic -> MMIO register write
    v
KV-CPU Hardware
    |
    | (4) HEPC re-calculates priorities
    | (5) DMA Engine schedules tier migration
    v
Memory Tiers (HBM / T1 / T2 / T3)
```

### Closed-Loop Hardware Orchestration
```text
    +-------------------+           +-------------------+
    |   NMCE (Compute)  | <-------+ |   RTBD (Metadata) |
    |  Score Dot-Prod   |           |  Ref-Count / Tags |
    +---------+---------+           +---------+---------+
              |                               ^
              v                               |
    +---------+---------+           +---------+---------+
    |   HEPC (Policy)   | -------> |   DMA (Placement) |
    |  P(Bi) Scoring    |           |  Migrate / Fetch  |
    +-------------------+           +-------------------+
```

### KV Block Semantic Lifecycle
```text
    [ ALLOCATED ] ----> [   HOT     ] ----> [ PRE-EVICT ] ----> [ COLD/OFFLOAD ]
          |               ^    |               |                     |
          | (Prefetch)    |    | (Step Gap)    | (Low Prio)          | (Release)
          +---------------+    +---------------+---------------------+
                                       |
                                       v
                                [   RECLAIMED  ]
```

## Key Mechanisms

### 1. Decode-Step Signal Path
In autoregressive inference, the "temperature" or "value" of a KV-cache block changes every time a new token is generated.
- **Path:** The LLM runtime writes the current step counter to the driver via `KV_CPU_STEP_ADVANCE`.
- **Driver Action:** The driver performs an MMIO write to a hardware doorbell.
- **Hardware Action:** This triggers the **HEPC (Hardware Eviction Policy Controller)** to perform a new scan cycle across all cached blocks to update their step-proximity scores ($w_s \cdot S$).

### 2. Metadata Handling (RTBD)
The **RTBD (Request-Tagged Block Directory)** remains a hardware-side concept in this prototype.
- **Current Driver Role:** The driver only forwards semantic hints such as `HOT`, `EVICT`, `PREFETCH`, and `SHARE_PREFIX` to MMIO registers.
- **Future Production Path:** A fuller implementation could map runtime request identifiers to hardware tags and update RTBD metadata explicitly.

### 3. Prefix Sharing Concept
Prefix sharing is handled via a **Reference Counting** mechanism exposed to the driver.
- When multiple requests share a common prefix (e.g., a long system prompt), the runtime calls `KV_CPU_SHARE_PREFIX`.
- The current prototype signals a dedicated MMIO register pair for the shared range.
- The HEPC uses this counter as a "stickiness" weight ($w_d \cdot D$), preventing frequently shared prefixes from being evicted to slow tiers (T2/T3).

### 4. Separation of Control vs. Data Plane
- **Control Plane:** Handled by this driver (`madvise`, `ioctl`). Includes policy updates, step signals, and lifecycle hints.
- **Data Plane:** Not implemented in this repository. The architectural intent is that future hardware blocks such as the **NMCE (Near-Memory Compute Engine)** and DMA engines would move data autonomously once signaled by the control plane.

## Summary of Design Philosophy
The driver follows the **"Kernel as Policy Manager"** philosophy. It provides the mechanism for userspace to express intent, while delegating the heavy lifting of block-by-block management to silicon-level controllers.
