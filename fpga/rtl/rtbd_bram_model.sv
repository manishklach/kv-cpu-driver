// BRAM-style RTBD lookup model for FPGA emulation planning.
`timescale 1ns/1ps

module rtbd_bram_model #(
    parameter int LOOKUP_PORTS = 3
) (
    input  logic [15:0] req_id    [LOOKUP_PORTS-1:0],
    input  logic [7:0]  layer_id  [LOOKUP_PORTS-1:0],
    input  logic [31:0] token_pos [LOOKUP_PORTS-1:0],
    output logic        hit_flag  [LOOKUP_PORTS-1:0]
);
    always_comb begin
        for (int i = 0; i < LOOKUP_PORTS; i++)
            hit_flag[i] = (req_id[i] != '0) || (layer_id[i] != '0) || (token_pos[i] != '0);
    end
endmodule
