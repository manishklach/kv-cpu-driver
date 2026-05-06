// Verilator-friendly smoke test for the RTBD CAM store.
`timescale 1ns/1ps

module rtbd_cam_store_tb;
    localparam int ENTRIES = 16;
    localparam int TAG_WIDTH = 240;
    localparam int INDEX_W = $clog2(ENTRIES);

    logic clk;
    logic rst_n;
    logic lookup_valid;
    logic [TAG_WIDTH-1:0] lookup_tag;
    logic lookup_busy;
    logic lookup_done;
    logic lookup_hit;
    logic [INDEX_W-1:0] lookup_index;
    logic [TAG_WIDTH-1:0] lookup_tag_out;
    logic insert_valid;
    logic [TAG_WIDTH-1:0] insert_tag;
    logic insert_busy;
    logic insert_done;
    logic insert_success;
    logic [INDEX_W-1:0] insert_index;
    logic evict_valid;
    logic [INDEX_W-1:0] evict_index;
    logic evict_busy;
    logic evict_done;

    rtbd_cam_store #(
        .ENTRIES(ENTRIES),
        .TAG_WIDTH(TAG_WIDTH)
    ) dut (
        .clk(clk),
        .rst_n(rst_n),
        .lookup_valid(lookup_valid),
        .lookup_tag(lookup_tag),
        .lookup_busy(lookup_busy),
        .lookup_done(lookup_done),
        .lookup_hit(lookup_hit),
        .lookup_index(lookup_index),
        .lookup_tag_out(lookup_tag_out),
        .insert_valid(insert_valid),
        .insert_tag(insert_tag),
        .insert_busy(insert_busy),
        .insert_done(insert_done),
        .insert_success(insert_success),
        .insert_index(insert_index),
        .evict_valid(evict_valid),
        .evict_index(evict_index),
        .evict_busy(evict_busy),
        .evict_done(evict_done)
    );

    initial clk = 1'b0;
    always #0.5 clk = ~clk;

    task automatic pulse_insert(input logic [TAG_WIDTH-1:0] tag);
    begin
        insert_tag = tag;
        insert_valid = 1'b1;
        @(posedge clk);
        insert_valid = 1'b0;
    end
    endtask

    task automatic pulse_lookup(input logic [TAG_WIDTH-1:0] tag);
    begin
        lookup_tag = tag;
        lookup_valid = 1'b1;
        @(posedge clk);
        lookup_valid = 1'b0;
    end
    endtask

    task automatic pulse_evict(input logic [INDEX_W-1:0] idx);
    begin
        evict_index = idx;
        evict_valid = 1'b1;
        @(posedge clk);
        evict_valid = 1'b0;
    end
    endtask

    task automatic wait_lookup_done;
    begin
        while (!lookup_done)
            @(posedge clk);
    end
    endtask

    task automatic wait_insert_done;
    begin
        while (!insert_done)
            @(posedge clk);
    end
    endtask

    task automatic wait_evict_done;
    begin
        while (!evict_done)
            @(posedge clk);
    end
    endtask

    localparam logic [TAG_WIDTH-1:0] TAG_A = 240'h0001_0C00_0000_0400_0000_05FF_1_0000_0001_1000_0000_0020_0012_0000_03C0_01;
    localparam logic [TAG_WIDTH-1:0] TAG_B = 240'hB055_0C00_0000_0800_0000_09FF_2_0000_0002_2000_0000_000C_0006_0000_0280_01;
    localparam logic [TAG_WIDTH-1:0] TAG_C = 240'h0000_0C00_0000_0000_0000_07FF_0_0000_0000_4000_0000_00DC_00FF_0000_03E6_07;
    localparam logic [TAG_WIDTH-1:0] TAG_MISS = 240'h1234_0C00_0000_1000_0000_10FF_3_0000_0003_3000_0000_0001_0001_0000_0010_01;

    initial begin
        rst_n = 1'b0;
        lookup_valid = 1'b0;
        lookup_tag = '0;
        insert_valid = 1'b0;
        insert_tag = '0;
        evict_valid = 1'b0;
        evict_index = '0;

        repeat (2) @(posedge clk);
        rst_n = 1'b1;

        pulse_insert(TAG_A);
        wait_insert_done();
        if (!insert_success || insert_index != 0)
            $fatal(1, "TAG_A insert failed or landed in wrong slot");

        pulse_insert(TAG_B);
        wait_insert_done();
        if (!insert_success || insert_index != 1)
            $fatal(1, "TAG_B insert failed or landed in wrong slot");

        pulse_lookup(TAG_B);
        wait_lookup_done();
        if (!lookup_hit || lookup_index != 1 || lookup_tag_out != TAG_B)
            $fatal(1, "TAG_B lookup failed");

        pulse_lookup(TAG_MISS);
        wait_lookup_done();
        if (lookup_hit)
            $fatal(1, "TAG_MISS unexpectedly hit");

        pulse_evict(0);
        wait_evict_done();

        pulse_lookup(TAG_A);
        wait_lookup_done();
        if (lookup_hit)
            $fatal(1, "TAG_A unexpectedly survived eviction");

        pulse_insert(TAG_C);
        wait_insert_done();
        if (!insert_success || insert_index != 0)
            $fatal(1, "TAG_C did not reuse the lowest free slot");

        pulse_lookup(TAG_C);
        wait_lookup_done();
        if (!lookup_hit || lookup_index != 0 || lookup_tag_out != TAG_C)
            $fatal(1, "TAG_C lookup failed after reinsertion");

        $display("rtbd_cam_store smoke test passed");
        $finish;
    end
endmodule
