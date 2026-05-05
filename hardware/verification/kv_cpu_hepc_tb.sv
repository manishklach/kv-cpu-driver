// KV-CPU Phase 1 Verification Testbench
// Functional simulation for HEPC priority scoring and DMA trigger logic.
`timescale 1ns/1ps

module kv_cpu_hepc_tb;
    // Clock and Reset
    logic clk;
    logic rst_n;

    // MMIO Register Inputs
    logic step_advance_trig;
    logic [31:0] current_step;
    logic [7:0] w_r, w_f, w_s, w_d;
    logic [15:0] evict_thresh, prefetch_thresh;

    // RTBD Entry Mock Data
    logic [7:0]  R_count, F_count;
    logic [31:0] last_access_step;
    logic        is_prefix;
    logic [7:0]  ref_count;

    // Hardware Outputs
    logic trigger_evict_dma;
    logic trigger_prefetch_dma;

    // Instantiate HEPC Priority Engine
    hepc_priority_engine dut (.*);

    // Clock Generation (2 GHz for 0.5 ns period)
    initial clk = 0;
    always #0.25 clk = ~clk;

    initial begin
        // Initialize Signals
        rst_n = 0;
        step_advance_trig = 0;
        current_step = 0;
        w_r = 8'd1;
        w_f = 8'd1;
        w_s = 8'd2;
        w_d = 8'd200;
        evict_thresh = 16'h1000;
        prefetch_thresh = 16'hE000;
        R_count = 0;
        F_count = 0;
        last_access_step = 0;
        is_prefix = 0;
        ref_count = 0;

        #2 rst_n = 1;

        // TEST CASE 1: Active Prefix Guard
        // Shared prefixes with a non-zero refcount must not be evicted.
        is_prefix = 1;
        ref_count = 8'd5;
        R_count = 8'd10;
        F_count = 8'd0;
        last_access_step = 32'd0;
        step_advance_trig = 1;
        #0.5 step_advance_trig = 0;

        #1 assert(trigger_evict_dma == 0)
            else $error("Error: Prefix block incorrectly scheduled for eviction.");

        // TEST CASE 2: Autonomous Eviction Trigger
        // A dormant non-prefix block should fall below the eviction threshold.
        is_prefix = 0;
        ref_count = 0;
        current_step = 32'd1000;
        last_access_step = 32'd500;
        R_count = 8'd20;
        F_count = 8'd0;

        step_advance_trig = 1;
        #0.5 step_advance_trig = 0;

        #1 if (trigger_evict_dma)
              $display("Success: HEPC triggered async DMA eviction for dormant block.");
            else
              $error("Error: Dormant block did not trigger eviction.");

        // TEST CASE 3: Predictive Prefetch
        // A recent, high-recency block should be promoted for prefetch.
        current_step = 32'd1000;
        last_access_step = 32'd998;
        R_count = 8'd250;
        F_count = 8'd250;
        is_prefix = 0;
        ref_count = 0;

        step_advance_trig = 1;
        #0.5 step_advance_trig = 0;

        #1 if (trigger_prefetch_dma)
              $display("Success: HEPC triggered prefetch for high-priority block.");
            else
              $error("Error: High-priority block did not trigger prefetch.");

        #10 $finish;
    end
endmodule
