// cpld2_io.v -- CPLD2 "I/O + shifter" for the other-m68k board (ATF1508AS, 5 V).
//
// Single clock clk24 (posedge), asynchronous active-low reset sysreset_n.  The
// 3-bit phase counter ph2 is resynchronised by LATCH from CPLD1 (high during F3 only),
// so the edge at which LATCH is sampled high is E4 and ph2 == n during Fn thereafter.
//
// Register map, bit positions, interrupt levels and the serial divisor come from
// generated/board_params.vh; see docs/architecture.md section 5 and hdl/README.md.
//
// Written to fit the Atmel fitter (docs/atf15xx.md): one clock, GCLR reset, upcounters
// that terminate on all-ones, sentinel shifters instead of bit counters, and a read mux
// that only carries what the CPU cannot shadow.  The control registers INTEN, SDIO,
// GPIO and USERIO.OUT are therefore WRITE-ONLY (they read as 0); the BIOS keeps shadows.
// There is no hardware serial transmitter: SERDATA writes are ignored and SERTXBUSY
// reads 0.
//
// Build options (pending the design decision recorded in STATUS.md):
//   -DNO_PS2HW  no PS/2 receiver; PS2RDY becomes a W1C flag set on every falling edge
//               of PS/2 clock (the CPU shifts the bits itself), PS2DATA reads 0.
//   -DNO_SERHW  no serial receiver (an external UART is used); SERRX/SERRXOVR/SERDATA gone.
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
    // serial
    output wire        ser_tx,
    input  wire        ser_rx,
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

// Serial receiver timing in slots (one tick per E0, 8 fast clocks): 208/8 = 26 per bit.
localparam       SER_SLOTS_PER_BIT = SERIAL_FAST_CLOCKS_PER_BIT / SLOT_FAST_CLOCKS;
localparam [4:0] RX_BIT_LOAD  = 5'd31 - (SER_SLOTS_PER_BIT - 1);       // 26 ticks to all-ones
localparam [4:0] RX_HALF_LOAD = 5'd31 - (SER_SLOTS_PER_BIT / 2 - 1);   // 13 ticks to all-ones

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
wire        sel_serdata = (a[3:1] == IO_SERDATA_OFF[2:0]);
wire        sel_userio  = (a[3:1] == IO_USERIO_OFF[2:0]);
wire        sel_id      = (a[3:1] == IO_ID_OFF[2:0]);

wire        io_rd  = ~iocs_n &  rw;        // CPLD2 drives D while this is true (F5..F7)
wire        wr_en  = e0 & ~iocs_n & ~rw;   // capture write data at E0
wire        rd_end = e0 & ~iocs_n &  rw;   // read side effects at E0

// ---------------------------------------------------------------- registers
reg [3:0]  inten;
reg [2:0]  sdio;                        // CS, CLK, MOSI
reg [5:0]  gpio;                        // PS2CLK, PS2DAT, SERTX, AUDIO, LED0, LED1
reg [3:0]  uio_out;
`ifndef NO_PS2HW
reg [7:0]  ps2data;
reg        f_ps2err;
`endif
`ifndef NO_SERHW
reg [7:0]  serdata;
`endif
reg        f_vblank, f_hsync, f_ps2rdy, f_serrx, f_serrxovr;

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
assign ser_tx  = gpio[GPIO_SERTX_BIT];
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
`ifndef NO_SERHW
        rd_data[INTSTAT_SERRX_BIT]     = f_serrx;
        rd_data[INTSTAT_SERRXOVR_BIT]  = f_serrxovr;
`endif
        rd_data[INTSTAT_SERTXBUSY_BIT] = 1'b0;
