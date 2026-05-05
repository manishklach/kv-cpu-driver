# Phase 1 FPGA Emulation Plan

## Objective

Phase 1 FPGA emulation exists to validate the KV-CPU control loop in hardware-like conditions before committing to a custom silicon implementation. The emphasis is on proving control semantics, bounded decision latency, and the shape of host-to-device traffic reduction.

## Target Platform

The working assumption for this phase is a Xilinx Alveo U250-class board or equivalent server FPGA platform with:

- Sufficient LUT and BRAM or URAM capacity for HEPC and RTBD emulation structures
- A host-visible PCIe transport path
- On-board memory suitable for staging-tier emulation
- Integrated debug visibility through logic analyzer tooling

## Phase 1 Architecture

The Phase 1 emulator should model the following loop:

1. Host writes a decode-step update via MMIO.
2. FPGA control logic captures the step and triggers a score recomputation pass.
3. RTBD lookup logic resolves request-scoped KV metadata for test traffic.
4. HEPC-style decision logic emits eviction or prefetch trigger intents.
5. Debug counters and probes capture latency and event rates.

## Pillar-to-FPGA Mapping

| Pillar | FPGA Mapping | Phase 1 Intent |
| :--- | :--- | :--- |
| NMCE | DSP-oriented placeholder or simplified model | Validate interfaces and score-return flow |
| HEPC | LUT fabric | Measure bounded score-to-trigger latency |
| RTBD | BRAM/URAM-backed lookup model | Emulate tag lookup and tenant isolation |
| DMA | PCIe DMA IP plus trigger/status registers | Observe transfer intents and counters |

## Recommended Scope Cut

To keep Phase 1 tractable, the emulator should not attempt full production behavior. A good first milestone is:

- MMIO register block for `STEP_ADVANCE` and threshold programming
- BRAM-backed RTBD model rather than a full native CAM
- Small multi-port lookup path sufficient for multi-tenant contention demos
- Trigger registers and counters instead of a full migration engine
- On-board debug instrumentation for timing validation

## Verification Strategy

- Simulate host-triggered decode-step events
- Reuse hardware collateral from `hardware/verification/`
- Add FPGA-integration testbenches for register writes and trigger propagation
- Capture cycle counts from MMIO ingress to trigger assertion

## Deliverables

- Top-level FPGA shell
- MMIO register block
- RTBD BRAM model
- HEPC integration path
- Basic simulation harness
- Board mapping notes and constraints placeholders

## Success Metrics

- Measured MMIO-to-trigger latency within a bounded target window
- Correct trigger behavior for dormant, hot, and shared-prefix cases
- Deterministic multi-tenant lookup isolation
- Clear debug evidence that traffic can be reduced relative to a naive host-managed path
