# Hardware Collateral

This directory collects the hardware-side artifacts that complement the Linux control-plane prototype.

## Layout

- `rtl/`: SystemVerilog source for HEPC and NMCE-related logic blocks.
- `verification/`: Focused SystemVerilog testbenches for hardware control logic.
- `specs/`: Higher-level hardware specifications and narrative design notes.
- `mmio/`: Register-map and RTBD/MMIO layout artifacts.
- `thermal/`: Packaging, thermal, and mechanical notes.
- `diagrams/`: Supporting diagrams and PDF collateral.

## Notes

- The Linux driver in this repository is still a control-plane prototype.
- These hardware files are design collateral, not a complete tapeout-ready implementation.
