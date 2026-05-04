# KV-CPU Master Specification

**Version:** 1.8  
**Reference:** kv_cpu_patent_v3.pdf  

---

## I. Silicon Die Microarchitecture (Quadrant Layout)

### Quadrant A: NMCE (Compute)
- **DPU Array:** 128x FP16/BF16 MAC units.
- **Traffic reduction:** 25.6x Scalar Return Path.
- **Throughput:** 256 GFLOPS (On-Device).

### Quadrant B: RTBD (Metadata)
- **CAM Size:** 65,536 Hardware Tag Entries.
- **Isolation:** Multi-tenant Request-ID (16-bit).
- **Ref-Counting:** Silicon-enforced Prefix Sharing.

### Quadrant C: HEPC (Policy)
- **Trigger:** 50ns Step-Advance MMIO write.
- **Algorithm:** R+F+S+D Priority Scoring.
- **DMA:** Dual-channel Async Tiering.

---

## II. Autonomous Tiering Decision Tree

The HEPC re-evaluates block priorities upon receiving a `STEP_ADVANCE` signal.

**Priority Score Formula:**
$$P(B_i) = w_r \cdot R + w_f \cdot F + w_s \cdot S + w_d \cdot D \pmod{2^{16}}$$

- **P(Bi) > PREFETCH_T:** Issue `IORING_OP_KV_STAGE` (Move to T1)
- **P(Bi) < EVICT_T:** Issue `IORING_OP_KV_EVICT` (Move to T2/T3)

---

## III. Detailed Register Map (BAR 0/2)

| Address | Name | Function / Semantic |
| :--- | :--- | :--- |
| **0x0000** | STEP_ADVANCE | Triggers HEPC Hardware Scan (MMIO Write) |
| **0x0008** | WEIGHT_S | Lookahead window (W) for Step Proximity |
| **0x0020** | RTBD_SHARE | Hardware Ref-Count increment for prefix blocks |
| **0x0100** | NMCE_DESC | Descriptor write for Attention Score Compute |
| **0x2000...** | RTBD_CAM | Direct SRAM access for 65k Tag Entries |

---

## IV. Priority Logic (SystemVerilog Snippet)

```systemverilog
always_comb begin
  // Priority Scoring Logic [Pillar II]
  p_score = (w_r * R) + (w_f * F) + (w_s * (255 - step_diff)) + (w_d * is_prefix);

  // DMA Controller Triggering [Pillar IV]
  if (p_score < EVICT_THRESH && ref_count == 0) 
    send_io_uring_evict(block_id);
end
```

---

## V. Packaging & Thermal Specification

- **Process Node:** TSMC N5/N7 FinFET.
- **Voltage (Core):</strong> 0.75V Vdd.
- **TDP (Active):** 20W - 28W Peak.
- **Form Factor:** PCIe HHHL / CXL EDSFF E3.S.

---

**Patent Information (India):**  
Docket No 65779 | App Number 202641056309  
Reference Number TEMP/E1/61503/2026-CHE | CBR Number 37184  
CONFIDENTIAL - FILED UNDER THE PATENTS ACT, 1970
