// tb_system.v -- whole-board simulation: CPLD1 + CPLD2 + 2 SRAM + 2 74AHCT245 + 2 flash
// + behavioural 68000 bus master, with a video/frame monitor, a bus-contention checker
// and thirteen self-checking tests.  Prints "RESULT: PASS" or "RESULT: FAIL".
`timescale 1ns/1ps

// ---------------------------------------------------------------------------------
// SST39SF040-70 flash, one byte lane (U4 = D15..D8, U5 = D7..D0).  /CE is tied low on the
// board, so reads are gated by /OE alone: tACC 70 from address, tOE 35 from OE low, outputs
// float tDF 20 after OE high.  Writes: address latched at the WE falling edge, data at the
// rising edge; the datasheet rules tWP >= 40, tAH 30 (address stable after WE falls),
// tDS 40 (data stable before WE rises), tOEH/tOES (OE high around the pulse) are checked
// and counted in `errors`.  The JEDEC command sequence (addresses compared on A14..A0)
// implements byte program (AND into the array, tBP 20 us), sector erase (4 KB, tSE 25 ms),
// chip erase (tSCE 100 ms), software ID (0xBF / 0xB7) and reset (0xF0).  While busy, reads
// return DQ7 inverted and DQ6 toggling, like the real part.
module tb_flash #(parameter NAME = "flash") (
    input  wire [18:0] a,
    input  wire        oe_n,
    input  wire        we_n,
    inout  wire [7:0]  d,
    output wire        driving
);
    localparam real T_ACC = 70.0, T_OE = 35.0, T_DF = 20.0;
    localparam real T_WP = 40.0, T_AH = 30.0, T_DS = 40.0, T_OEH = 10.0;
    localparam real T_BP = 20_000.0, T_SE = 25_000_000.0, T_SCE = 100_000_000.0;
    reg [7:0] mem [0:524287];
    integer errors = 0, writes = 0, programs = 0, erases = 0, busy_reads = 0;
    integer state = 0;                         // JEDEC sequence state
    reg     id_mode = 0;
    reg     busy = 0;
    reg [7:0] busy_data = 8'hFF;               // what the location will read when done
    reg     toggle = 0;
    time    min_twp = 1_000_000;
    // ---- read path
    integer a_seq = 0, a_seq_d = 0;
    always @(a) begin a_seq = a_seq + 1; a_seq_d <= #(T_ACC) a_seq; end
    reg drive = 0, ok_oe = 0;
    always @(oe_n) if (!oe_n) begin drive <= 1'b1; ok_oe <= #(T_OE) 1'b1; end
                   else       begin drive <= #(T_DF) 1'b0; ok_oe <= 1'b0; end
    wire ok = ok_oe && (a_seq == a_seq_d) && !oe_n;
    reg [7:0] rd;
    reg mem_tick = 0;                          // toggled after every array change (explicit
    always @(a or busy or id_mode or toggle or busy_data or mem_tick) begin   // sensitivity: @* on a 512 KB array is unusably slow)
        if (busy)          rd = {~busy_data[7], toggle, busy_data[5:0]};
        else if (id_mode)  rd = (a[14:0] == 15'h0000) ? 8'hBF : (a[14:0] == 15'h0001) ? 8'hB7 : mem[a];
        else               rd = mem[a];
    end
    assign d = drive ? (ok ? rd : 8'hxx) : 8'bzzzzzzzz;
    assign driving = drive;
    // a completed read of status toggles DQ6 (modelled per OE pulse while busy)
    always @(posedge oe_n) if (busy) begin toggle = ~toggle; busy_reads = busy_reads + 1; end
    // ---- write path
    time t_fall, t_achg = 0, t_dchg = 0;
    reg [18:0] a_lat;
    always @(a) t_achg = $time;
    always @(d) t_dchg = $time;
    task err; input [8*64:1] msg; begin
        errors = errors + 1;
        if (errors < 10) $display("%0t %0s: %0s", $time, NAME, msg);
    end endtask
    reg we_low = 0;                            // a real falling edge was seen (filters the x->1 at reset)
    always @(negedge we_n) begin
        t_fall = $time; a_lat = a; we_low = (we_n === 1'b0);
        if (!oe_n) err("OE low at WE falling edge");
        if (busy) err("WE while busy (ignored by the part)");
    end
    always @(posedge we_n) if (we_low) begin
        we_low = 0;
        writes = writes + 1;
        if ($time - t_fall < T_WP) err("tWP < 40 ns");
        if ($time - t_fall < min_twp) min_twp = $time - t_fall;
        if (t_achg > t_fall && t_achg - t_fall < T_AH) err("address moved within tAH of WE falling");
        if ($time - t_dchg < T_DS || d === 8'bzzzzzzzz || ^d === 1'bx) err("tDS < 40 ns or data not driven at WE rising");
        if (!oe_n) err("OE low at WE rising edge");
        if (!busy) command(a_lat, d);
    end
    task command; input [18:0] ad; input [7:0] dt; begin
        if (dt == 8'hF0 && state != 3) begin state = 0; id_mode = 0; end
        else case (state)
        0: state = (ad[14:0] == 15'h5555 && dt == 8'hAA) ? 1 : 0;
        1: state = (ad[14:0] == 15'h2AAA && dt == 8'h55) ? 2 : 0;
        2: begin
            if (ad[14:0] != 15'h5555) state = 0;
            else if (dt == 8'hA0) state = 3;
            else if (dt == 8'h80) state = 4;
            else if (dt == 8'h90) begin id_mode = 1; state = 0; end
            else state = 0;
        end
        3: begin                                   // byte program
            programs = programs + 1;
            mem[ad] = mem[ad] & dt; busy_data = mem[ad]; state = 0; mem_tick = ~mem_tick;
            start_busy(T_BP);
        end
        4: state = (ad[14:0] == 15'h5555 && dt == 8'hAA) ? 5 : 0;
        5: state = (ad[14:0] == 15'h2AAA && dt == 8'h55) ? 6 : 0;
        6: begin
            state = 0;
            if (dt == 8'h30) begin erase_sector(ad[18:12]); start_busy(T_SE); end
            else if (dt == 8'h10 && ad[14:0] == 15'h5555) begin erase_chip; start_busy(T_SCE); end
        end
        default: state = 0;
        endcase
    end endtask
    task erase_sector; input [6:0] sec; integer i; begin
        erases = erases + 1;
        for (i = 0; i < 4096; i = i + 1) mem[{sec, i[11:0]}] = 8'hFF;
        busy_data = 8'hFF; mem_tick = ~mem_tick;
    end endtask
    task erase_chip; integer i; begin
        erases = erases + 1;
        for (i = 0; i < 524288; i = i + 1) mem[i] = 8'hFF;
        busy_data = 8'hFF; mem_tick = ~mem_tick;
    end endtask
    event ev_busy; real t_busy;
    always @(ev_busy) begin #(t_busy); busy = 0; end
    task start_busy; input real t; begin
        busy = 1; toggle = 0; t_busy = t; -> ev_busy;
    end endtask
endmodule

// ---------------------------------------------------------------------------------
// 74AHCT245: dir=1 drives A->B, dir=0 drives B->A; 10 ns propagation and enable/disable.
module tb_ahct245 (
    inout  wire [7:0] a,
    inout  wire [7:0] b,
    input  wire       dir,
    input  wire       oe_n,
    output wire       drv_a,          // this chip is driving its A side
    output wire       drv_b
);
    wire en_ab = ~oe_n &  dir;
    wire en_ba = ~oe_n & ~dir;
    assign #10 b = en_ab ? a : 8'bzzzzzzzz;
    assign #10 a = en_ba ? b : 8'bzzzzzzzz;
    assign #10 drv_b = en_ab;
    assign #10 drv_a = en_ba;
endmodule

// ---------------------------------------------------------------------------------
// 68681 bus-interface stub: open-drain DTACK about 2.5 X1 periods (3.6864 MHz) after CS,
// released after CS; reads return 8'hA0 | RS; writes are recorded.  IRQ is driven by the tb.
module tb_duart (
    input  wire       cs_n,
    input  wire       rw,
    input  wire [4:1] rs,
    inout  wire [7:0] d,
    output wire       dtack_n,         // open drain
    output wire       driving
);
    localparam real T_X1 = 271.27;
    reg ack = 1'b0;
    reg [7:0] last_wr = 8'h00; reg [4:1] last_wr_rs = 4'h0; integer writes = 0, reads = 0;
    assign dtack_n = ack ? 1'b0 : 1'bz;
    wire   rd = ~cs_n & rw;
    assign d = rd ? (8'hA0 | {4'b0000, rs}) : 8'bzzzzzzzz;
    assign driving = rd;
    always @(negedge cs_n) begin
        #(2.5 * T_X1);
        if (!cs_n) begin
            ack = 1'b1;
            if (rw) reads = reads + 1;
            else begin writes = writes + 1; last_wr = d; last_wr_rs = rs; end
        end
    end
    always @(posedge cs_n) begin #50; ack = 1'b0; end
endmodule

// ---------------------------------------------------------------------------------
module tb_system;

`ifdef BOARD_PARAMS_VH
`undef BOARD_PARAMS_VH
`endif
`include "board_params.vh"

localparam real T_CLK24 = 41.6667;
localparam real T_PIN   = 7.5;             // CPLD output delay

// ---- clocks and reset -----------------------------------------------------------
reg clk24 = 1'b0;
always #(T_CLK24/2.0) clk24 = ~clk24;
reg sysreset_n = 1'b0;

// ---- CPLD1 ---------------------------------------------------------------------
wire        cpuclk, berr_n, vpa_n, ramoe_n, weu_n, wel_n, romoe_n, romwe_n, iocs_n, bufen_n, duartcs_n;
tri1        dtack_n;                     // open-drain bus: CPLD1 and the DUART, 4.7k pull-up
wire        latch, de, hsync, vsync;
wire [16:0] ma;
wire [23:1] a;
wire        as_n, uds_n, lds_n, rw;

cpld1_mmu u1 (
    .clk24(clk24), .sysreset_n(sysreset_n),
    .a(a), .as_n(as_n), .uds_n(uds_n), .lds_n(lds_n), .rw(rw),
    .cpuclk(cpuclk), .dtack_n(dtack_n), .berr_n(berr_n), .vpa_n(vpa_n),
    .ma(ma), .ramoe_n(ramoe_n), .weu_n(weu_n), .wel_n(wel_n),
    .romoe_n(romoe_n), .romwe_n(romwe_n), .iocs_n(iocs_n), .bufen_n(bufen_n), .duartcs_n(duartcs_n),
    .latch(latch), .de(de), .hsync(hsync), .vsync(vsync)
);

// CPLD outputs reach the board T_PIN after the internal clock edge
wire        #(T_PIN) cpuclk_p = cpuclk;
wire        #(T_PIN) dtack_p  = dtack_n;
wire        #(T_PIN) berr_p   = berr_n;
wire        #(T_PIN) vpa_p    = vpa_n;
wire        #(T_PIN) ramoe_p  = ramoe_n;
wire        #(T_PIN) weu_p    = weu_n;
wire        #(T_PIN) wel_p    = wel_n;
wire        #(T_PIN) romoe_p  = romoe_n;
wire        #(T_PIN) romwe_p  = romwe_n;
wire        #(T_PIN) iocs_p   = iocs_n;
wire        #(T_PIN) bufen_p  = bufen_n;
wire        #(T_PIN) duartcs_p = duartcs_n;
wire        #(T_PIN) latch_p  = latch;
wire        #(T_PIN) de_p     = de;
wire        #(T_PIN) hsync_p  = hsync;
wire        #(T_PIN) vsync_p  = vsync;
wire [16:0] #(T_PIN) ma_p     = ma;

// ---- buses ---------------------------------------------------------------------
wire [15:0] cpu_d;          // CPU bus: 68000, flash, DUART, A side of the transceivers
wire [15:0] sram_d;         // SRAM-side bus: SRAMs, CPLD2, B side

// ---- 68000 ---------------------------------------------------------------------
wire cpu_drv;
tb_m68k_bus cpu (
    .clk(cpuclk_p), .reset_n(sysreset_n),
    .a(a), .as_n(as_n), .uds_n(uds_n), .lds_n(lds_n), .rw(rw), .d(cpu_d),
    .dtack_n(dtack_p), .berr_n(berr_p), .vpa_n(vpa_p),
    .fast_phase(u1.ph), .driving(cpu_drv)
);

// ---- DUART (bus interface only) ----------------------------------------------------
wire duart_drv;
reg  duart_irq_n = 1'b1;
tb_duart duart (.cs_n(duartcs_p), .rw(rw), .rs(a[4:1]), .d(cpu_d[7:0]), .dtack_n(dtack_n), .driving(duart_drv));

// ---- flash (CE tied low; OE and WE from CPLD1) -----------------------------------
wire rom_drv, rom_drv_lo;
tb_flash #(.NAME("flash-hi")) flash_hi (.a(a[19:1]), .oe_n(romoe_p), .we_n(romwe_p), .d(cpu_d[15:8]), .driving(rom_drv));
tb_flash #(.NAME("flash-lo")) flash_lo (.a(a[19:1]), .oe_n(romoe_p), .we_n(romwe_p), .d(cpu_d[7:0]),  .driving(rom_drv_lo));

// ---- transceivers ----------------------------------------------------------------
wire xh_da, xh_db, xl_da, xl_db;
tb_ahct245 xh (.a(cpu_d[15:8]), .b(sram_d[15:8]), .dir(~rw), .oe_n(bufen_p), .drv_a(xh_da), .drv_b(xh_db));
tb_ahct245 xl (.a(cpu_d[7:0]),  .b(sram_d[7:0]),  .dir(~rw), .oe_n(bufen_p), .drv_a(xl_da), .drv_b(xl_db));

// ---- SRAMs -----------------------------------------------------------------------
wire sh_drv, sl_drv;
tb_sram #(.NAME("SRAM-hi")) sram_hi (.a(ma_p), .d(sram_d[15:8]), .ce_n(1'b0), .oe_n(ramoe_p), .we_n(weu_p), .driving(sh_drv));
tb_sram #(.NAME("SRAM-lo")) sram_lo (.a(ma_p), .d(sram_d[7:0]),  .ce_n(1'b0), .oe_n(ramoe_p), .we_n(wel_p), .driving(sl_drv));

// ---- CPLD2 -----------------------------------------------------------------------
wire [2:0] ipl_n;
wire [7:0] rgb;
wire       sd_cs, sd_clk, sd_mosi, audio;
wire [1:0] led;
reg        sd_miso = 1'b0, paljmp = 1'b0, nmibtn = 1'b0;
tri1       ps2_clk, ps2_dat;
tri1 [3:0] uio;
reg        ps2_clk_drv = 1'b1, ps2_dat_drv = 1'b1;   // tb side of the open-drain lines
assign ps2_clk = ps2_clk_drv ? 1'bz : 1'b0;
assign ps2_dat = ps2_dat_drv ? 1'bz : 1'b0;

cpld2_io u2 (
    .clk24(clk24), .sysreset_n(sysreset_n),
    .a(a[3:1]), .iocs_n(iocs_p), .rw(rw),
    .latch(latch_p), .de(de_p), .hsync(hsync_p), .vsync(vsync_p),
    .d(sram_d), .ipl_n(ipl_n), .rgb(rgb),
    .sd_cs(sd_cs), .sd_clk(sd_clk), .sd_mosi(sd_mosi), .sd_miso(sd_miso),
    .ps2_clk(ps2_clk), .ps2_dat(ps2_dat),
    .duart_irq_n(duart_irq_n),
    .audio(audio), .led(led), .paljmp(paljmp), .nmibtn(nmibtn), .uio(uio)
);

// ---- bus contention checker ------------------------------------------------------
// Driver counts on both buses; an overlap must persist >= 1 ns to count (filters the
// zero-width ordering artefacts of same-timestep events).
wire [2:0] cpu_bus_drivers  = cpu_drv + rom_drv + xh_da + duart_drv;
wire [2:0] sram_bus_drivers = sh_drv + xh_db + u2.io_rd;
wire [2:0] #1 cpu_bus_drivers_f  = cpu_bus_drivers;
wire [2:0] #1 sram_bus_drivers_f = sram_bus_drivers;
integer contention = 0;
integer ev_cpu = 0, ev_rom = 0, ev_xa = 0, ev_xb = 0, ev_sram = 0, ev_cpld2 = 0;   // drive events (non-vacuity)
always @(posedge cpu_drv)  ev_cpu   = ev_cpu + 1;
always @(posedge rom_drv)  ev_rom   = ev_rom + 1;
always @(posedge xh_da)    ev_xa    = ev_xa + 1;
always @(posedge xh_db)    ev_xb    = ev_xb + 1;
always @(posedge sh_drv)   ev_sram  = ev_sram + 1;
always @(posedge u2.io_rd) ev_cpld2 = ev_cpld2 + 1;
always @(cpu_bus_drivers_f) if (cpu_bus_drivers_f > 1) begin
    contention = contention + 1;
    if (contention < 10) $display("%0t CONTENTION on CPU bus: cpu=%b rom=%b xcvr=%b", $time, cpu_drv, rom_drv, xh_da);
end
always @(sram_bus_drivers_f) if (sram_bus_drivers_f > 1) begin
    contention = contention + 1;
    if (contention < 10) $display("%0t CONTENTION on SRAM bus: sram=%b xcvr=%b cpld2=%b", $time, sh_drv, xh_db, u2.io_rd);
end
// the two byte halves must agree
always @(posedge clk24) if (sysreset_n && (xh_da !== xl_da || xh_db !== xl_db || sh_drv !== sl_drv || rom_drv !== rom_drv_lo)) begin
    contention = contention + 1;
    $display("%0t byte halves disagree", $time);
end

// ---- phase lock check: CPLD2's counter must follow CPLD1's ---------------------------
integer phase_err = 0;
always @(negedge clk24) if (sysreset_n && $time > 2000 && u1.ph !== u2.ph2) begin
    phase_err = phase_err + 1;
    if (phase_err < 5) $display("%0t PHASE: cpld1 ph=%0d cpld2 ph2=%0d", $time, u1.ph, u2.ph2);
end

// ---- video / frame monitor -------------------------------------------------------
// Uses CPLD1's counters hierarchically.  Per line: slots and fetches; per frame: lines,
// active lines, vsync lines.  Every fetch address is checked against page base +
// v*160 + h, and (when pix_check is set) every pixel against the SRAM models.
integer mon_slots = 0, mon_fetches = 0, mon_lines = 0, mon_active = 0, mon_vsync_lines = 0;
integer mon_fetch_err = 0, mon_slot_err = 0, mon_addr_err = 0, mon_pix_err = 0;
integer fr_lines, fr_active, fr_vsync_lines, fr_fetch_err, fr_slot_err, fr_addr_err, fr_pix_err;
integer frames = 0;
reg        mon_line_valid = 0;     // the current line started after a frame start
reg        pix_check = 0;
reg        fetched_now = 0;
reg [16:0] fetch_addr = 0;
reg [16:0] mon_base = FB_PAGE0_WORD;
reg [16:0] first_fetch_ma = 17'h1FFFF;
integer    exp_h_total;
reg [7:0]  exp_pix;
event      frame_start;
event      line_start;

always @(posedge clk24) if (sysreset_n) begin
    case (u1.ph)
    3'd7: begin                                            // this edge is E0
        mon_slots = mon_slots + 1;
        if (u1.h == u1.h_sync_start_m1) begin              // HSYNC begins: a line ends
            exp_h_total = u1.pal ? PAL_H_TOTAL : NTSC_H_TOTAL;
            if (mon_line_valid) begin
                if (mon_slots != exp_h_total) begin
                    mon_slot_err = mon_slot_err + 1;
                    if (mon_slot_err < 5) $display("%0t MON: line %0d had %0d slots (expected %0d)", $time, u1.v, mon_slots, exp_h_total);
                end
                if (mon_fetches == NTSC_H_ACTIVE) mon_active = mon_active + 1;
                else if (mon_fetches != 0) begin
                    mon_fetch_err = mon_fetch_err + 1;
                    if (mon_fetch_err < 5) $display("%0t MON: line %0d had %0d fetches", $time, u1.v, mon_fetches);
                end
                if (u1.vsync_i) mon_vsync_lines = mon_vsync_lines + 1;
                mon_lines = mon_lines + 1;
            end
            mon_slots = 0; mon_fetches = 0; mon_line_valid = 1;
            -> line_start;
            if (u1.v == u1.v_total_m1) begin               // frame start (v wraps to 0)
                fr_lines = mon_lines; fr_active = mon_active; fr_vsync_lines = mon_vsync_lines;
                fr_fetch_err = mon_fetch_err; fr_slot_err = mon_slot_err;
                fr_addr_err = mon_addr_err; fr_pix_err = mon_pix_err;
                mon_lines = 0; mon_active = 0; mon_vsync_lines = 0;
                mon_fetch_err = 0; mon_slot_err = 0; mon_addr_err = 0; mon_pix_err = 0;
                frames = frames + 1;
                mon_base = u1.page ? FB_PAGE1_WORD : FB_PAGE0_WORD;
                -> frame_start;
            end
        end
    end
    3'd1: begin                                            // this edge is E2: MA holds VA
        fetched_now = u1.de;
        if (u1.de) begin
            mon_fetches = mon_fetches + 1;
            fetch_addr = ma_p;
            if (u1.v == 0 && u1.h == 0) first_fetch_ma = ma_p;
            if (ma_p !== mon_base + u1.v * NTSC_H_ACTIVE + u1.h) begin
                mon_addr_err = mon_addr_err + 1;
                if (mon_addr_err < 5) $display("%0t MON: fetch address %h, expected %h (v=%0d h=%0d)", $time, ma_p, mon_base + u1.v*NTSC_H_ACTIVE + u1.h, u1.v, u1.h);
            end
        end
    end
    3'd4: begin                                            // E5: pix = upper byte of this slot's word
        if (pix_check) begin
            exp_pix = fetched_now ? sram_hi.mem[fetch_addr] : 8'h00;
            if (rgb !== exp_pix) begin
                mon_pix_err = mon_pix_err + 1;
                if (mon_pix_err < 5) $display("%0t MON: pixel (hi) %h expected %h (v=%0d h=%0d)", $time, rgb, exp_pix, u1.v, u1.h);
            end
        end
    end
    3'd0: begin                                            // E1: pix = lower byte of the previous slot's word
        if (pix_check) begin                               // (fetch regs are still the previous slot's here)
            exp_pix = fetched_now ? sram_lo.mem[fetch_addr] : 8'h00;
            if (rgb !== exp_pix) begin
                mon_pix_err = mon_pix_err + 1;
                if (mon_pix_err < 5) $display("%0t MON: pixel (lo) %h expected %h (v=%0d h=%0d)", $time, rgb, exp_pix, u1.v, u1.h);
            end
        end
    end
    default: ;
    endcase
end

// ---- test bookkeeping --------------------------------------------------------------
integer errors = 0, test_errors = 0, tests_run = 0, tests_failed = 0;
reg [8*96:1] test_name;

task begin_test; input [8*96:1] name; begin
    test_name = name; test_errors = 0; tests_run = tests_run + 1;
    $display("---- test %0d: %0s", tests_run, name);
end endtask

task end_test; begin
    if (test_errors == 0) $display("PASS: %0s", test_name);
    else begin $display("FAIL: %0s (%0d errors)", test_name, test_errors); tests_failed = tests_failed + 1; end
end endtask

task check; input cond; input [8*96:1] msg; begin
    if (!cond) begin
        errors = errors + 1; test_errors = test_errors + 1;
        $display("%0t   ERROR: %0s", $time, msg);
    end
end endtask

task check_eq; input [31:0] got; input [31:0] exp; input [8*64:1] msg; begin
    if (got !== exp) begin
        errors = errors + 1; test_errors = test_errors + 1;
        $display("%0t   ERROR: %0s: got %h expected %h", $time, msg, got, exp);
    end
end endtask

// Expected length of a slot-aligned (RAM/IO) cycle as a function of the fast phase of
// S0.  Derived from the slot schedule: AS is low 62 ns after S2; it is sampled at E4;
// DTACK is seen at the following falling edge; the cycle ends at E0.
function integer exp_ram_clocks; input integer phase; begin
    case (phase)
    0: exp_ram_clocks = 4;      // serviced this slot, zero wait
    2: exp_ram_clocks = 7;      // AS low after E4 -> next slot (architecture.md says 6)
    4: exp_ram_clocks = 6;      // next slot
    6: exp_ram_clocks = 5;      // serviced this slot, DTACK seen at E5
    default: exp_ram_clocks = -1;
    endcase
end endfunction

// 68681 register offsets (byte registers at odd addresses; board_params.vh only carries CPLD blocks)
localparam [4:0] DUART_SRA_OFFSET = 5'd3, DUART_TBA_OFFSET = 5'd7, DUART_RBA_OFFSET = 5'd7;

// flash command constants, as spec/board.json "flash" / generated/hw.h define them
// (board_params.vh does not carry the flash section): chip byte address N = ROM_BASE + 2N

// flash helpers: word i of the ROM window across the two byte lanes
function [15:0] rom_word; input [18:0] i; begin rom_word = {flash_hi.mem[i], flash_lo.mem[i]}; end endfunction
task set_rom; input [18:0] i; input [15:0] v; begin flash_hi.mem[i] = v[15:8]; flash_lo.mem[i] = v[7:0]; end endtask
reg oe_low_seen = 0;                       // ROMOE asserted since last cleared (for the write test)
always @(negedge romoe_n) oe_low_seen = 1;

// address helpers
function [23:0] io_addr;  input [3:0] off; begin io_addr = IO_BASE + {off, 1'b0}; end endfunction
function [23:0] sw_addr;  input [3:0] off; begin sw_addr = SWITCHES_BASE + {off, 1'b0}; end endfunction

// ---- peripheral stimulus tasks -------------------------------------------------------
task ps2_send_frame; input [10:0] frame; integer i; begin   // bit 0 (start) first
    for (i = 0; i < 11; i = i + 1) begin
        ps2_dat_drv = frame[i];
        #10000; ps2_clk_drv = 0;
        #40000; ps2_clk_drv = 1;
        #30000;
    end
    ps2_dat_drv = 1;
end endtask

task ps2_send; input [7:0] b; input good_parity; reg p; begin
    p = ~(^b);                              // odd parity
    if (!good_parity) p = ~p;
    ps2_send_frame({1'b1, p, b, 1'b0});
end endtask

task ps2_send_partial; input integer nbits; integer i; begin   // start + some data bits, then silence
    for (i = 0; i < nbits; i = i + 1) begin
        ps2_dat_drv = (i == 0) ? 0 : 1;
        #10000; ps2_clk_drv = 0;
        #40000; ps2_clk_drv = 1;
        #30000;
    end
    ps2_dat_drv = 1;
end endtask


// ---- the tests -------------------------------------------------------------------
integer i, k, n;
reg [15:0] w;
reg [23:0] addr;
reg [15:0] exp;
integer t0;

initial begin
    // memory contents
    for (i = 0; i < 524288; i = i + 1) begin w = (i * 16'h1F3) ^ 16'hA5C3; flash_hi.mem[i] = w[15:8]; flash_lo.mem[i] = w[7:0]; end
    set_rom(0, 16'h0000); set_rom(1, 16'h4000);            // SSP 0x00004000
    set_rom(2, 16'h00E0); set_rom(3, 16'h0100);            // PC  0xE00100
    for (i = 0; i < 131072; i = i + 1) begin
        sram_hi.mem[i] = (i * 7 + 3) & 8'hFF;
        sram_lo.mem[i] = (i * 13 + 5) & 8'hFF;
    end
    sram_hi.mem[0] = 8'h12; sram_lo.mem[0] = 8'h34;

    if ($test$plusargs("vcd")) begin                       // first 2 ms unless +vcd_all (huge)
        $dumpfile("build/tb_system.vcd");
        $dumpvars(0, tb_system);
    end

    #500 sysreset_n = 1;
    #200;

    // ------------------------------------------------------------------ 1
    begin_test("reset vector fetch via overlay, then overlay clear");
    check(u1.overlay === 1'b1, "overlay set after reset");
    cpu.read_word(24'h000000); check_eq(cpu.last_data, 16'h0000, "vector word 0 (SSP hi)");
    check(cpu.last_status == 0, "DTACK on overlay read");
    cpu.read_word(24'h000002); check_eq(cpu.last_data, 16'h4000, "vector word 1 (SSP lo)");
    cpu.read_word(24'h000004); check_eq(cpu.last_data, 16'h00E0, "vector word 2 (PC hi)");
    cpu.read_word(24'h000006); check_eq(cpu.last_data, 16'h0100, "vector word 3 (PC lo)");
    check_eq(cpu.last_clocks, 4, "overlay ROM read is 4 clocks");
    check(u1.overlay === 1'b1, "overlay still set after RAM-region reads");
    cpu.write_word(24'h000000, 16'hBEEF);                  // write under overlay: DTACK, ignored
    #30;
    check(cpu.last_status == 0, "DTACK on write under overlay");
    check_eq(cpu.last_clocks, 4, "write under overlay is 4 clocks");
    check(sram_hi.mem[0] == 8'h12 && sram_lo.mem[0] == 8'h34, "write under overlay ignored");
    check(flash_hi.writes == 0 && flash_lo.writes == 0, "write under overlay does not pulse ROMWE");
    cpu.read_word(24'hE00100);                             // first real ROM access clears the overlay
    check_eq(cpu.last_data, rom_word(19'h0080), "ROM word at E00100");
    #100;
    check(u1.overlay === 1'b0, "overlay cleared after ROM-region access");
    cpu.read_word(24'h000000); check_eq(cpu.last_data, 16'h1234, "RAM visible at 0 after overlay clear");
    end_test;

    // ------------------------------------------------------------------ 2
    begin_test("RAM word/byte read/write at all four S0 alignments");
    for (n = 0; n < 2; n = n + 1) begin
        cpu.t_rel = (n == 0) ? 0.0 : 60.0;                 // write-data release: fastest and slowest
        cpu.read_word(24'h001000);                         // realign: serviced cycles end at E0
        for (k = 0; k < 4; k = k + 1) begin
            addr = 24'h002000 + k * 16 + n * 256;
            w    = 16'hC0DE + k * 16'h1111 + n;
            cpu.idle(k);
            cpu.write_word(addr, w);
            #30;                                           // let WE rise and the SRAM model write
            check(cpu.last_status == 0, "DTACK on RAM write");
            check_eq(cpu.last_s0_fast, 2*k, "write S0 fast phase");
            check_eq(cpu.last_clocks, exp_ram_clocks(2*k), "write cycle clocks");
            cpu.read_word(24'h001000);                     // realign to E0 again
            check(sram_hi.mem[addr[17:1]] == w[15:8] && sram_lo.mem[addr[17:1]] == w[7:0], "word written to SRAM");
            cpu.idle(k);
            cpu.read_word(addr);
            check_eq(cpu.last_s0_fast, 2*k, "read S0 fast phase");
            check_eq(cpu.last_clocks, exp_ram_clocks(2*k), "read cycle clocks");
            check_eq(cpu.last_data, w, "word read back");
            cpu.idle(k);
            cpu.write_byte(addr + 3, 8'h5A ^ k);           // odd address: lower byte of word 1
            #30;
            check_eq(cpu.last_clocks, exp_ram_clocks(2*k), "byte write clocks");
            cpu.read_word(24'h001000);
            check(sram_lo.mem[addr[17:1] + 1] == (8'h5A ^ k), "odd byte written");
            check(sram_hi.mem[addr[17:1] + 1] == (((addr[17:1]+1) * 7 + 3) & 8'hFF), "even byte untouched");
            cpu.idle(k);
            cpu.write_byte(addr + 2, 8'hA5 ^ k);           // even address: upper byte
            #30;
            cpu.read_word(24'h001000);
            check(sram_hi.mem[addr[17:1] + 1] == (8'hA5 ^ k), "even byte written");
            cpu.idle(k);
            cpu.read_byte(addr + 2); check_eq(cpu.last_data, 8'hA5 ^ k, "even byte read");
            cpu.idle(k);
            cpu.read_byte(addr + 3); check_eq(cpu.last_data, 8'h5A ^ k, "odd byte read");
            check_eq(cpu.last_clocks, exp_ram_clocks(2*k), "byte read clocks");
        end
    end
    cpu.t_rel = 0.0;
    // back-to-back writes and reads (as MOVEM would do)
    for (k = 0; k < 8; k = k + 1) cpu.write_word(24'h003000 + 2*k, 16'h1000 + k);
    for (k = 0; k < 8; k = k + 1) begin
        cpu.read_word(24'h003000 + 2*k);
        check_eq(cpu.last_data, 16'h1000 + k, "back-to-back word");
        check_eq(cpu.last_clocks, 4, "back-to-back aligned cycle is 4 clocks");
    end
    end_test;

    // ------------------------------------------------------------------ 3
    begin_test("ROM reads take 4 clocks at any alignment");
    for (k = 0; k < 4; k = k + 1) begin
        cpu.read_word(24'h001000);                         // realign to E0
        cpu.idle(k);
        cpu.read_word(24'hE00000 + 2 * (16'h100 + k));
        check_eq(cpu.last_s0_fast, 2*k, "ROM S0 fast phase");
        check_eq(cpu.last_clocks, 4, "ROM cycle clocks");
        check_eq(cpu.last_data, rom_word(19'h100 + k), "ROM data");
        cpu.read_byte(24'hE00000 + 2 * (16'h200 + k) + 1);
        check_eq(cpu.last_data, rom_word(19'h200 + k) & 16'h00FF, "ROM odd byte");
        check_eq(cpu.last_clocks, 4, "ROM byte cycle clocks");
    end
    // a stray write to ROM: DTACK in 4 clocks, one WE pulse per lane, OE high throughout,
    // and the array untouched (no command sequence)
    k = flash_hi.writes; n = flash_lo.writes;
    w = rom_word(19'h100);
    oe_low_seen = 0;
    cpu.write_word(24'hE00200, 16'h0000);
    #60;
    check(cpu.last_status == 0, "DTACK on ROM write");
    check_eq(cpu.last_clocks, 4, "ROM write is 4 clocks");
    check(flash_hi.writes == k + 1 && flash_lo.writes == n + 1, "one ROMWE pulse per lane");
    check(oe_low_seen == 0, "ROMOE stayed high during the write cycle");
    check(flash_hi.min_twp >= 40 && flash_lo.min_twp >= 40, "ROMWE pulse >= 40 ns");
    check_eq(rom_word(19'h100), w, "stray write did not change the array");
    cpu.read_word(24'hE00200); check_eq(cpu.last_data, rom_word(19'h100), "ROM reads back after stray write");
    end_test;

    // ------------------------------------------------------------------ 3b
    begin_test("flash: software ID, program a word, sector erase, program again");
    // software ID
    cpu.write_word(FLASH_CMD1, 16'hAAAA); cpu.write_word(FLASH_CMD2, 16'h5555); cpu.write_word(FLASH_CMD1, 16'h9090);
    cpu.read_word(ROM_BASE);     check_eq(cpu.last_data, {FLASH_MFR_ID, FLASH_MFR_ID}, "manufacturer ID on both lanes");
    cpu.read_word(ROM_BASE + 2); check_eq(cpu.last_data, {FLASH_DEV_ID, FLASH_DEV_ID}, "device ID on both lanes");
    cpu.write_word(ROM_BASE, 16'hF0F0);
    cpu.read_word(ROM_BASE);     check_eq(cpu.last_data, rom_word(0), "array visible again after reset command");
    // program (clears bits only): word 0x1200 of the window, in chip sector 1
    addr = ROM_BASE + 24'h002400;
    exp  = rom_word(addr[19:1]) & 16'h0F0F;
    k = flash_hi.programs;
    cpu.write_word(FLASH_CMD1, 16'hAAAA); cpu.write_word(FLASH_CMD2, 16'h5555); cpu.write_word(FLASH_CMD1, 16'hA0A0);
    cpu.write_word(addr, exp);
    n = 0;
    cpu.read_word(addr);
    while (cpu.last_data !== exp && n < 200) begin n = n + 1; cpu.read_word(addr); end
    check(n > 0, "busy status seen while programming");
    check(n < 200, "programming completed");
    check_eq(cpu.last_data, exp, "programmed word reads back");
    check(flash_hi.programs == k + 1, "one program operation");
    w = ((addr[19:1] + 1) * 16'h1F3) ^ 16'hA5C3;
    check_eq(rom_word(addr[19:1] + 1), w, "neighbour untouched");
    // sector erase: chip sector 1 = CPU 0xE02000..0xE03FFF
    cpu.write_word(FLASH_CMD1, 16'hAAAA); cpu.write_word(FLASH_CMD2, 16'h5555); cpu.write_word(FLASH_CMD1, 16'h8080);
    cpu.write_word(FLASH_CMD1, 16'hAAAA); cpu.write_word(FLASH_CMD2, 16'h5555); cpu.write_word(addr, 16'h3030);
    #100;                                                  // WE rises after DS negates, after the task returns
    check(flash_hi.busy && flash_lo.busy, "both lanes busy after the erase command");
    n = 0;
    cpu.read_word(addr);
    while (cpu.last_data !== 16'hFFFF && n < 200000) begin n = n + 1; cpu.read_word(addr); end
    check(n < 200000, "erase completed");
    check(rom_word(19'h1000) == 16'hFFFF && rom_word(19'h1FFF) == 16'hFFFF, "whole sector erased");
    check(rom_word(19'h0FFF) != 16'hFFFF && rom_word(19'h2000) != 16'hFFFF, "neighbouring sectors untouched");
    check(rom_word(0) == 16'h0000 && rom_word(1) == 16'h4000, "sector 0 (vectors) untouched");
    // program the full value back
    cpu.write_word(FLASH_CMD1, 16'hAAAA); cpu.write_word(FLASH_CMD2, 16'h5555); cpu.write_word(FLASH_CMD1, 16'hA0A0);
    cpu.write_word(addr, 16'h1234);
    n = 0; cpu.read_word(addr);
    while (cpu.last_data !== 16'h1234 && n < 200) begin n = n + 1; cpu.read_word(addr); end
    check_eq(cpu.last_data, 16'h1234, "word programmed after erase");
    check(flash_hi.errors == 0 && flash_lo.errors == 0, "no flash write-timing violations");
    end_test;

    // ------------------------------------------------------------------ 4
    begin_test("BERR on unmapped, VPA on IACK");
    cpu.read_word(24'h800000);  check(cpu.last_status == 1, "BERR on read of 0x800000");
    cpu.write_word(24'h040000, 16'h0000); check(cpu.last_status == 1, "BERR on write just above RAM");
    cpu.read_word(24'hDFFFFE);  check(cpu.last_status == 1, "BERR just below ROM");
    cpu.read_word(24'hEFFFFE);  check(cpu.last_status == 0, "top of the 1 MB ROM window is mapped");
    cpu.read_word(24'hF30000);  check(cpu.last_status == 1, "BERR just above DUART");
    cpu.read_word(24'hFFFFF3);  check(cpu.last_status == 2, "VPA on IACK level 1");
    cpu.read_word(24'hFFFFFF);  check(cpu.last_status == 2, "VPA on IACK level 7");
    cpu.read_word(24'h03FFFE);  check(cpu.last_status == 0, "top of RAM is mapped");
    cpu.read_word(24'hF1FFFE);  check(cpu.last_status == 0, "SWITCHES read gets DTACK");
    end_test;

    // ------------------------------------------------------------------ 5
    begin_test("IO register write/read, ID, pins, NMI");
    cpu.read_word(io_addr(IO_ID_OFF));      check_eq(cpu.last_data, ID_VALUE, "ID register");
    cpu.read_word(io_addr(IO_INTEN_OFF));   check_eq(cpu.last_data, 16'h0000, "INTEN is write-only (reads 0)");
    cpu.read_word(io_addr(IO_SDIO_OFF));    check_eq(cpu.last_data, 16'h0000, "SDIO is write-only (reads 0)");
    cpu.read_word(io_addr(IO_GPIO_OFF));    check_eq(cpu.last_data, 16'h0000, "GPIO is write-only (reads 0)");
    cpu.read_word(io_addr(IO_USERIO_OFF));  check_eq(cpu.last_data, 16'h0F00, "USERIO reset value (pins read high, OUT reads 0)");
    check(sd_cs === 1 && sd_clk === 0 && sd_mosi === 0, "SDIO reset value on the pins");
    check(ps2_clk === 1'b1 && ps2_dat === 1'b1 && led === 2'b00 && audio === 1'b0, "GPIO reset value on the pins");
    cpu.write_word(io_addr(IO_INTEN_OFF), 16'hFFFF);
    cpu.read_word(io_addr(IO_INTEN_OFF));   check_eq(cpu.last_data, 16'h0000, "INTEN still reads 0 after write");
    check_eq(u2.inten, 4'hF, "INTEN written (4 bits)");
    cpu.write_word(io_addr(IO_INTEN_OFF), 16'h0000);
    cpu.write_word(io_addr(IO_GPIO_OFF), 16'h001F);
    check(led === 2'b11 && audio === 1'b1, "LEDs and audio follow GPIO");
    cpu.read_word(io_addr(IO_GPIO_OFF));    check_eq(cpu.last_data, 16'h0000, "GPIO reads 0");
    cpu.write_word(io_addr(IO_GPIO_OFF), 16'h0000);        // pull PS/2 lines low, LEDs off
    check(ps2_clk === 1'b0 && ps2_dat === 1'b0 && led === 2'b00, "PS/2 open-drain pulled low");
    cpu.read_word(io_addr(IO_INTSTAT_OFF));
    check(cpu.last_data[INTSTAT_PS2CLK_BIT] == 0 && cpu.last_data[INTSTAT_PS2DAT_BIT] == 0, "INTSTAT shows PS/2 pins low");
    cpu.write_word(io_addr(IO_GPIO_OFF), 16'h0003);
    check(ps2_clk === 1'b1 && ps2_dat === 1'b1, "PS/2 lines released");
    sd_miso = 1;
    cpu.write_word(io_addr(IO_SDIO_OFF), 16'h0004);
    check(sd_cs === 0 && sd_clk === 0 && sd_mosi === 1, "SD pins follow SDIO");
    cpu.read_word(io_addr(IO_SDIO_OFF));    check_eq(cpu.last_data, 16'h0000, "SDIO reads 0 (MISO via INTSTAT)");
    cpu.write_word(io_addr(IO_SDIO_OFF), 16'h0003);
    cpu.write_word(io_addr(IO_USERIO_OFF), 16'h000A);
    check(uio === 4'b1010, "USERIO pins");
    cpu.read_word(io_addr(IO_USERIO_OFF));  check_eq(cpu.last_data, 16'h0A00, "USERIO pins read back in IN, OUT reads 0");
    cpu.write_word(io_addr(IO_USERIO_OFF), 16'h000F);
    paljmp = 1;
    cpu.read_word(io_addr(IO_INTSTAT_OFF));
    check(cpu.last_data[INTSTAT_PALJMP_BIT] == 1 && cpu.last_data[INTSTAT_SDMISO_BIT] == 1 && cpu.last_data[INTSTAT_DUARTIRQ_BIT] == 0, "INTSTAT live pins");
    paljmp = 0;
    // IO cycles are slot aligned like RAM
    cpu.read_word(24'h001000);
    for (k = 0; k < 4; k = k + 1) begin
        cpu.idle(k);
        cpu.read_word(io_addr(IO_ID_OFF));
        check_eq(cpu.last_clocks, exp_ram_clocks(2*k), "IO cycle clocks");
        check_eq(cpu.last_data, ID_VALUE, "ID at every alignment");
    end
    // mirror: A5..A15 ignored
    cpu.read_word(io_addr(IO_ID_OFF) + 24'h00FFF0); check_eq(cpu.last_data, ID_VALUE, "IO mirror");
    // NMI button
    nmibtn = 1; #200; check_eq(ipl_n, 3'b000, "IPL = 7 while NMI pressed");
    nmibtn = 0; #200; check_eq(ipl_n, 3'b111, "IPL idle");
    end_test;

    // ------------------------------------------------------------------ 6
    begin_test("soft switch PAGE1 -> VA reload at next frame");
    check(u1.page === 1'b0, "page 0 after reset");
    cpu.write_word(sw_addr(SW_PAGE1_OFF), 16'h0000);
    check(cpu.last_status == 0 && cpu.last_clocks == 4, "switch write is an immediate 4-clock cycle");
    #100;
    check(u1.page === 1'b1, "PAGE flop set");
    @(frame_start);
    @(line_start);                                         // line 0 fetched
    check_eq(first_fetch_ma, FB_PAGE1_WORD, "first fetch of the frame from page 1");
    check_eq(fr_addr_err, 0, "fetch addresses before the switch (page 0) were right");
    cpu.write_word(sw_addr(SW_PAGE0_OFF), 16'h0000);
    #100;
    check(u1.page === 1'b0, "PAGE flop cleared");
    @(frame_start);
    @(line_start);
    check_eq(first_fetch_ma, FB_PAGE0_WORD, "first fetch of the frame from page 0 again");
    check_eq(fr_addr_err, 0, "fetch addresses during the page-1 frame were right");
    end_test;

    // ------------------------------------------------------------------ 7
    begin_test("one full NTSC frame: fetches, lines, syncs, pixel stream");
    // put a recognisable pattern into line 5 of page 0 through the bus
    for (k = 0; k < 16; k = k + 1) cpu.write_word({FB_PAGE0_WORD, 1'b0} + 5*320 + 2*k, {8'hE0 + k, 8'h03 + k});
    cpu.read_word(io_addr(IO_INTSTAT_OFF));
    cpu.write_word(io_addr(IO_INTSTAT_OFF), 16'hFFFF);     // clear VBLANK/HSYNC flags
    @(frame_start);
    pix_check = 1;
    @(frame_start);
    pix_check = 0;
    check_eq(fr_lines, NTSC_V_TOTAL, "lines per frame");
    check_eq(fr_active, NTSC_V_ACTIVE, "active lines (160 fetches each)");
    check_eq(fr_vsync_lines, NTSC_V_SYNC_END - NTSC_V_SYNC_START, "VSYNC lines");
    check_eq(fr_fetch_err, 0, "no line with a wrong fetch count");
    check_eq(fr_slot_err, 0, "every line has H_TOTAL slots");
    check_eq(fr_addr_err, 0, "every fetch address correct");
    check_eq(fr_pix_err, 0, "every pixel matches SRAM contents");
    check(hsync === 1'b1, "HSYNC pin idles high (active-low)");
    cpu.read_word(io_addr(IO_INTSTAT_OFF));
    check(cpu.last_data[INTSTAT_VBLANK_BIT] == 1, "VBLANK flag set during the frame");
    check(cpu.last_data[INTSTAT_HSYNC_BIT] == 1, "HSYNC flag set during the frame");
    cpu.write_word(io_addr(IO_INTEN_OFF), 1 << INTEN_VBLANK_BIT);
    #200; check_eq(~ipl_n & 3'b111, IRQ_VBLANK_LEVEL, "IPL = VBLANK level");
    cpu.write_word(io_addr(IO_INTSTAT_OFF), 1 << INTSTAT_VBLANK_BIT);
    #200; check_eq(ipl_n, 3'b111, "IPL idle after W1C");
    cpu.read_word(io_addr(IO_INTSTAT_OFF));
    check(cpu.last_data[INTSTAT_VBLANK_BIT] == 0, "VBLANK flag cleared by W1C");
    check(cpu.last_data[INTSTAT_HSYNC_BIT] == 1, "HSYNC flag not affected by VBLANK W1C");
    cpu.write_word(io_addr(IO_INTEN_OFF), 1 << INTEN_HSYNC_BIT);
    #200; check_eq(~ipl_n & 3'b111, IRQ_HSYNC_LEVEL, "IPL = HSYNC level");
    cpu.write_word(io_addr(IO_INTEN_OFF), 0);
    cpu.write_word(io_addr(IO_INTSTAT_OFF), 16'hFFFF);
    end_test;

    // ------------------------------------------------------------------ 7b
    begin_test("PAL soft switch: 312-line frame, 192-slot lines");
    cpu.write_word(sw_addr(SW_PAL_OFF), 16'h0000);
    #100; check(u1.pal_req === 1'b1 && u1.pal === 1'b0, "PAL requested, not yet in effect");
    @(frame_start);
    #100; check(u1.pal === 1'b1, "PAL in effect at frame start");
    @(frame_start);
    check_eq(fr_lines, PAL_V_TOTAL, "PAL lines per frame");
    check_eq(fr_active, PAL_V_ACTIVE, "PAL active lines");
    check_eq(fr_vsync_lines, PAL_V_SYNC_END - PAL_V_SYNC_START, "PAL VSYNC lines");
    check_eq(fr_slot_err, 0, "every PAL line has 192 slots");
    check_eq(fr_addr_err, 0, "PAL fetch addresses correct");
    cpu.write_word(sw_addr(SW_NTSC_OFF), 16'h0000);
    @(frame_start);
    #100; check(u1.pal === 1'b0, "back to NTSC");
    end_test;

    // ------------------------------------------------------------------ 8
    begin_test("PS/2 frame -> PS2DATA/PS2RDY and IPL 2");
    cpu.write_word(io_addr(IO_INTEN_OFF), 1 << INTEN_PS2_BIT);
    ps2_send(8'h1C, 1);
    #1000;
    check_eq(~ipl_n & 3'b111, IRQ_PS2_LEVEL, "IPL = PS2 level after frame");
    cpu.read_word(io_addr(IO_INTSTAT_OFF));
    check(cpu.last_data[INTSTAT_PS2RDY_BIT] == 1, "PS2RDY set");
    check(cpu.last_data[INTSTAT_PS2ERR_BIT] == 0, "PS2ERR clear");
    cpu.read_word(io_addr(IO_PS2DATA_OFF)); check_eq(cpu.last_data, 16'h001C, "PS2DATA");
    #200; check_eq(ipl_n, 3'b111, "IPL idle after PS2DATA read");
    cpu.read_word(io_addr(IO_INTSTAT_OFF));
    check(cpu.last_data[INTSTAT_PS2RDY_BIT] == 0, "PS2RDY cleared by read");
    ps2_send(8'hF0, 0);                                    // bad parity
    #1000;
    cpu.read_word(io_addr(IO_INTSTAT_OFF));
    check(cpu.last_data[INTSTAT_PS2RDY_BIT] == 1 && cpu.last_data[INTSTAT_PS2ERR_BIT] == 1, "PS2RDY and PS2ERR on bad parity");
    cpu.read_word(io_addr(IO_PS2DATA_OFF)); check_eq(cpu.last_data, 16'h00F0, "PS2DATA of bad frame");
    cpu.write_word(io_addr(IO_INTSTAT_OFF), 1 << INTSTAT_PS2ERR_BIT);
    cpu.read_word(io_addr(IO_INTSTAT_OFF));
    check(cpu.last_data[INTSTAT_PS2ERR_BIT] == 0, "PS2ERR W1C");
    // watchdog: a truncated frame followed by silence must not desynchronise the receiver
    ps2_send_partial(3);
    #400000;                                               // > 4 HSYNC periods (~254 us)
    check(u2.ps2_busy == 0, "receiver reset by watchdog");
    ps2_send(8'hA7, 1);
    #1000;
    cpu.read_word(io_addr(IO_PS2DATA_OFF)); check_eq(cpu.last_data, 16'h00A7, "byte after resync");
    cpu.write_word(io_addr(IO_INTEN_OFF), 0);
    end_test;

    // ------------------------------------------------------------------ 9
    begin_test("DUART cycles: DUARTCS, DTACK from the chip, CPLD1 releases DTACK");
    check(duartcs_n === 1'b1, "DUARTCS idle high");
    cpu.read_byte(DUART_BASE + DUART_SRA_OFFSET);
    check(cpu.last_status == 0, "DUART byte read gets DTACK");
    check(cpu.last_clocks > 4, "DUART read waited for the chip");
    check_eq(cpu.last_data[7:0], 8'hA0 | DUART_SRA_OFFSET[4:1], "DUART read data (RS = A4..A1)");
    check_eq(duart.reads, 1, "one DUART read strobe");
    cpu.write_byte(DUART_BASE + DUART_TBA_OFFSET, 8'h5A);
    check(cpu.last_status == 0, "DUART byte write gets DTACK");
    check_eq(duart.writes, 1, "one DUART write strobe");
    check_eq(duart.last_wr, 8'h5A, "DUART write data");
    check_eq(duart.last_wr_rs, DUART_TBA_OFFSET[4:1], "DUART write register select");
    cpu.read_byte(DUART_BASE + 24'h00FF00 + DUART_RBA_OFFSET);   // A5..A15 ignored
    check(cpu.last_status == 0 && cpu.last_data[7:0] == (8'hA0 | DUART_RBA_OFFSET[4:1]), "DUART mirror");
    check(iocs_n === 1'b1 && bufen_n === 1'b1 && romoe_n === 1'b1 && romwe_n === 1'b1, "no IOCS/BUFEN/ROMOE/ROMWE during DUART cycles");
    cpu.read_word(24'h001000);  check(cpu.last_status == 0, "RAM still answers after DUART cycles");
    end_test;

    // ------------------------------------------------------------------ 10
    begin_test("DUART IRQ -> INTSTAT.DUARTIRQ and IPL 3");
    duart_irq_n = 0; #300;
    check_eq(ipl_n, 3'b111, "IPL idle while INTEN.DUART clear");
    cpu.read_word(io_addr(IO_INTSTAT_OFF));
    check(cpu.last_data[INTSTAT_DUARTIRQ_BIT] == 1, "INTSTAT.DUARTIRQ follows the pin");
    cpu.write_word(io_addr(IO_INTEN_OFF), 1 << INTEN_DUART_BIT);
    #300; check_eq(~ipl_n & 3'b111, IRQ_DUART_LEVEL, "IPL = DUART level");
    duart_irq_n = 1; #300;
    check_eq(ipl_n, 3'b111, "IPL idle when the DUART releases IRQ");
    cpu.write_word(io_addr(IO_INTEN_OFF), 0);
    end_test;

    // ------------------------------------------------------------------ 11
    begin_test("no bus contention, no SRAM timing violations, phase lock");
    #1000;
    check_eq(contention, 0, "bus contention events");
    check_eq(sram_hi.errors, 0, "SRAM-hi timing violations");
    check_eq(sram_lo.errors, 0, "SRAM-lo timing violations");
    check_eq(flash_hi.errors + flash_lo.errors, 0, "flash timing violations");
    check_eq(phase_err, 0, "CPLD2 phase counter locked to CPLD1");
    check(ev_cpu > 0 && ev_rom > 0 && ev_xa > 0 && ev_xb > 0 && ev_sram > 0 && ev_cpld2 > 0, "every bus driver was exercised");
    $display("     (%0d SRAM-hi writes, %0d SRAM-lo writes, %0d SRAM-hi read enables, %0d bus cycles, %0d frames)",
             sram_hi.writes, sram_lo.writes, sram_hi.reads, cpu.cycles, frames);
    $display("     drive events: cpu %0d rom %0d xcvr->cpu %0d xcvr->sram %0d sram %0d cpld2 %0d",
             ev_cpu, ev_rom, ev_xa, ev_xb, ev_sram, ev_cpld2);
    $display("     SRAM write timing minima: tWP %0t ps (>= 50 ns), tDW %0t ps (>= 30 ns)", sram_hi.min_twp, sram_hi.min_tdw);
    $display("     flash: %0d WE pulses, min tWP %0t ps (>= 40 ns), %0d programs, %0d erases, %0d busy polls", flash_hi.writes, flash_hi.min_twp, flash_hi.programs, flash_hi.erases, flash_hi.busy_reads);
    end_test;

    $display("");
    $display("%0d tests, %0d failed, %0d errors", tests_run, tests_failed, errors);
    if (tests_failed == 0 && errors == 0) $display("RESULT: PASS");
    else $display("RESULT: FAIL");
    $finish;
end

// VCD: stop dumping after 2 ms unless +vcd_all
initial if ($test$plusargs("vcd") && !$test$plusargs("vcd_all")) begin
    #2_000_000 $dumpoff;
end

// global timeout
initial begin
    #200_000_000;
    $display("TIMEOUT");
    $display("RESULT: FAIL");
    $finish;
end

endmodule
