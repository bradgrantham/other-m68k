// tb_sram.v -- behavioural 128Kx8 70 ns SRAM (KM681000-7 class) with timing checks.
//
// Read timing  : tAA 70 (address to data), tOE 35 (OE low to data), tOHZ 25 (OE high
//                to Hi-Z).  Outputs are driven (as X) as soon as OE is low and WE high,
//                become valid when both tAA and tOE have elapsed, and float tOHZ after OE
//                rises.  Address changes while outputs are enabled make the data X again
//                for tAA.
// Write timing : checked on every WE rising edge: tWP >= 50, tDW >= 30, data not X/Z,
//                address stable throughout the WE low pulse (tAS/tWR >= 0); data must not
//                change before WE rises (tDH >= 0 is implied).
// Violations are reported with $display and counted in `errors`.
`timescale 1ns/1ps

module tb_sram #(
    parameter NAME = "sram",
    parameter real tAA  = 70.0,
    parameter real tOE  = 35.0,
    parameter real tOHZ = 25.0,
    parameter real tWP  = 50.0,
    parameter real tDW  = 30.0
) (
    input  wire [16:0] a,
    inout  wire [7:0]  d,
    input  wire        ce_n,
    input  wire        oe_n,
    input  wire        we_n,
    output wire        driving
);

reg [7:0] mem [0:131071];
integer   errors = 0;
integer   writes = 0;
integer   reads  = 0;
time      min_twp = 1000000;    // smallest WE pulse seen
time      min_tdw = 1000000;    // smallest data-valid-to-WE-high seen

// ---- read path ------------------------------------------------------------------
// "valid after" idiom: a counter is bumped on every change and a delayed copy catches
// up tX later; equality means the last change is at least tX old (exact, even if the
// signal changes again inside the window).
integer a_seq = 0,  a_seq_d = 0;       // address age
integer oe_seq = 0, oe_seq_d = 0;      // OE/WE/CE enable age
wire    rd_en = ~ce_n & ~oe_n & we_n;

always @(a) begin
    a_seq = a_seq + 1;
    a_seq_d <= #(tAA) a_seq;
end
always @(rd_en) begin
    oe_seq = oe_seq + 1;
    oe_seq_d <= #(tOE) oe_seq;
end

// output enable: on immediately when rd_en rises, off tOHZ after it falls
reg drive = 0;
always @(rd_en) begin
    if (rd_en) drive <= 1'b1;
    else       drive <= #(tOHZ) 1'b0;
end

wire data_ok = (a_seq == a_seq_d) && (oe_seq == oe_seq_d) && rd_en;
assign d       = drive ? (data_ok ? mem[a] : 8'bxxxxxxxx) : 8'bzzzzzzzz;
assign driving = drive;

// ---- write path -----------------------------------------------------------------
time t_we_fall = 0;
time t_d_change = 0;
time t_a_change = 0;
reg  [16:0] a_at_fall;
reg         we_low = 0;        // a real WE falling edge has been seen (ignores power-up X->1)

always @(d) t_d_change = $time;
always @(a) begin
    t_a_change = $time;
    if (!we_n && !ce_n) begin
        errors = errors + 1;
        $display("%0t %s: ERROR address changed during WE low (tAS/tWR)", $time, NAME);
    end
end

always @(negedge we_n) if (!ce_n && we_n === 1'b0) begin
    t_we_fall = $time;
    we_low    = 1;
    a_at_fall = a;
    if (^a === 1'bx) begin
        errors = errors + 1;
        $display("%0t %s: ERROR address X at WE fall", $time, NAME);
    end
end

always @(posedge we_n) if (!ce_n && we_low) begin
    we_low = 0;
    writes = writes + 1;
    if ($time - t_we_fall < min_twp) min_twp = $time - t_we_fall;
    if (^d !== 1'bx && $time - t_d_change < min_tdw) min_tdw = $time - t_d_change;
    if ($time - t_we_fall < tWP) begin
        errors = errors + 1;
        $display("%0t %s: ERROR tWP %0t < %0f", $time, NAME, $time - t_we_fall, tWP);
    end
    if (^d === 1'bx) begin
        errors = errors + 1;
        $display("%0t %s: ERROR write data X/Z at WE rise (d=%b)", $time, NAME, d);
    end else if ($time - t_d_change < tDW) begin
        errors = errors + 1;
        $display("%0t %s: ERROR tDW %0t < %0f", $time, NAME, $time - t_d_change, tDW);
    end
    if (a !== a_at_fall) begin
        errors = errors + 1;
        $display("%0t %s: ERROR address changed during write", $time, NAME);
    end
    mem[a] = d;
end

always @(posedge rd_en) reads = reads + 1;

endmodule
