# KV-CPU Control Plane Reference Driver v0.1.0

## Overview

This release establishes the first coherent public baseline for the KV-CPU Linux control-plane prototype. It moves the repository from an architectural sketch to a more reviewable systems artifact with:

- a hardened Linux kernel driver skeleton
- a more complete control-plane UAPI
- a growing hardware collateral set
- initial FPGA-emulation scaffolding
- basic smoke-test and CI coverage

The repository remains a reference prototype rather than a production-ready driver or tapeout-ready hardware implementation, but the project now has a clearer shape across software, RTL, verification, and emulation.

## Highlights

### Driver control-plane hardening

- Added stricter argument handling in `kvctl` so malformed commands fail cleanly instead of reading past `argv`.
- Claimed and released PCI BAR resources correctly in the probe/remove lifecycle.
- Fixed an MMIO command-stream race by serializing `STEP_ADVANCE`, `HOT`, `EVICT`, `PREFETCH`, and `SHARE` through a shared spinlock.
- Added a write memory barrier before MMIO writes to avoid ordering hazards on weaker memory-ordering architectures.

### Expanded UAPI and observability

- Added `KV_CPU_SET_WEIGHTS` to program HEPC weights and thresholds from userspace.
- Added `KV_CPU_GET_TELEMETRY` to snapshot command counters, the current decode step, last-command fields, and programmed runtime weights.
- Added a sysfs interface for the most important live control-plane fields:
  - `current_step`
  - `w_r`, `w_f`, `w_s`, `w_d`
  - `evict_thresh`, `prefetch_thresh`
  - `telemetry`

### Mock-mode testing and CI

- Added a mock ioctl smoke test at `tools/test/kvcpu_mock_test.c`.
- Extended the smoke test to exercise:
  - `STEP_ADVANCE`
  - `HOT`
  - `EVICT`
  - `PREFETCH`
  - `SHARE_PREFIX`
  - `SET_WEIGHTS`
  - `GET_TELEMETRY`
  - invalid zero-length rejection
- Added GitHub Actions build coverage that:
  - downloads official Linux 6.11 sources
  - runs `make defconfig && make modules_prepare`
  - builds the external module and userspace tools on every push and PR

### Hardware collateral growth

- Renamed and reorganized the original design collateral into a clearer `hardware/` tree.
- Added subdomains for:
  - `hardware/rtl/`
  - `hardware/specs/`
  - `hardware/mmio/`
  - `hardware/thermal/`
  - `hardware/diagrams/`
  - `hardware/verification/`
- Added a hardware index README and several new supporting hardware artifacts, including:
  - HEPC and NMCE SystemVerilog sources
  - MMIO / RTBD collateral
  - thermal and packaging collateral
  - diagram PDFs
  - verification benches

### RTL completeness improvements

- Added an 8-segment piecewise-linear softmax numerator approximation block:
  - `hardware/rtl/nmce_exp_approx_pwl.sv`
- Removed the undefined `approx_exp()` hole from the NMCE RTL by wiring the approximation unit into:
  - `hardware/rtl/nmce_dpu_array.sv`
  - `hardware/rtl/kv_cpu_nmce_dpu.sv`
- Added a behavioral RTBD CAM controller reference model for multi-tenant lookup verification.
- Added an RTBD metadata CAM and HEPC-facing wrapper so the scoring engine is no longer detached from a metadata source:
  - `hardware/rtl/rtbd_metadata_cam.sv`
  - `hardware/rtl/hepc_rtbd_pipeline.sv`

### Verification collateral

- Added a HEPC priority engine testbench:
  - `hardware/verification/kv_cpu_hepc_tb.sv`
- Added a multi-tenant RTBD contention bench:
  - `hardware/verification/kv_cpu_multi_tenant_tb.sv`

### FPGA emulation track

- Added a top-level `fpga/` tree to make Phase 1 emulation a first-class part of the repo rather than a buried note.
- Added:
  - `fpga/docs/phase1_emulation_plan.md`
  - `fpga/docs/alveo_u250_mapping.md`
  - `fpga/top/kv_cpu_fpga_top.sv`
  - starter support RTL and a simple FPGA top-level testbench

## Included in this release

Representative areas shipped in the `v0.1.0` baseline include:

- Linux control-plane driver skeleton under `drivers/misc/kv_cpu/`
- UAPI header under `include/uapi/linux/kv_cpu.h`
- userspace tools under `tools/`
- hardware collateral under `hardware/`
- FPGA emulation scaffolding under `fpga/`
- CI workflow under `.github/workflows/`

## Known limitations

- No real DMA engine implementation exists yet; DMA paths remain signaling-only.
- No real page pinning or VA-to-PA translation path is implemented.
- The RTBD and CAM-related RTL blocks are still behavioral reference models, not complete production-grade implementations.
- The FPGA tree is an emulation scaffold, not a ready-to-flash board project.
- The hardware RTL was updated statically in this environment; no local Verilog compiler was available here for compile-time validation.

## Suggested next steps

- Add a small in-kernel or userspace mock-mode validation script that loads the module and runs the smoke test automatically on Linux.
- Expand CI to run the mock smoke test on an actual Linux runner with module loading enabled.
- Add a minimal RTBD/HEPC integration testbench.
- Flesh out the FPGA path with board-specific scripts and constraints.
- Continue replacing behavioral placeholder RTL with more explicit synthesizable datapath blocks.
