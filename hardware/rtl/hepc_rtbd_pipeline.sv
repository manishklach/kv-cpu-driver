// KV-CPU HEPC + RTBD integration wrapper.
// Connects address-tag lookup metadata to the HEPC scoring engine.
`timescale 1ns/1ps

module hepc_rtbd_pipeline (
    input  logic        clk,
    input  logic        rst_n,
    input  logic        step_advance_trig,
    input  logic [31:0] current_step,
    input  logic [7:0]  w_r,
    input  logic [7:0]  w_f,
    input  logic [7:0]  w_s,
    input  logic [7:0]  w_d,
    input  logic [15:0] evict_thresh,
    input  logic [15:0] prefetch_thresh,
    input  logic        lookup_valid,
    input  logic [15:0] req_id,
    input  logic [15:0] layer_head_idx,
    input  logic [31:0] token_pos,
    output logic        rtbd_hit,
    output logic [1:0]  tier_loc,
    output logic [63:0] phys_addr,
    output logic        trigger_evict_dma,
    output logic        trigger_prefetch_dma
);
    logic [7:0]  R_count;
    logic [7:0]  F_count;
    logic [31:0] last_access_step;
    logic        is_prefix;
    logic [7:0]  ref_count;

    rtbd_metadata_cam rtbd_lookup (
        .lookup_valid(lookup_valid),
        .req_id(req_id),
        .layer_head_idx(layer_head_idx),
        .token_pos(token_pos),
        .hit_flag(rtbd_hit),
        .tier_loc(tier_loc),
        .phys_addr(phys_addr),
        .R_count(R_count),
        .F_count(F_count),
        .last_access_step(last_access_step),
        .is_prefix(is_prefix),
        .ref_count(ref_count)
    );

    hepc_priority_engine hepc (
        .clk(clk),
        .rst_n(rst_n),
        .step_advance_trig(step_advance_trig & rtbd_hit),
        .current_step(current_step),
        .w_r(w_r),
        .w_f(w_f),
        .w_s(w_s),
        .w_d(w_d),
        .R_count(R_count),
        .F_count(F_count),
        .last_access_step(last_access_step),
        .is_prefix(is_prefix),
        .ref_count(ref_count),
        .evict_thresh(evict_thresh),
        .prefetch_thresh(prefetch_thresh),
        .trigger_evict_dma(trigger_evict_dma),
        .trigger_prefetch_dma(trigger_prefetch_dma)
    );
endmodule
