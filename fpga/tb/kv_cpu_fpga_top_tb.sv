// Basic integration testbench for the Phase 1 FPGA top-level shell.
`timescale 1ns/1ps

module kv_cpu_fpga_top_tb;
    logic clk;
    logic rst_n;
    logic mmio_step_valid;
    logic [31:0] mmio_step_value;
    logic hepc_scan_pulse;
    logic [31:0] observed_step;

    kv_cpu_fpga_top dut (
        .clk(clk),
        .rst_n(rst_n),
        .mmio_step_valid(mmio_step_valid),
        .mmio_step_value(mmio_step_value),
        .hepc_scan_pulse(hepc_scan_pulse),
        .observed_step(observed_step)
    );

    initial clk = 0;
    always #0.5 clk = ~clk;

    initial begin
        rst_n = 0;
        mmio_step_valid = 0;
        mmio_step_value = '0;

        #2 rst_n = 1;

        mmio_step_value = 32'd128;
        mmio_step_valid = 1'b1;
        #1 mmio_step_valid = 1'b0;

        #1 assert(observed_step == 32'd128)
            else $error("MMIO step value was not captured.");

        #1 assert(hepc_scan_pulse == 1'b0)
            else $error("Scan pulse did not deassert after the write.");

        #5 $finish;
    end
endmodule
