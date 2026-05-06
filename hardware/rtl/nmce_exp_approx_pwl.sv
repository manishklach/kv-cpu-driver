// KV-CPU NMCE softmax numerator approximation unit.
// Interprets x_in as a signed Q16.16 score and emits a Q1.15 exp() estimate.
`timescale 1ns/1ps

module nmce_exp_approx_pwl (
    input  logic signed [31:0] x_in,
    output logic        [15:0] y_out
);
    localparam logic signed [31:0] X_NEG_8 = -(32'sd8 <<< 16);
    localparam logic signed [31:0] X_NEG_7 = -(32'sd7 <<< 16);
    localparam logic signed [31:0] X_NEG_6 = -(32'sd6 <<< 16);
    localparam logic signed [31:0] X_NEG_5 = -(32'sd5 <<< 16);
    localparam logic signed [31:0] X_NEG_4 = -(32'sd4 <<< 16);
    localparam logic signed [31:0] X_NEG_3 = -(32'sd3 <<< 16);
    localparam logic signed [31:0] X_NEG_2 = -(32'sd2 <<< 16);
    localparam logic signed [31:0] X_NEG_1 = -(32'sd1 <<< 16);
    localparam logic signed [31:0] X_ZERO  = 32'sd0;

    // Knot values in Q1.15, approximating exp(x) on [-8, 0].
    localparam logic [15:0] Y_NEG_8 = 16'd11;
    localparam logic [15:0] Y_NEG_7 = 16'd30;
    localparam logic [15:0] Y_NEG_6 = 16'd81;
    localparam logic [15:0] Y_NEG_5 = 16'd221;
    localparam logic [15:0] Y_NEG_4 = 16'd600;
    localparam logic [15:0] Y_NEG_3 = 16'd1631;
    localparam logic [15:0] Y_NEG_2 = 16'd4435;
    localparam logic [15:0] Y_NEG_1 = 16'd12055;
    localparam logic [15:0] Y_ZERO  = 16'd32767;

    function automatic [15:0] interp_segment(
        input logic [15:0] y0,
        input logic [15:0] y1,
        input logic signed [31:0] delta_q16
    );
        logic [31:0] accum;
        logic [15:0] dy;
    begin
        dy = y1 - y0;
        accum = y0 + ((dy * delta_q16[15:0]) >> 16);
        interp_segment = accum[15:0];
    end
    endfunction

    always_comb begin
        if (x_in <= X_NEG_8) begin
            y_out = Y_NEG_8;
        end else if (x_in < X_NEG_7) begin
            y_out = interp_segment(Y_NEG_8, Y_NEG_7, x_in - X_NEG_8);
        end else if (x_in < X_NEG_6) begin
            y_out = interp_segment(Y_NEG_7, Y_NEG_6, x_in - X_NEG_7);
        end else if (x_in < X_NEG_5) begin
            y_out = interp_segment(Y_NEG_6, Y_NEG_5, x_in - X_NEG_6);
        end else if (x_in < X_NEG_4) begin
            y_out = interp_segment(Y_NEG_5, Y_NEG_4, x_in - X_NEG_5);
        end else if (x_in < X_NEG_3) begin
            y_out = interp_segment(Y_NEG_4, Y_NEG_3, x_in - X_NEG_4);
        end else if (x_in < X_NEG_2) begin
            y_out = interp_segment(Y_NEG_3, Y_NEG_2, x_in - X_NEG_3);
        end else if (x_in < X_NEG_1) begin
            y_out = interp_segment(Y_NEG_2, Y_NEG_1, x_in - X_NEG_2);
        end else if (x_in < X_ZERO) begin
            y_out = interp_segment(Y_NEG_1, Y_ZERO, x_in - X_NEG_1);
        end else begin
            y_out = Y_ZERO;
        end
    end
endmodule
