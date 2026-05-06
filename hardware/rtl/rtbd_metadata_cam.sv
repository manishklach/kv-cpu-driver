// KV-CPU RTBD metadata lookup engine.
// Behavioral CAM-style model that resolves the metadata consumed by HEPC.
`timescale 1ns/1ps

module rtbd_metadata_cam (
    input  logic        lookup_valid,
    input  logic [15:0] req_id,
    input  logic [15:0] layer_head_idx,
    input  logic [31:0] token_pos,
    output logic        hit_flag,
    output logic [1:0]  tier_loc,
    output logic [63:0] phys_addr,
    output logic [7:0]  R_count,
    output logic [7:0]  F_count,
    output logic [31:0] last_access_step,
    output logic        is_prefix,
    output logic [7:0]  ref_count
);
    typedef struct packed {
        logic [15:0] req_id;
        logic [15:0] layer_head_idx;
        logic [31:0] token_start;
        logic [31:0] token_end;
        logic [1:0]  tier_loc;
        logic [63:0] phys_addr;
        logic [7:0]  R_count;
        logic [7:0]  F_count;
        logic [31:0] last_access_step;
        logic        is_prefix;
        logic [7:0]  ref_count;
    } rtbd_meta_entry_t;

    localparam rtbd_meta_entry_t ENTRY_PREFIX = '{
        req_id: 16'h0000,
        layer_head_idx: 16'h0C00,
        token_start: 32'd0,
        token_end: 32'd2047,
        tier_loc: 2'd0,
        phys_addr: 64'h0000_0000_4000_0000,
        R_count: 8'd220,
        F_count: 8'd255,
        last_access_step: 32'd998,
        is_prefix: 1'b1,
        ref_count: 8'd7
    };

    localparam rtbd_meta_entry_t ENTRY_REQ_A = '{
        req_id: 16'hA001,
        layer_head_idx: 16'h0C00,
        token_start: 32'd1024,
        token_end: 32'd1535,
        tier_loc: 2'd1,
        phys_addr: 64'h0000_0001_1000_0000,
        R_count: 8'd32,
        F_count: 8'd18,
        last_access_step: 32'd960,
        is_prefix: 1'b0,
        ref_count: 8'd1
    };

    localparam rtbd_meta_entry_t ENTRY_REQ_C = '{
        req_id: 16'hB055,
        layer_head_idx: 16'h0C00,
        token_start: 32'd2048,
        token_end: 32'd2559,
        tier_loc: 2'd2,
        phys_addr: 64'h0000_0002_2000_0000,
        R_count: 8'd12,
        F_count: 8'd6,
        last_access_step: 32'd640,
        is_prefix: 1'b0,
        ref_count: 8'd1
    };

    rtbd_meta_entry_t matched_entry;

    function automatic logic entry_match(
        input rtbd_meta_entry_t entry,
        input logic [15:0] req_id_in,
        input logic [15:0] layer_head_idx_in,
        input logic [31:0] token_pos_in
    );
    begin
        entry_match = (req_id_in == entry.req_id) &&
                      (layer_head_idx_in == entry.layer_head_idx) &&
                      (token_pos_in >= entry.token_start) &&
                      (token_pos_in <= entry.token_end);
    end
    endfunction

    always_comb begin
        hit_flag = 1'b0;
        matched_entry = '0;
        tier_loc = '0;
        phys_addr = '0;
        R_count = '0;
        F_count = '0;
        last_access_step = '0;
        is_prefix = 1'b0;
        ref_count = '0;

        if (lookup_valid) begin
            if (entry_match(ENTRY_PREFIX, req_id, layer_head_idx, token_pos)) begin
                matched_entry = ENTRY_PREFIX;
                hit_flag = 1'b1;
            end else if (entry_match(ENTRY_REQ_A, req_id, layer_head_idx, token_pos)) begin
                matched_entry = ENTRY_REQ_A;
                hit_flag = 1'b1;
            end else if (entry_match(ENTRY_REQ_C, req_id, layer_head_idx, token_pos)) begin
                matched_entry = ENTRY_REQ_C;
                hit_flag = 1'b1;
            end
        end

        if (hit_flag) begin
            tier_loc = matched_entry.tier_loc;
            phys_addr = matched_entry.phys_addr;
            R_count = matched_entry.R_count;
            F_count = matched_entry.F_count;
            last_access_step = matched_entry.last_access_step;
            is_prefix = matched_entry.is_prefix;
            ref_count = matched_entry.ref_count;
        end
    end
endmodule
