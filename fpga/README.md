# Phase 1 FPGA Emulation

This directory captures the first-stage FPGA emulation plan for the KV-CPU architecture.

## Goal

Phase 1 is intended to validate the closed-loop control path before a silicon tape-out:

- Host MMIO writes reach the emulated device
- HEPC-style score recomputation is triggered deterministically
- RTBD lookup and multi-tenant isolation behave as expected
- Eviction and prefetch trigger paths are observable in simulation and on-board debug

## Scope

This is an emulation-oriented track, not a full chip implementation. The initial focus is:

- `docs/`: Emulation plan and platform mapping notes
- `top/`: FPGA top-level integration shells
- `rtl/`: Emulation-specific support logic such as MMIO registers and BRAM-backed lookup models
- `tb/`: Simulation harnesses for the FPGA integration layer
- `constraints/`: Placeholder for board timing and pin constraints
- `scripts/`: Placeholder for synthesis and build automation

## Platform Direction

The current planning documents assume a Xilinx Alveo U250-class accelerator as the primary Phase 1 target, with PCIe DMA IP used as the host-facing transport for MMIO-style control.

## Success Criteria

- Bounded MMIO-to-trigger latency
- Deterministic score update behavior
- Repeatable multi-tenant lookup behavior
- Observable trigger statistics and debug counters
