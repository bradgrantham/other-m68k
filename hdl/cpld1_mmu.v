// cpld1_mmu.v -- CPLD1 "MMU" for the other-m68k board (ATF1508AS, 5 V).
//
// Everything runs on the 24 MHz oscillator; the only clock is clk24 (posedge) and
// the only reset is the asynchronous active-low sysreset_n.  Time is described on
// the 24 MHz grid as edges E0..E7 (one slot = 8 fast clocks = 4 CPU clocks); the
// interval after edge En is Fn.  The 3-bit phase counter `ph` holds n during Fn, so
// an action scheduled "at En" is written as `if (ph == n-1)`.
//
// See docs/architecture.md sections 2-4 and hdl/README.md for the slot schedule.
`timescale 1ns/1ps

module cpld1_mmu #(
    parameter SYNC_ACTIVE_LOW = 1     // 1: HSYNC/VSYNC pins are active-low (AD724)
) (
    input  wire        clk24,
    input  wire        sysreset_n,
    // 68000 side
    input  wire [23:1] a,
    input  wire        as_n,
    input  wire        uds_n,
    input  wire        lds_n,
    input  wire        rw,            // 1 = read
    output wire        cpuclk,        // 12 MHz, high during F0,F2,F4,F6
    output wire        dtack_n,
    output wire        berr_n,
    output wire        vpa_n,
    // SRAM side
    output reg  [16:0] ma,            // multiplexed SRAM word address
    output wire        ramoe_n,
    output wire        weu_n,         // write enable, upper byte (D15..D8)
    output wire        wel_n,         // write enable, lower byte (D7..D0)
    // other chip selects
    output wire        romce_n,
    output wire        iocs_n,
    output wire        bufen_n,       // 74AHCT245 enable
    // to CPLD2 / AD724
    output reg         latch,         // high during F3 of every slot
    output reg         de,            // display enable, in the slot of the fetch
    output wire        hsync,
    output wire        vsync
);

`ifdef BOARD_PARAMS_VH
`undef BOARD_PARAMS_VH
`endif
`include "board_params.vh"

// ---------------------------------------------------------------- phase counter
reg [2:0] ph;                          // ph == n during Fn
assign cpuclk = ~ph[0];                // F0,F2,F4,F6 high; E0 is a cpuclk rising edge

// ---------------------------------------------------------------- address decode
wire ram_region = (a[23:24-RAM_DECODE_BITS]      == RAM_BASE[23:24-RAM_DECODE_BITS]);
wire rom_region = (a[23:24-ROM_DECODE_BITS]      == ROM_BASE[23:24-ROM_DECODE_BITS]);
wire io_region  = (a[23:24-IO_DECODE_BITS]       == IO_BASE[23:24-IO_DECODE_BITS]);
wire sw_region  = (a[23:24-SWITCHES_DECODE_BITS] == SWITCHES_BASE[23:24-SWITCHES_DECODE_BITS]);
wire iack       = &a[23:4];

reg  overlay;                          // set by reset; RAM region reads the EPROM while set
wire rom_sel = rom_region | (ram_region & overlay);
wire ram_sel = ram_region & ~overlay;
wire mapped  = ram_region | rom_region | io_region | sw_region;
wire as_low  = ~as_n;

// ---------------------------------------------------------------- bus-cycle state
reg service;      // this slot services a RAM/IO cycle (set E4, cleared E0)
reg serviced;     // the current AS-low cycle has already been serviced
reg dtack_r;      // slot-aligned DTACK (RAM/IO)
reg ramoe;        // active-high internal copies of the strobes
reg bufen;
reg iocs;
reg we_phase;     // F6|F7 of a slot servicing a RAM write
reg rom_cyc;      // an AS-low cycle in the real ROM region is in progress (overlay clear)

// Decision at E4: AS low, RAM or IO, not yet serviced.
wire svc_now = as_low & (ram_sel | io_region) & ~serviced;

// Combinational outputs (per architecture.md): ROMCE, BERR, VPA, immediate DTACK,
// and the UDS/LDS gating inside WE.
assign romce_n = ~(as_low & rom_sel & rw);
assign berr_n  = ~(as_low & ~mapped & ~iack);
assign vpa_n   = ~(as_low & iack);
assign dtack_n = ~(dtack_r | (as_low & (rom_sel | sw_region)));
assign weu_n   = ~(we_phase & ~uds_n);
assign wel_n   = ~(we_phase & ~lds_n);
assign ramoe_n = ~ramoe;
assign bufen_n = ~bufen;
assign iocs_n  = ~iocs;

// ---------------------------------------------------------------- video timing
reg  [7:0]  h;          // slot counter within the line
reg  [8:0]  v;          // line counter
reg         vact;       // v < V_ACTIVE
reg         hsync_i;    // positive-logic sync
reg         vsync_i;
reg  [16:0] va;         // video word address
reg         page;       // soft switch: 0 = FB_PAGE0, 1 = FB_PAGE1
reg         pal_req;    // soft switch as written
reg         pal;        // soft switch in effect (changes at frame start)

// Compare constants; PAL/NTSC selected by the `pal` flop.  Values equal in both
// standards fold to a constant in synthesis.
wire [7:0] h_total_m1      = pal ? PAL_H_TOTAL-1      : NTSC_H_TOTAL-1;
wire [7:0] h_active_m1     = pal ? PAL_H_ACTIVE-1     : NTSC_H_ACTIVE-1;
wire [7:0] h_sync_start_m1 = pal ? PAL_H_SYNC_START-1 : NTSC_H_SYNC_START-1;
wire [7:0] h_sync_end_m1   = pal ? PAL_H_SYNC_END-1   : NTSC_H_SYNC_END-1;
wire [8:0] v_total_m1      = pal ? PAL_V_TOTAL-1      : NTSC_V_TOTAL-1;
wire [8:0] v_active_m1     = pal ? PAL_V_ACTIVE-1     : NTSC_V_ACTIVE-1;
wire [8:0] v_sync_start_m1 = pal ? PAL_V_SYNC_START-1 : NTSC_V_SYNC_START-1;
wire [8:0] v_sync_end_m1   = pal ? PAL_V_SYNC_END-1   : NTSC_V_SYNC_END-1;

assign hsync = SYNC_ACTIVE_LOW ? ~hsync_i : hsync_i;
assign vsync = SYNC_ACTIVE_LOW ? ~vsync_i : vsync_i;

// ---------------------------------------------------------------- the one process
always @(posedge clk24 or negedge sysreset_n) begin
    if (!sysreset_n) begin
        ph       <= 3'd0;
        ma       <= 17'd0;
        latch    <= 1'b0;
        service  <= 1'b0;
        serviced <= 1'b0;
        dtack_r  <= 1'b0;
        ramoe    <= 1'b0;
        bufen    <= 1'b0;
        iocs     <= 1'b0;
        we_phase <= 1'b0;
        overlay  <= 1'b1;
        rom_cyc  <= 1'b0;
        h        <= 8'd0;
        v        <= 9'd0;
        vact     <= 1'b1;
        de       <= 1'b1;        // h=0, v=0 is the first active slot
        hsync_i  <= 1'b0;
        vsync_i  <= 1'b0;
        va       <= FB_PAGE0_WORD;
        page     <= 1'b0;
        pal_req  <= 1'b0;
        pal      <= 1'b0;
    end else begin
        ph <= ph + 3'd1;

        // -------- cycle bookkeeping sampled on every fast edge
        if (as_n) begin
            serviced <= 1'b0;                 // AS seen high: the cycle is over
            if (rom_cyc) overlay <= 1'b0;     // AS rose after a real-ROM access
            rom_cyc <= 1'b0;
        end else begin
            if (rom_region) rom_cyc <= 1'b1;
            // soft switches: any write into the SWITCHES region, A2..A1 select
            if (sw_region & ~rw) begin
                if (a[2:1] == SW_PAGE0_OFF[1:0]) page    <= 1'b0;
                if (a[2:1] == SW_PAGE1_OFF[1:0]) page    <= 1'b1;
                if (a[2:1] == SW_NTSC_OFF[1:0])  pal_req <= 1'b0;
                if (a[2:1] == SW_PAL_OFF[1:0])   pal_req <= 1'b1;
            end
        end

        // -------- slot schedule
        case (ph)
        3'd7: begin                            // ---- E0
            we_phase <= 1'b0;
            bufen    <= 1'b0;
            iocs     <= 1'b0;
            dtack_r  <= 1'b0;
            ramoe    <= 1'b0;                  // (not in the doc's E0 list; harmless, see README)
            if (service) serviced <= 1'b1;     // set wins over the AS-high clear above
            service  <= 1'b0;

            // horizontal counter advances at E0
            if (h == h_total_m1) begin
                h  <= 8'd0;
                de <= vact;                    // vact already reflects the new line
            end else begin
                h  <= h + 8'd1;
            end
            if (h == h_active_m1) de <= 1'b0;
            if (h == h_sync_end_m1) hsync_i <= 1'b0;
            if (h == h_sync_start_m1) begin
                hsync_i <= 1'b1;
                // vertical counter advances at the slot where HSYNC begins
                if (v == v_total_m1) begin
                    v    <= 9'd0;
                    vact <= 1'b1;
                    va   <= page ? FB_PAGE1_WORD : FB_PAGE0_WORD;   // frame start
                    pal  <= pal_req;
                end else begin
                    v    <= v + 9'd1;
                end
                if (v == v_active_m1)     vact    <= 1'b0;
                if (v == v_sync_start_m1) vsync_i <= 1'b1;
                if (v == v_sync_end_m1)   vsync_i <= 1'b0;
            end
        end
        3'd0: begin                            // ---- E1
            ma <= va;
        end
        3'd1: begin                            // ---- E2
            if (de) ramoe <= 1'b1;             // video read
        end
        3'd2: begin                            // ---- E3
            latch <= 1'b1;
        end
        3'd3: begin                            // ---- E4
            latch <= 1'b0;
            ma    <= a[17:1];
            if (de) va <= va + 17'd1;
            service <= svc_now;
            dtack_r <= svc_now;
            ramoe   <= svc_now & ram_sel & rw; // stays on only for a CPU read of RAM
        end
        3'd4: begin                            // ---- E5
            if (service) begin
                bufen <= 1'b1;
                iocs  <= io_region;
            end
        end
        3'd5: begin                            // ---- E6
            if (service & ram_sel & ~rw) we_phase <= 1'b1;
        end
        default: begin                         // ---- E7: nothing
        end
        endcase
    end
end

endmodule
