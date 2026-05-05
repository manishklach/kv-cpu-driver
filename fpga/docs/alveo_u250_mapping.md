# Alveo U250 Mapping Notes

## Why This Class of Board

An Alveo U250-class board is a reasonable Phase 1 platform because it offers:

- Large logic capacity for control-heavy datapath emulation
- PCIe connectivity for host-visible MMIO and DMA-style integration
- On-board memory suitable for staging-tier experiments
- Mature tooling for debug and hardware inspection

## Proposed Mapping

| KV-CPU Element | FPGA Resource | Notes |
| :--- | :--- | :--- |
| MMIO Control Plane | PCIe BAR + register block | Carries decode-step and lifecycle hints |
| HEPC Scoring | LUT fabric | Favor timing clarity over aggressive optimization in Phase 1 |
| RTBD Lookup | BRAM or URAM model | Behaves like a bounded lookup structure, not a full native CAM |
| DMA Trigger Path | PCIe DMA IP + counters | Enough to validate trigger generation and telemetry |
| Debug Visibility | ILA probes | Used to measure trigger latency and inspect control flow |

## Non-Goals for the First Cut

- Full LPDDR5X behavior fidelity
- Production-quality CXL.mem semantics
- Full NUMA or HMAT integration
- Final timing closure for a silicon-equivalent datapath

## Practical Bring-Up Order

1. Clock, reset, and PCIe shell
2. MMIO register block
3. HEPC trigger path
4. RTBD lookup model
5. Trigger counters and debug probes
6. Optional DMA-intent path
