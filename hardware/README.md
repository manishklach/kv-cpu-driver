# Hardware Collateral

This directory collects the hardware-side artifacts that complement the Linux control-plane prototype.

## Layout

- `rtl/`: SystemVerilog source for HEPC, RTBD, and NMCE-related logic blocks.
- `verification/`: Focused SystemVerilog testbenches for hardware control logic.
- `specs/`: Higher-level hardware specifications and narrative design notes.
- `mmio/`: Register-map and RTBD/MMIO layout artifacts.
- `thermal/`: Packaging, thermal, and mechanical notes.
- `diagrams/`: Supporting diagrams and PDF collateral.

## Notes

- The Linux driver in this repository is still a control-plane prototype.
- These hardware files are design collateral, not a complete tapeout-ready implementation.
- Some RTL blocks are currently behavioral reference models used to support simulation and interface validation before fuller hardware implementation.
- Some specification material is versioned as separate snapshots when it introduces substantial architectural additions such as multi-tenant arbitration.

## Current Anchors

- `rtl/rtbd_cam_store.sv` is a synthesizable reference RTBD tag-store with insert, lookup, and evict flows over 65,536 entries and 240-bit tags, returning the lowest-index match as the effective priority-encoded result.
- `verification/rtbd_cam_store_tb.sv` provides a Verilator-friendly smoke test for insert, lookup, miss, evict, and slot reuse behavior.
