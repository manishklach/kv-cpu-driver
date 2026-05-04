// KV-CPU NMCE Dot-Product Unit (DPU) Array
// Optimized for TSMC N5/N7 | Ref: kv_cpu_patent_v3.pdf
/*
 * Docket No 65779
 * App Number 202641056309
 * Reference Number TEMP/E1/61503/2026-CHE
 * CBR Number 37184
 * Country: India
 */
module nmce_dpu_array #(
    parameter D = 128,      // Head Dimension
    parameter B = 32        // KV Block Size
)(
    input  logic clk,
    input  logic rst_n,
    input  logic [15:0] q_vec [D-1:0],    // Query Vector (FP16)
    input  logic [15:0] k_block [B-1:0][D-1:0], // Key Block (Local SRAM)
    input  logic [15:0] scale_factor,      // 1/sqrt(D)
    output logic [15:0] scores [B-1:0]     // Scalar results to GPU
);
    // 128-wide systolic MAC array
    // Computes Score[i] = (Q . K[i]) * Scale
    always_ff @(posedge clk) begin
        if (!rst_n) begin
            scores <= '{default: 0};
        end else begin
            for (int i = 0; i < B; i++) begin
                logic [31:0] dot_prod;
                dot_prod = 0;
                for (int j = 0; j < D; j++) begin
                    dot_prod += q_vec[j] * k_block[i][j]; // MAC operation
                end
                // Apply scaling and pass to Exp() Approximation Unit
                scores[i] <= approx_exp(dot_prod * scale_factor);
            end
        end
    end
endmodule