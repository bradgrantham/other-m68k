// tb_m68k_bus.v -- behavioural MC68000 bus master (12.5 MHz grade worst-case delays).
//
// Driven from cpuclk.  A bus cycle occupies half-clock states S0..S7; S0 starts on a
// rising edge.  Delays (from the state's clock edge):
//   address valid        55 ns after S0 (rising)
//   AS low, R/W low      55 ns after S2 (rising); read DS low with AS
//   write data driven    50 ns after S3 (falling)
//   write DS low         55 ns after S4 (rising)
//   DTACK/BERR/VPA       sampled at the falling edge ending S4 and every following
//                        falling edge, with 20 ns setup (a value that changed less than
//                        20 ns before the edge is not seen)
//   read data            sampled at the falling edge ending S6 with 10 ns setup
//   AS/DS high           50 ns after S7 (falling)
//   write data released  t_rel ns after the rising edge ending S7 (0..60)
//   R/W high             55 ns after the rising edge ending S7
//
// A cycle can start on any CPU clock: a task waits for the next rising edge unless it
// is called at the instant of one (back-to-back cycles).  idle(n) skips n clocks.
// After each cycle: last_clocks (S0 edge to end-of-S7 edge), last_status
// (ST_DTACK/ST_BERR/ST_VPA/ST_TIMEOUT), last_data (reads), last_s0_fast (0..7, the
// fast-clock phase of S0 if fast_phase is wired).
`timescale 1ns/1ps

module tb_m68k_bus (
    input  wire        clk,            // CPUCLK
    input  wire        reset_n,
    output reg  [23:1] a,
    output reg         as_n,
    output reg         uds_n,
    output reg         lds_n,
    output reg         rw,
    inout  wire [15:0] d,
    input  wire        dtack_n,
    input  wire        berr_n,
    input  wire        vpa_n,
    input  wire [2:0]  fast_phase,     // CPLD1 phase counter, for reporting alignment
    output wire        driving
);

localparam ST_DTACK = 0, ST_BERR = 1, ST_VPA = 2, ST_TIMEOUT = 3;

localparam real T_CLAV  = 55.0;   // clock to address valid
localparam real T_CLSA  = 55.0;   // clock to AS/DS/RW asserted
localparam real T_CLDO  = 50.0;   // clock to write data out
localparam real T_CLSN  = 50.0;   // clock to AS/DS negated
localparam real T_SETUP_DTACK = 20.0;
localparam real T_SETUP_DATA  = 10.0;
localparam integer MAX_WAIT = 40;

reg  [15:0] dout = 16'h0000;
reg         drv  = 1'b0;
assign d       = drv ? dout : 16'bzzzzzzzzzzzzzzzz;
assign driving = drv;

real    t_rel = 0.0;              // write data release delay after S7 ends (0..60)

integer last_clocks = 0;
integer last_status = 0;
reg  [15:0] last_data = 16'h0000;
integer last_s0_fast = 0;
integer cycles = 0;

// setup-time modelling: delayed copies show the value as it was t_setup ago
wire        dtack_s, berr_s, vpa_s;
wire [15:0] d_s;
assign #(T_SETUP_DTACK) dtack_s = dtack_n;
assign #(T_SETUP_DTACK) berr_s  = berr_n;
assign #(T_SETUP_DTACK) vpa_s   = vpa_n;
assign #(T_SETUP_DATA)  d_s     = d;

// clock bookkeeping
integer clkcount = 0;
time    t_last_pos = -1;
always @(posedge clk) begin
    clkcount   = clkcount + 1;
    t_last_pos = $time;
end

initial begin
    a = 23'd0; as_n = 1; uds_n = 1; lds_n = 1; rw = 1;
end

// ---- the bus cycle -----------------------------------------------------------------
task cycle;
    input [23:0] addr;
    input        is_write;
    input        upper;      // UDS
    input        lower;      // LDS
    input [15:0] wdata;
    integer      waits;
    reg          done;
    begin
        // S0 begins at a rising edge; reuse the current instant if we are on one
        if ($time != t_last_pos) @(posedge clk);
        last_s0_fast = fast_phase;
        a  <= #(T_CLAV) addr[23:1];
        @(negedge clk);                                    // S1
        @(posedge clk);                                    // S2
        as_n <= #(T_CLSA) 1'b0;
        if (is_write) begin
            rw <= #(T_CLSA) 1'b0;
        end else begin
            uds_n <= #(T_CLSA) ~upper;
            lds_n <= #(T_CLSA) ~lower;
        end
        @(negedge clk);                                    // S3
        if (is_write) begin
            dout <= #(T_CLDO) wdata;
            drv  <= #(T_CLDO) 1'b1;
        end
        @(posedge clk);                                    // S4
        if (is_write) begin
            uds_n <= #(T_CLSA) ~upper;
            lds_n <= #(T_CLSA) ~lower;
        end
        // wait for a termination at a falling edge (end of S4 / wait states)
        done  = 0;
        waits = 0;
        while (!done) begin
            @(negedge clk);
            if (berr_s === 1'b0)        begin done = 1; last_status = ST_BERR;  end
            else if (vpa_s === 1'b0)    begin done = 1; last_status = ST_VPA;   end
            else if (dtack_s === 1'b0)  begin done = 1; last_status = ST_DTACK; end
            else begin
                waits = waits + 1;
                if (waits > MAX_WAIT) begin done = 1; last_status = ST_TIMEOUT; end
            end
        end
        // S5 begins at this falling edge
        @(posedge clk);                                    // S6
        @(negedge clk);                                    // S7: sample read data, negate strobes
        if (!is_write) last_data = d_s;
        as_n  <= #(T_CLSN) 1'b1;
        uds_n <= #(T_CLSN) 1'b1;
        lds_n <= #(T_CLSN) 1'b1;
        @(posedge clk);                                    // end of S7 (= next S0 edge)
        if (is_write) begin
            drv <= #(t_rel) 1'b0;
            rw  <= #(T_CLAV) 1'b1;
        end
        last_clocks = 4 + waits;              // S0..S7 is 4 clocks plus one per wait
        cycles = cycles + 1;
    end
endtask

task read_word;  input [23:0] addr;                      begin cycle(addr, 0, 1, 1, 16'h0000); end endtask
task write_word; input [23:0] addr; input [15:0] data;   begin cycle(addr, 1, 1, 1, data);     end endtask
task read_byte;  input [23:0] addr;                      begin cycle(addr, 0, ~addr[0], addr[0], 16'h0000);
                                                               last_data = addr[0] ? {8'h00, last_data[7:0]} : {8'h00, last_data[15:8]}; end endtask
task write_byte; input [23:0] addr; input [7:0] data;    begin cycle(addr, 1, ~addr[0], addr[0], {data, data}); end endtask
task idle;       input integer n;                        begin repeat (n) @(posedge clk); end endtask

endmodule