`ifndef NO_PS2HW
        rd_data[INTSTAT_PS2ERR_BIT]    = f_ps2err;
`endif
        rd_data[INTSTAT_NMIBTN_BIT]    = nmibtn;
        rd_data[INTSTAT_SDMISO_BIT]    = sd_miso;
        rd_data[INTSTAT_PS2CLK_BIT]    = ps2_clk;
        rd_data[INTSTAT_PS2DAT_BIT]    = ps2_dat;
        rd_data[INTSTAT_SERRXPIN_BIT]  = ser_rx;
        rd_data[INTSTAT_PALJMP_BIT]    = paljmp;
        rd_data[INTSTAT_VSYNCPIN_BIT]  = vs;
        rd_data[INTSTAT_DEPIN_BIT]     = de;
        rd_data[INTSTAT_HSYNCPIN_BIT]  = hs;
    end
`ifndef NO_PS2HW
    sel_ps2data: rd_data[PS2DATA_DATA_BIT +: 8] = ps2data;
`endif
`ifndef NO_SERHW
    sel_serdata: rd_data[SERDATA_DATA_BIT +: 8] = serdata;
`endif
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
`ifndef NO_PS2HW
reg        ps2_busy;
reg [9:0]  ps2_sh;
reg        ps2_par;                     // running parity of data+parity bits
reg [1:0]  ps2_wd;                      // HSYNC periods since the last PS/2 clock edge
wire       ps2_dat_s = ps2_dat;         // data is stable around the clock edge
localparam [9:0] PS2_SENTINEL = 10'b10_0000_0000;
`endif

// ---------------------------------------------------------------- serial receiver
// 10-bit shifter loaded with a sentinel in bit 9 at the start edge; samples are shifted
// in from the top, so the sentinel reaching bit 0 marks the stop-bit sample and the
// byte is then in bits 9..2 (bit 1 holds the start bit).  Sample timing counts slots.
`ifndef NO_SERHW
reg        rx_s1, rx_s2;                // synchroniser
reg        rx_busy;
reg [4:0]  rx_cnt;                      // upcounter in slots; samples when all ones
reg [9:0]  rx_sh;
wire       rx_fall = ~rx_s1 & rx_s2;
localparam [9:0] RX_SENTINEL = 10'b10_0000_0000;
`endif

// ---------------------------------------------------------------- IPL encoder
function [2:0] max3;
    input [2:0] x, y;
    begin max3 = (x > y) ? x : y; end
endfunction
wire [2:0] l_vb  = (f_vblank & inten[INTEN_VBLANK_BIT]) ? IRQ_VBLANK_LEVEL : 3'd0;
wire [2:0] l_hs  = (f_hsync  & inten[INTEN_HSYNC_BIT])  ? IRQ_HSYNC_LEVEL  : 3'd0;
wire [2:0] l_ps2 = (f_ps2rdy & inten[INTEN_PS2_BIT])    ? IRQ_PS2_LEVEL    : 3'd0;
`ifdef NO_SERHW
wire [2:0] l_rx  = 3'd0;
`else
wire [2:0] l_rx  = (f_serrx  & inten[INTEN_SERRX_BIT])  ? IRQ_SERRX_LEVEL  : 3'd0;
`endif
wire [2:0] l_nmi = nmi_s ? IRQ_NMI_LEVEL : 3'd0;
wire [2:0] level = max3(l_nmi, max3(max3(l_vb, l_hs), max3(l_ps2, l_rx)));

// ---------------------------------------------------------------- the one process
always @(posedge clk24 or negedge sysreset_n) begin
    if (!sysreset_n) begin
        ph2      <= 3'd0;
        pix      <= 8'd0;
        lo       <= 8'd0;
        inten    <= 4'd0;
        sdio     <= 3'b011;             // SDIO reset 0x0003: CS high, CLK high
        gpio     <= 6'b000111;          // GPIO reset 0x0007: PS/2 released, SERTX idle
        uio_out  <= 4'hF;               // USERIO reset 0x000F
`ifndef NO_PS2HW
        ps2data  <= 8'd0; f_ps2err <= 1'b0;
`endif
`ifndef NO_SERHW
        serdata  <= 8'd0;
