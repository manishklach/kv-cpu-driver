// KV-CPU NMCE Dot-Product Unit (DPU) Array
// Optimized for TSMC N5/N7 | Ref: kv_cpu_patent_v3.pdf
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
    logic signed [31:0] dot_prod     [B-1:0];
    logic signed [31:0] scaled_score [B-1:0];
    logic        [15:0] approx_score [B-1:0];

    genvar i;

    // 128-wide systolic MAC array
    // Computes Score[i] = exp((Q . K[i]) * Scale) via the PWL approximation unit.
    generate
        for (i = 0; i < B; i++) begin : gen_nmce_scores
            always_comb begin
                dot_prod[i] = '0;
                for (int j = 0; j < D; j++)
                    dot_prod[i] += $signed(q_vec[j]) * $signed(k_block[i][j]);
            end

            assign scaled_score[i] = dot_prod[i] * $signed({16'd0, scale_factor});

            nmce_exp_approx_pwl exp_unit (
                .x_in(scaled_score[i]),
                .y_out(approx_score[i])
            );
        end
    endgenerate

    always_ff @(posedge clk) begin
        if (!rst_n)
            scores <= '{default: 0};
        else
            scores <= approx_score;
    end
endmodule
