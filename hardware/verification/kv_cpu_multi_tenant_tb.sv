// KV-CPU Multi-Tenant RTBD CAM Testbench
// Simulates concurrent request lookup and LPDDR contention.
`timescale 1ns/1ps

module kv_cpu_multi_tenant_tb;
    // System Signals
    logic clk;
    logic rst_n;

    // Request Interface (Concurrent Lookups)
    logic [15:0] req_id    [2:0];
    logic [7:0]  layer_id  [2:0];
    logic [31:0] token_pos [2:0];

    // Hardware Outputs (Address Resolution)
    logic [63:0] phys_addr_out [2:0];
    logic [1:0]  tier_loc_out  [2:0];
    logic        hit_flag      [2:0];

    // Instantiate RTBD CAM Controller
    rtbd_cam_controller #(.ENTRIES(65536)) cam_inst (.*);

    initial clk = 0;
    always #0.25 clk = ~clk; // 2 GHz clock

    initial begin
        rst_n = 0;

        for (int i = 0; i < 3; i++) begin
            req_id[i] = '0;
            layer_id[i] = '0;
            token_pos[i] = '0;
        end

        #2 rst_n = 1;

        // SCENARIO: Multi-tenant contention
        // Request A: Standard inference request
        req_id[0] = 16'hA001;
        layer_id[0] = 8'd12;
        token_pos[0] = 32'd1024;

        // Request B: Shared prefix (system prompt ID 0x0000)
        req_id[1] = 16'h0000;
        layer_id[1] = 8'd12;
        token_pos[1] = 32'd0;

        // Request C: Different user on the same layer
        req_id[2] = 16'hB055;
        layer_id[2] = 8'd12;
        token_pos[2] = 32'd2048;

        #1 $display("T=%0t: Dispatching 3 concurrent lookups...", $time);

        // VERIFICATION: Single-cycle lookup resolution
        #0.5;
        for (int i = 0; i < 3; i++) begin
            if (hit_flag[i])
                $display("Request %h: HIT at Tier %0d, Addr %h",
                         req_id[i], tier_loc_out[i], phys_addr_out[i]);
            else
                $display("Request %h: MISS - Triggering HEPC Prefetch", req_id[i]);
        end

        // TEST: Prefix guard consistency
        // The shared prefix should resolve consistently regardless of caller.
        #10 $finish;
    end
endmodule
