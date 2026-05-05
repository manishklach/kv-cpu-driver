// Placeholder register block for observing DMA-intent events in FPGA emulation.
`timescale 1ns/1ps

module dma_trigger_regs (
    input  logic clk,
    input  logic rst_n,
    input  logic trigger_evict,
    input  logic trigger_prefetch,
    output logic [31:0] evict_count,
    output logic [31:0] prefetch_count
);
    always_ff @(posedge clk) begin
        if (!rst_n) begin
            evict_count <= '0;
            prefetch_count <= '0;
        end else begin
            if (trigger_evict)
                evict_count <= evict_count + 1'b1;
            if (trigger_prefetch)
                prefetch_count <= prefetch_count + 1'b1;
        end
    end
endmodule