`endif
        f_vblank <= 1'b0; f_hsync <= 1'b0; f_ps2rdy <= 1'b0; f_serrx <= 1'b0;
        f_serrxovr <= 1'b0;
        hs_q     <= 1'b0; vs_q <= 1'b0; nmi_s <= 1'b0;
        ipl_n    <= 3'b111;
        ps2c_s1  <= 1'b1; ps2c_s2 <= 1'b1; ps2c_q <= 1'b1;
`ifndef NO_PS2HW
        ps2_busy <= 1'b0; ps2_sh <= 10'd0; ps2_par <= 1'b0; ps2_wd <= 2'd0;
`endif
`ifndef NO_SERHW
        rx_s1    <= 1'b1; rx_s2 <= 1'b1; rx_busy <= 1'b0;
        rx_cnt   <= 5'd0; rx_sh <= 10'd0;
`endif
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
        ps2c_s1 <= ps2_clk;
        ps2c_s2 <= ps2c_s1;
        ps2c_q  <= ps2c_s2;
`ifndef NO_SERHW
        rx_s1   <= ser_rx;
        rx_s2   <= rx_s1;
`endif

        // -------- register writes (data captured at E0 of a serviced I/O write)
        if (wr_en) begin
            if (sel_intstat) begin                     // W1C bits
                if (d[INTSTAT_VBLANK_BIT])   f_vblank   <= 1'b0;
                if (d[INTSTAT_HSYNC_BIT])    f_hsync    <= 1'b0;
`ifndef NO_PS2HW
                if (d[INTSTAT_PS2ERR_BIT])   f_ps2err   <= 1'b0;
`endif
`ifndef NO_SERHW
                if (d[INTSTAT_SERRXOVR_BIT]) f_serrxovr <= 1'b0;
`endif
            end
            if (sel_inten)  inten   <= d[3:0];
            if (sel_sdio)   sdio    <= d[2:0];
            if (sel_gpio)   gpio    <= d[5:0];
            if (sel_userio) uio_out <= d[USERIO_OUT_BIT +: 4];
        end
        // -------- read side effects
`ifdef NO_PS2HW
        if (wr_en & sel_intstat & d[INTSTAT_PS2RDY_BIT]) f_ps2rdy <= 1'b0;   // W1C
`else
        if (rd_end & sel_ps2data) f_ps2rdy <= 1'b0;
`endif
`ifndef NO_SERHW
        if (rd_end & sel_serdata) f_serrx  <= 1'b0;
`endif

        // -------- interrupt flags (set wins over clear)
        if (vs_rise) f_vblank <= 1'b1;
        if (hs_rise) f_hsync  <= 1'b1;
        ipl_n <= ~level;

`ifdef NO_PS2HW
        // -------- PS/2: one interrupt per clock edge, the CPU shifts the bits
        if (ps2_edge) f_ps2rdy <= 1'b1;
`else
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
`endif

`ifndef NO_SERHW
        // -------- serial receiver, 115200 8N1, sampled on the slot tick
        if (!rx_busy) begin
            if (rx_fall) begin                         // start edge
                rx_busy <= 1'b1;
                rx_sh   <= RX_SENTINEL;
                rx_cnt  <= RX_HALF_LOAD;               // to the middle of the start bit
            end
        end else if (e0) begin
            if (!(&rx_cnt)) begin
                rx_cnt <= rx_cnt + 5'd1;
            end else begin                             // sample point
                rx_cnt <= RX_BIT_LOAD;
                if (rx_sh == RX_SENTINEL) begin        // middle of the start bit
                    if (rx_s2) rx_busy <= 1'b0;        // glitch: not a start bit
                    else rx_sh <= {rx_s2, rx_sh[9:1]};
                end else if (rx_sh[0]) begin           // stop bit
                    rx_busy <= 1'b0;
                    if (rx_s2) begin
                        serdata <= rx_sh[9:2];
                        f_serrx <= 1'b1;
                        if (f_serrx) f_serrxovr <= 1'b1;
                    end
                end else begin                         // data bits, LSB first
                    rx_sh <= {rx_s2, rx_sh[9:1]};
                end
            end
        end
`endif
    end
end

endmodule
