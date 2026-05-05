// KV-CPU RTBD CAM Controller
// Behavioral reference model for multi-tenant lookup verification.
`timescale 1ns/1ps

module rtbd_cam_controller #(
    parameter int ENTRIES = 65536
) (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [15:0] req_id    [2:0],
    input  logic [7:0]  layer_id  [2:0],
    input  logic [31:0] token_pos [2:0],
    output logic [63:0] phys_addr_out [2:0],
    output logic [1:0]  tier_loc_out  [2:0],
    output logic        hit_flag      [2:0]
);
    localparam logic [15:0] PREFIX_REQ_ID = 16'h0000;
    localparam int LOOKUP_PORTS = 3;

    typedef struct packed {
        logic [15:0] req_id;
        logic [7:0]  layer_id;
        logic [31:0] token_pos;
        logic [63:0] phys_addr;
        logic [1:0]  tier_loc;
    } rtbd_entry_t;

    localparam rtbd_entry_t ENTRY_REQ_A = '{
        req_id: 16'hA001,
        layer_id: 8'd12,
        token_pos: 32'd1024,
        phys_addr: 64'h0000_0001_1000_0000,
        tier_loc: 2'd1
    };

    localparam rtbd_entry_t ENTRY_PREFIX = '{
        req_id: PREFIX_REQ_ID,
        layer_id: 8'd12,
        token_pos: 32'd0,
        phys_addr: 64'h0000_0000_4000_0000,
        tier_loc: 2'd0
    };

    localparam rtbd_entry_t ENTRY_REQ_C = '{
        req_id: 16'hB055,
        layer_id: 8'd12,
        token_pos: 32'd2048,
        phys_addr: 64'h0000_0002_2000_0000,
        tier_loc: 2'd2
    };

    rtbd_entry_t matched_entry [LOOKUP_PORTS];

    always_comb begin
        for (int i = 0; i < LOOKUP_PORTS; i++) begin
            phys_addr_out[i] = '0;
            tier_loc_out[i] = '0;
            hit_flag[i] = 1'b0;
            matched_entry[i] = '0;

            if (req_id[i] == ENTRY_REQ_A.req_id &&
                layer_id[i] == ENTRY_REQ_A.layer_id &&
                token_pos[i] == ENTRY_REQ_A.token_pos) begin
                matched_entry[i] = ENTRY_REQ_A;
                hit_flag[i] = 1'b1;
            end else if (req_id[i] == ENTRY_PREFIX.req_id &&
                         layer_id[i] == ENTRY_PREFIX.layer_id &&
                         token_pos[i] == ENTRY_PREFIX.token_pos) begin
                matched_entry[i] = ENTRY_PREFIX;
                hit_flag[i] = 1'b1;
            end else if (req_id[i] == ENTRY_REQ_C.req_id &&
                         layer_id[i] == ENTRY_REQ_C.layer_id &&
                         token_pos[i] == ENTRY_REQ_C.token_pos) begin
                matched_entry[i] = ENTRY_REQ_C;
                hit_flag[i] = 1'b1;
            end

            if (hit_flag[i]) begin
                phys_addr_out[i] = matched_entry[i].phys_addr;
                tier_loc_out[i] = matched_entry[i].tier_loc;
            end
        end
    end

    // Placeholder to keep the interface aligned with future sequential logic.
    always_ff @(posedge clk) begin
        if (!rst_n) begin
            // No sequential state in this behavioral model yet.
        end
    end
endmodule
