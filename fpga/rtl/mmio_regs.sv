// Minimal MMIO-facing register block for Phase 1 FPGA emulation.
`timescale 1ns/1ps

module mmio_regs (
    input  logic        clk,
    input  logic        rst_n,
    input  logic        step_valid,
    input  logic [31:0] step_value,
    output logic        hepc_scan_pulse,
    output logic [31:0] observed_step
);
    always_ff @(posedge clk) begin
        if (!rst_n) begin
            hepc_scan_pulse <= 1'b0;
            observed_step <= '0;
        end else begin
            hepc_scan_pulse <= step_valid;
            if (step_valid)
                observed_step <= step_value;
        end
    end
endmodule
