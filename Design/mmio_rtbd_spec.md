# KV-CPU MMIO Address Space & RTBD Specification

**Version:** 1.7  
**Reference:** kv_cpu_patent_v3.pdf  

---

## 1. MMIO Address Space (CXL/PCIe BARs)

```text
[BAR 0] CSRs (Control/Status)
0x0000 - 0x0FFF : HEPC Lifecycle Control (Step-Advance, Weights)
0x1000 - 0x1FFF : DMA Engine Management (Evict/Prefetch Queues)
0x2000 - 0x2FFF : NMCE Descriptor Submission (Compute Dispatch)

[BAR 2] RTBD TAG STORE (65k Entries)
0x0000_0000 - 0x001F_FFFF : Hardware CAM Entry Region (32B Aligned)
```

## 2. RTBD Entry Bit-Field Detail

| Bit Range | Field | Width | Hardware Semantic |
| :--- | :--- | :--- | :--- |
| **[15:0]** | REQUEST_ID | 16b | Unique ID / 0x0000 = Shared Prefix |
| **[31:16]** | LAYER_HEAD_IDX | 16b | 8b Layer / 8b Attention Head |
| **[95:32]** | TOKEN_RANGE | 64b | 32b Start / 32b End Position |
| **[97:96]** | TIER_LOC | 2b | HBM(0), T1(1), DRAM(2), NVMe(3) |
| **[161:98]** | PHYS_ADDR | 64b | Tier-local byte address |
| **[177:162]** | PRIORITY | 16b | HEPC Composite Score P(Bi) |
| **[209:178]** | ACCESS_STEP | 32b | Last Access Step (t) |
| **[239:210]** | META/REF | 30b | 8b Ref_count, Prefix_flag, Dirty |

## 3. HMAT Memory Attributes (T1 Tier)

- **NUMA Integration:** Registered via ACPI HMAT for Linux kernel placement.
- **Latency:** ~150 ns (Local Access) / ~220 ns (Remote Access).
- **Throughput:** 200 GB/s aggregate LPDDR5X bandwidth.
- **Control Plane:** madvise extensions (`MADV_KV_HOT`/`EVICT`/`PREFETCH`) trigger MMIO writes.

---

**Patent Information (India):**  
Docket No 65779 | App Number 202641056309  
Reference Number TEMP/E1/61503/2026-CHE | CBR Number 37184  
CONFIDENTIAL - FILED UNDER THE PATENTS ACT, 1970
