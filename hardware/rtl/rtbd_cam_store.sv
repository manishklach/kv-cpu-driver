// KV-CPU RTBD CAM store.
//
// This is a synthesizable reference tag-store microarchitecture for partner
// evaluation. It models a 240-bit RTBD tag with insert, lookup, and evict
// operations, returning the lowest-index matching entry as the effective
// priority-encoded match. The lookup implementation is a sequential scan
// reference, not a final parallel match-line CAM implementation.
`timescale 1ns/1ps

module rtbd_cam_store #(
    parameter int ENTRIES = 65536,
    parameter int TAG_WIDTH = 240
) (
    input  logic                         clk,
    input  logic                         rst_n,
    input  logic                         lookup_valid,
    input  logic [TAG_WIDTH-1:0]         lookup_tag,
    output logic                         lookup_busy,
    output logic                         lookup_done,
    output logic                         lookup_hit,
    output logic [$clog2(ENTRIES)-1:0]   lookup_index,
    output logic [TAG_WIDTH-1:0]         lookup_tag_out,
    input  logic                         insert_valid,
    input  logic [TAG_WIDTH-1:0]         insert_tag,
    output logic                         insert_busy,
    output logic                         insert_done,
    output logic                         insert_success,
    output logic [$clog2(ENTRIES)-1:0]   insert_index,
    input  logic                         evict_valid,
    input  logic [$clog2(ENTRIES)-1:0]   evict_index,
    output logic                         evict_busy,
    output logic                         evict_done
);
    localparam int INDEX_W = $clog2(ENTRIES);

    typedef enum logic [1:0] {
        RTBD_IDLE,
        RTBD_LOOKUP,
        RTBD_INSERT,
        RTBD_EVICT
    } rtbd_state_t;

    rtbd_state_t state;
    logic [TAG_WIDTH-1:0] tag_mem [0:ENTRIES-1];
    logic                  valid_mem [0:ENTRIES-1];
    logic [INDEX_W-1:0]    scan_idx;
    logic [TAG_WIDTH-1:0]  active_lookup_tag;
    logic [TAG_WIDTH-1:0]  active_insert_tag;
    integer                i;

    assign lookup_busy = (state == RTBD_LOOKUP);
    assign insert_busy = (state == RTBD_INSERT);
    assign evict_busy = (state == RTBD_EVICT);

    always_ff @(posedge clk) begin
        if (!rst_n) begin
            state <= RTBD_IDLE;
            scan_idx <= '0;
            active_lookup_tag <= '0;
            active_insert_tag <= '0;
            lookup_done <= 1'b0;
            lookup_hit <= 1'b0;
            lookup_index <= '0;
            lookup_tag_out <= '0;
            insert_done <= 1'b0;
            insert_success <= 1'b0;
            insert_index <= '0;
            evict_done <= 1'b0;
            for (i = 0; i < ENTRIES; i = i + 1) begin
                valid_mem[i] <= 1'b0;
                tag_mem[i] <= '0;
            end
        end else begin
            lookup_done <= 1'b0;
            insert_done <= 1'b0;
            evict_done <= 1'b0;

            case (state)
            RTBD_IDLE: begin
                if (lookup_valid) begin
                    state <= RTBD_LOOKUP;
                    scan_idx <= '0;
                    active_lookup_tag <= lookup_tag;
                    lookup_hit <= 1'b0;
                    lookup_index <= '0;
                    lookup_tag_out <= '0;
                end else if (insert_valid) begin
                    state <= RTBD_INSERT;
                    scan_idx <= '0;
                    active_insert_tag <= insert_tag;
                    insert_success <= 1'b0;
                    insert_index <= '0;
                end else if (evict_valid) begin
                    state <= RTBD_EVICT;
                    scan_idx <= evict_index;
                end
            end

            RTBD_LOOKUP: begin
                if (valid_mem[scan_idx] && tag_mem[scan_idx] == active_lookup_tag) begin
                    lookup_done <= 1'b1;
                    lookup_hit <= 1'b1;
                    lookup_index <= scan_idx;
                    lookup_tag_out <= tag_mem[scan_idx];
                    state <= RTBD_IDLE;
                end else if (scan_idx == ENTRIES - 1) begin
                    lookup_done <= 1'b1;
                    lookup_hit <= 1'b0;
                    lookup_index <= '0;
                    lookup_tag_out <= '0;
                    state <= RTBD_IDLE;
                end else begin
                    scan_idx <= scan_idx + 1'b1;
                end
            end

            RTBD_INSERT: begin
                if (!valid_mem[scan_idx]) begin
                    valid_mem[scan_idx] <= 1'b1;
                    tag_mem[scan_idx] <= active_insert_tag;
                    insert_done <= 1'b1;
                    insert_success <= 1'b1;
                    insert_index <= scan_idx;
                    state <= RTBD_IDLE;
                end else if (scan_idx == ENTRIES - 1) begin
                    insert_done <= 1'b1;
                    insert_success <= 1'b0;
                    insert_index <= '0;
                    state <= RTBD_IDLE;
                end else begin
                    scan_idx <= scan_idx + 1'b1;
                end
            end

            RTBD_EVICT: begin
                valid_mem[scan_idx] <= 1'b0;
                tag_mem[scan_idx] <= '0;
                evict_done <= 1'b1;
                state <= RTBD_IDLE;
            end

            default: begin
                state <= RTBD_IDLE;
            end
            endcase
        end
    end
endmodule
