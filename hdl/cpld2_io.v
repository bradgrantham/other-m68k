// cpld2_io.v -- CPLD2 "I/O + shifter" for the other-m68k board (ATF1508AS, 5 V).
//
// Single clock clk24 (posedge), asynchronous active-low reset sysreset_n.  The
// 3-bit phase counter ph2 is resynchronised by LATCH from CPLD1 (high during F3 only),
// so the edge at which LATCH is sampled high is E4 and ph2 == n during Fn thereafter.
//
// Register map, bit positions, interrupt levels and the serial divisor come from
// generated/board_params.vh; see docs/architecture.md section 5 and hdl/README.md.
//
// Written to fit the Atmel fitter (docs/atf15xx.md): one clock, GCLR reset, a sentinel
// shifter instead of a bit counter, and a read mux that only carries what the CPU cannot
// shadow.  The control registers INTEN, SDIO,
// GPIO and USERIO.OUT are therefore WRITE-ONLY (they read as 0); the BIOS keeps shadows.
// Serial lives in a 68681 DUART on the CPU bus (spec revision 2); CPLD2 only routes the
// DUART's IRQ pin into the interrupt encoder (level 3) and INTSTAT.DUARTIRQ.
`timescale 1ns/1ps

module cpld2_io #(
    parameter SYNC_ACTIVE_LOW = 1     // polarity of the hsync/vsync pins (must match CPLD1)
) (
    input  wire        clk24,
    input  wire        sysreset_n,
    // CPU-side control (A1..A3 straight from the CPU, IOCS from CPLD1)
    input  wire [3:1]  a,
    input  wire        iocs_n,
    input  wire        rw,
    // from CPLD1
    input  wire        latch,
    input  wire        de,
    input  wire        hsync,
    input  wire        vsync,
    // SRAM-side data bus
    inout  wire [15:0] d,
    // 68000 interrupt priority
    output reg  [2:0]  ipl_n,
    // RGB332 pixel to the resistor DACs: {R2,R1,R0,G2,G1,G0,B1,B0}
    output wire [7:0]  rgb,
    // SD card (software SPI)
    output wire        sd_cs,
    output wire        sd_clk,
    output wire        sd_mosi,
    input  wire        sd_miso,
    // PS/2 (open drain)
    inout  wire        ps2_clk,
    inout  wire        ps2_dat,
    // 68681 DUART interrupt request (open drain, active low)
    input  wire        duart_irq_n,
    // misc
    output wire        audio,
    output wire [1:0]  led,
    input  wire        paljmp,
    input  wire        nmibtn,        // 1 = pressed
    inout  wire [3:0]  uio            // open drain with pull-ups
);

`ifdef BOARD_PARAMS_VH
`undef BOARD_PARAMS_VH
`endif
`include "board_params.vh"

// ---------------------------------------------------------------- phase and pixel path
reg [2:0] ph2;
reg [7:0] pix;
reg [7:0] lo;
assign rgb = pix;

wire e0 = (ph2 == 3'd7);               // the coming edge is E0

// ---------------------------------------------------------------- bus interface
wire        sel_intstat = (a[3:1] == IO_INTSTAT_OFF[2:0]);
wire        sel_inten   = (a[3:1] == IO_INTEN_OFF[2:0]);
wire        sel_sdio    = (a[3:1] == IO_SDIO_OFF[2:0]);
wire        sel_gpio    = (a[3:1] == IO_GPIO_OFF[2:0]);
wire        sel_ps2data = (a[3:1] == IO_PS2DATA_OFF[2:0]);
wire        sel_userio  = (a[3:1] == IO_USERIO_OFF[2:0]);
wire        sel_id      = (a[3:1] == IO_ID_OFF[2:0]);

wire        io_rd  = ~iocs_n &  rw;        // CPLD2 drives D while this is true (F5..F7)
wire        wr_en  = e0 & ~iocs_n & ~rw;   // capture write data at E0
wire        rd_end = e0 & ~iocs_n &  rw;   // read side effects at E0

// ---------------------------------------------------------------- registers
reg [3:0]  inten;
reg [2:0]  sdio;                        // CS, CLK, MOSI
reg [4:0]  gpio;                        // PS2CLK, PS2DAT, AUDIO, LED0, LED1
reg [3:0]  uio_out;
reg [7:0]  ps2data;
reg        f_ps2err;
reg        f_vblank, f_hsync, f_ps2rdy;
reg        dirq_s;                      // DUART IRQ, synchronised, positive logic

// sync inputs (positive logic inside)
wire hs = SYNC_ACTIVE_LOW ? ~hsync : hsync;
wire vs = SYNC_ACTIVE_LOW ? ~vsync : vsync;
reg  hs_q, vs_q;
wire hs_rise = hs & ~hs_q;
wire vs_rise = vs & ~vs_q;
reg  nmi_s;

// pin drivers.  Open-drain pins: the flop that holds the bit also drives the pin
// (value 0) and its complement is the output enable, so pin and register share a cell.
assign sd_cs   = sdio[SDIO_CS_BIT];
assign sd_clk  = sdio[SDIO_CLK_BIT];
assign sd_mosi = sdio[SDIO_MOSI_BIT];
assign ps2_clk = gpio[GPIO_PS2CLK_BIT] ? 1'bz : gpio[GPIO_PS2CLK_BIT];
assign ps2_dat = gpio[GPIO_PS2DAT_BIT] ? 1'bz : gpio[GPIO_PS2DAT_BIT];
assign audio   = gpio[GPIO_AUDIO_BIT];
assign led     = {gpio[GPIO_LED1_BIT], gpio[GPIO_LED0_BIT]};
assign uio[0]  = uio_out[0] ? 1'bz : uio_out[0];
assign uio[1]  = uio_out[1] ? 1'bz : uio_out[1];
assign uio[2]  = uio_out[2] ? 1'bz : uio_out[2];
assign uio[3]  = uio_out[3] ? 1'bz : uio_out[3];

// ---------------------------------------------------------------- read mux
reg [15:0] rd_data;
always @* begin
    rd_data = 16'h0000;
    case (1'b1)
    sel_intstat: begin
        rd_data[INTSTAT_VBLANK_BIT]    = f_vblank;
        rd_data[INTSTAT_HSYNC_BIT]     = f_hsync;
        rd_data[INTSTAT_PS2RDY_BIT]    = f_ps2rdy;
        rd_data[INTSTAT_DUARTIRQ_BIT]  = dirq_s;
        rd_data[INTSTAT_PS2ERR_BIT]    = f_ps2err;
        rd_data[INTSTAT_NMIBTN_BIT]    = nmibtn;
        rd_data[INTSTAT_SDMISO_BIT]    = sd_miso;
        rd_data[INTSTAT_PS2CLK_BIT]    = ps2_clk;
        rd_data[INTSTAT_PS2DAT_BIT]    = ps2_dat;
        rd_data[INTSTAT_PALJMP_BIT]    = paljmp;
        rd_data[INTSTAT_VSYNCPIN_BIT]  = vs;
        rd_data[INTSTAT_DEPIN_BIT]     = de;
        rd_data[INTSTAT_HSYNCPIN_BIT]  = hs;
    end
    sel_ps2data: rd_data[PS2DATA_DATA_BIT +: 8] = ps2data;
    sel_userio:  rd_data[USERIO_IN_BIT +: 4] = uio;
    sel_id:      rd_data = ID_VALUE;
    default:     rd_data = 16'h0000;
    endcase
end
assign d = io_rd ? rd_data : 16'bz;

// ---------------------------------------------------------------- PS/2 receiver
// 10-bit shifter loaded with a sentinel in bit 9 when the start bit is seen; data and
// parity shift in from the top, so the sentinel reaching bit 0 marks the stop-bit edge
// and the byte is then in bits 8..1 (bit 9 is the parity bit).
reg        ps2c_s1, ps2c_s2;            // synchroniser
reg        ps2c_q;
wire       ps2_edge = ps2c_q & ~ps2c_s2;         // falling edge of the synchronised clock
reg        ps2_busy;
reg [9:0]  ps2_sh;
reg        ps2_par;                     // running parity of data+parity bits
reg [1:0]  ps2_wd;                      // HSYNC periods since the last PS/2 clock edge
wire       ps2_dat_s = ps2_dat;         // data is stable around the clock edge
localparam [9:0] PS2_SENTINEL = 10'b10_0000_0000;

// ---------------------------------------------------------------- IPL encoder
function [2:0] max3;
    input [2:0] x, y;
    begin max3 = (x > y) ? x : y; end
endfunction
wire [2:0] l_vb  = (f_vblank & inten[INTEN_VBLANK_BIT]) ? IRQ_VBLANK_LEVEL : 3'd0;
wire [2:0] l_hs  = (f_hsync  & inten[INTEN_HSYNC_BIT])  ? IRQ_HSYNC_LEVEL  : 3'd0;
wire [2:0] l_ps2 = (f_ps2rdy & inten[INTEN_PS2_BIT])    ? IRQ_PS2_LEVEL    : 3'd0;
wire [2:0] l_du  = (dirq_s   & inten[INTEN_DUART_BIT])  ? IRQ_DUART_LEVEL  : 3'd0;
wire [2:0] l_nmi = nmi_s ? IRQ_NMI_LEVEL : 3'd0;
wire [2:0] level = max3(l_nmi, max3(max3(l_vb, l_hs), max3(l_ps2, l_du)));

// ---------------------------------------------------------------- the one process
always @(posedge clk24 or negedge sysreset_n) begin
    if (!sysreset_n) begin
        ph2      <= 3'd0;
        pix      <= 8'd0;
        lo       <= 8'd0;
        inten    <= 4'd0;
        sdio     <= 3'b001;             // SDIO reset 0x0001: CS high, CLK low
        gpio     <= 5'b00011;           // GPIO reset 0x0003: PS/2 released
        uio_out  <= 4'hF;               // USERIO reset 0x000F
        ps2data  <= 8'd0; f_ps2err <= 1'b0;
        f_vblank <= 1'b0; f_hsync <= 1'b0; f_ps2rdy <= 1'b0; dirq_s <= 1'b0;
        hs_q     <= 1'b0; vs_q <= 1'b0; nmi_s <= 1'b0;
        ipl_n    <= 3'b111;
        ps2c_s1  <= 1'b1; ps2c_s2 <= 1'b1; ps2c_q <= 1'b1;
        ps2_busy <= 1'b0; ps2_sh <= 10'd0; ps2_par <= 1'b0; ps2_wd <= 2'd0;
    end else begin
        // -------- phase counter, synchronised by LATCH
        if (latch) ph2 <= 3'd4; else ph2 <= ph2 + 3'd1;

        // -------- pixel path: E4 captures the word (zero outside DE), E0 emits the low pixel
        if (latch) begin
            pix <= de ? d[15:8] : 8'd0;
            lo  <= de ? d[7:0]  : 8'd0;
        end else if (e0) begin
            pix <= lo;
        end

        // -------- input synchronisers / edge detectors
        hs_q    <= hs;
        vs_q    <= vs;
        nmi_s   <= nmibtn;
        dirq_s  <= ~duart_irq_n;
        ps2c_s1 <= ps2_clk;
        ps2c_s2 <= ps2c_s1;
        ps2c_q  <= ps2c_s2;

        // -------- register writes (data captured at E0 of a serviced I/O write)
        if (wr_en) begin
            if (sel_intstat) begin                     // W1C bits
                if (d[INTSTAT_VBLANK_BIT])   f_vblank   <= 1'b0;
                if (d[INTSTAT_HSYNC_BIT])    f_hsync    <= 1'b0;
                if (d[INTSTAT_PS2ERR_BIT])   f_ps2err   <= 1'b0;
            end
            if (sel_inten)  inten   <= d[3:0];
            if (sel_sdio)   sdio    <= d[2:0];
            if (sel_gpio)   gpio    <= d[4:0];
            if (sel_userio) uio_out <= d[USERIO_OUT_BIT +: 4];
        end
        // -------- read side effects
        if (rd_end & sel_ps2data) f_ps2rdy <= 1'b0;

        // -------- interrupt flags (set wins over clear)
        if (vs_rise) f_vblank <= 1'b1;
        if (hs_rise) f_hsync  <= 1'b1;
        ipl_n <= ~level;

        // -------- PS/2 receiver
        if (ps2_edge) begin
            ps2_wd <= 2'd0;
            if (!ps2_busy) begin                       // start bit must be 0
                if (!ps2_dat_s) begin
                    ps2_busy <= 1'b1;
                    ps2_sh   <= PS2_SENTINEL;
                    ps2_par  <= 1'b0;
                end
            end else if (ps2_sh[0]) begin              // stop bit: frame complete
                ps2_busy <= 1'b0;
                ps2data  <= ps2_sh[8:1];
                f_ps2rdy <= 1'b1;
                f_ps2err <= ~(ps2_dat_s & ps2_par);    // odd parity: data^parity must be 1
            end else begin                             // data bits 1..8 (LSB first), parity
                ps2_sh  <= {ps2_dat_s, ps2_sh[9:1]};
                ps2_par <= ps2_par ^ ps2_dat_s;
            end
        end else if (hs_rise & ps2_busy) begin         // watchdog: ~3 HSYNC periods idle
            if (&ps2_wd) ps2_busy <= 1'b0;
            else ps2_wd <= ps2_wd + 2'd1;
        end

    end
end

endmodule

// ---- pin block (written by pins.py from a fitter run; see hdl/README.md) ----
//PIN: CHIP "cpld2_io" ASSIGNED TO AN PLCC84
//PIN: sysreset_n     : 1
//PIN: sd_cs          : 4
//PIN: D_13           : 5
//PIN: D_15           : 6
//PIN: sd_miso        : 8
//PIN: iocs_n         : 9
//PIN: A_2            : 10
//PIN: A_1            : 11
//PIN: A_0            : 12
//PIN: sd_clk         : 15
//PIN: duart_irq_n    : 17
//PIN: paljmp         : 18
//PIN: de             : 20
//PIN: LATCH          : 21
//PIN: rw             : 22
//PIN: sd_mosi        : 24
//PIN: audio          : 25
//PIN: led_0          : 27
//PIN: led_1          : 28
//PIN: D_2            : 29
//PIN: D_10           : 30
//PIN: D_0            : 31
//PIN: ipl_n_0        : 33
//PIN: rgb_4          : 34
//PIN: ipl_n_1        : 35
//PIN: D_1            : 36
//PIN: ipl_n_2        : 37
//PIN: D_3            : 39
//PIN: D_14           : 40
//PIN: hsync          : 41
//PIN: vsync          : 44
//PIN: rgb_0          : 45
//PIN: rgb_6          : 46
//PIN: rgb_1          : 48
//PIN: rgb_5          : 49
//PIN: rgb_7          : 50
//PIN: rgb_3          : 51
//PIN: rgb_2          : 52
//PIN: a18            : 54
//PIN: a19            : 55
//PIN: ma17           : 56
//PIN: ma18           : 57
//PIN: D_6            : 63
//PIN: D_12           : 64
//PIN: D_11           : 65
//PIN: D_8            : 67
//PIN: D_7            : 68
//PIN: D_9            : 69
//PIN: D_5            : 70
//PIN: nmibtn         : 73
//PIN: uio_3          : 74
//PIN: uio_2          : 75
//PIN: uio_1          : 76
//PIN: uio_0          : 77
//PIN: ps2_dat        : 79
//PIN: ps2_clk        : 80
//PIN: D_4            : 81
//PIN: clk24          : 83
// ---- end pin block ----
