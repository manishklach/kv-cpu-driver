// KV-CPU FPGA top-level shell for Phase 1 emulation.
`timescale 1ns/1ps

module kv_cpu_fpga_top (
    input  logic        clk,
    input  logic        rst_n,
    input  logic        mmio_step_valid,
    input  logic [31:0] mmio_step_value,
    output logic        hepc_scan_pulse,
    output logic [31:0] observed_step
);
    mmio_regs mmio_regs_inst (
        .clk(clk),
        .rst_n(rst_n),
        .step_valid(mmio_step_valid),
        .step_value(mmio_step_value),
        .hepc_scan_pulse(hepc_scan_pulse),
        .observed_step(observed_step)
    );
endmodule
