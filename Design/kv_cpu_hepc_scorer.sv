// KV-CPU HEPC Priority Scorer & Tier Decision Logic
// Patent Ref: kv_cpu_patent_v3.pdf | Pillar II
/*
 * Docket No 65779
 * App Number 202641056309
 * Reference Number TEMP/E1/61503/2026-CHE
 * CBR Number 37184
 * Country: India
 */
module hepc_priority_engine (
    input  logic clk,
    input  logic rst_n,
    input  logic step_advance_trig, // Triggered by GPU MMIO write
    input  logic [31:0] current_step,
    // Weights from MMIO registers
    input  logic [7:0] w_r, w_f, w_s, w_d,
    // Entry Data from RTBD
    input  logic [7:0]  R_count, F_count,
    input  logic [31:0] last_access_step,
    input  logic        is_prefix,
    input  logic [7:0]  ref_count,
    // Thresholds
    input  logic [15:0] evict_thresh,
    input  logic [15:0] prefetch_thresh,
    // Control Outputs
    output logic trigger_evict_dma,
    output logic trigger_prefetch_dma
);
    logic [15:0] p_score;
    logic [31:0] step_diff;
    logic [7:0]  S_component;

    // 1. Calculate Step Proximity (S)
    assign step_diff = current_step - last_access_step;
    assign S_component = (step_diff < 255) ? (255 - step_diff[7:0]) : 8'h00;

    // 2. Composite Priority Score Calculation
    // Formula: P = wR*R + wF*F + wS*S + wD*D
    always_comb begin
        p_score = (w_r * R_count) + (w_f * F_count) + 
                  (w_s * S_component) + (w_d * (is_prefix ? 8'hFF : 8'h00));
    end

    // 3. Autonomous Tier Decision Logic
    always_ff @(posedge clk) begin
        if (!rst_n) begin
            {trigger_evict_dma, trigger_prefetch_dma} <= 2'b00;
        end else if (step_advance_trig) begin
            // Guard: Never evict active prefixes
            if (is_prefix && ref_count > 0) begin
                trigger_evict_dma <= 1'b0;
            end else begin
                trigger_evict_dma    <= (p_score < evict_thresh);
                trigger_prefetch_dma <= (p_score > prefetch_thresh);
            end
        end
    end
endmodule