// cpld2_io.v -- CPLD2 "I/O + shifter" for the other-m68k board (ATF1508AS, 5 V).
//
// Single clock clk24 (posedge), asynchronous active-low reset sysreset_n.  The
// 3-bit phase counter ph2 is resynchronised by LATCH from CPLD1 (high during F3 only),
// so the edge at which LATCH is sampled high is E4 and ph2 == n during Fn thereafter.
//
// Register map, bit positions, interrupt levels and the serial divisor come from
// generated/board_params.vh; see docs/architecture.md section 5 and hdl/README.md.
`timescale 1ns/1ps

module cpld2_io #(
    parameter SYNC_ACTIVE_LOW = 1,    // polarity of the hsync/vsync pins (must match CPLD1)
    parameter HW_TX           = 1     // 1: hardware serial transmitter behind SERDATA
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

localparam [7:0] SER_BIT_M1  = SERIAL_FAST_CLOCKS_PER_BIT - 1;      // 207
localparam [7:0] SER_HALF_M1 = SERIAL_FAST_CLOCKS_PER_BIT/2 - 1;    // 103

// ---------------------------------------------------------------- phase and pixel path
reg [2:0] ph2;
reg [7:0] pix;
reg [7:0] lo;
reg       active;
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

wire        io_rd  = ~iocs_n &  rw;    // CPLD2 drives D while this is true (F5..F7)
wire        wr_en  = e0 & ~iocs_n & ~rw;   // capture write data at E0
wire        rd_end = e0 & ~iocs_n &  rw;   // read side effects at E0

// ---------------------------------------------------------------- registers
reg [3:0]  inten;
reg [2:0]  sdio;                        // CS, CLK, MOSI
reg [5:0]  gpio;                        // PS2CLK, PS2DAT, SERTX, AUDIO, LED0, LED1
reg [3:0]  uio_out;
reg [7:0]  ps2data;
reg [7:0]  serdata;
reg        f_vblank, f_hsync, f_ps2rdy, f_serrx, f_ps2err, f_serrxovr;

// sync inputs (positive logic inside)
wire hs = SYNC_ACTIVE_LOW ? ~hsync : hsync;
wire vs = SYNC_ACTIVE_LOW ? ~vsync : vsync;
reg  hs_q, vs_q;
wire hs_rise = hs & ~hs_q;
wire vs_rise = vs & ~vs_q;
reg  nmi_s;

// pin drivers
assign sd_cs   = sdio[SDIO_CS_BIT];
assign sd_clk  = sdio[SDIO_CLK_BIT];
assign sd_mosi = sdio[SDIO_MOSI_BIT];
assign ps2_clk = gpio[GPIO_PS2CLK_BIT] ? 1'bz : 1'b0;
assign ps2_dat = gpio[GPIO_PS2DAT_BIT] ? 1'bz : 1'b0;
assign audio   = gpio[GPIO_AUDIO_BIT];
assign led     = {gpio[GPIO_LED1_BIT], gpio[GPIO_LED0_BIT]};
assign uio[0]  = uio_out[0] ? 1'bz : 1'b0;
assign uio[1]  = uio_out[1] ? 1'bz : 1'b0;
assign uio[2]  = uio_out[2] ? 1'bz : 1'b0;
assign uio[3]  = uio_out[3] ? 1'bz : 1'b0;

// ---------------------------------------------------------------- serial transmitter
// 11-bit shifter {sentinel, stop, data[7:0], start}, shifted right with zero fill; busy
// while anything is left above bit 0, so no bit counter is needed.
reg [10:0] tx_sh;
reg [7:0]  tx_cnt;
wire       tx_busy = |tx_sh[10:1];
wire       tx_out = (HW_TX != 0) ? (tx_sh[0] | ~tx_busy) : 1'b1;
wire       sertxbusy = (HW_TX != 0) ? tx_busy : 1'b0;
assign ser_tx = tx_out & gpio[GPIO_SERTX_BIT];

// ---------------------------------------------------------------- read mux
reg [15:0] rd_data;
always @* begin
    rd_data = 16'h0000;
    case (1'b1)
    sel_intstat: begin
        rd_data[INTSTAT_VBLANK_BIT]    = f_vblank;
        rd_data[INTSTAT_HSYNC_BIT]     = f_hsync;
        rd_data[INTSTAT_PS2RDY_BIT]    = f_ps2rdy;
        rd_data[INTSTAT_SERRX_BIT]     = f_serrx;
        rd_data[INTSTAT_SERTXBUSY_BIT] = sertxbusy;
        rd_data[INTSTAT_PS2ERR_BIT]    = f_ps2err;
        rd_data[INTSTAT_SERRXOVR_BIT]  = f_serrxovr;
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
    sel_inten:   rd_data[3:0] = inten;
    sel_sdio:    begin rd_data[2:0] = sdio; rd_data[SDIO_MISO_BIT] = sd_miso; end
    sel_gpio:    rd_data[5:0] = gpio;
    sel_ps2data: rd_data[PS2DATA_DATA_BIT +: 8] = ps2data;
    sel_serdata: rd_data[SERDATA_DATA_BIT +: 8] = serdata;
    sel_userio:  begin rd_data[USERIO_OUT_BIT +: 4] = uio_out; rd_data[USERIO_IN_BIT +: 4] = uio; end
    sel_id:      rd_data = ID_VALUE;
    default:     rd_data = 16'h0000;
    endcase
end
assign d = io_rd ? rd_data : 16'bz;

// ---------------------------------------------------------------- PS/2 receiver
reg        ps2c_s1, ps2c_s2;            // synchroniser
reg        ps2c_q;
wire       ps2_edge = ps2c_q & ~ps2c_s2;         // falling edge of the synchronised clock
reg [3:0]  ps2_cnt;                     // bits received in this frame (0..10)
reg [7:0]  ps2_sh;
reg        ps2_par;                     // running parity of data+parity bits
reg [2:0]  ps2_wd;                      // HSYNC periods since the last PS/2 clock edge
wire       ps2_dat_s = ps2_dat;         // data is stable around the clock edge

// ---------------------------------------------------------------- serial receiver
// 10-bit shifter loaded with a sentinel in bit 9 at the start edge; samples are shifted
// in from the top, so the sentinel reaching bit 0 marks the stop-bit sample and the
// byte is then in bits 9..2 (bit 1 holds the start bit).
reg        rx_s1, rx_s2;                // synchroniser
reg        rx_busy;
reg [7:0]  rx_cnt;
reg [9:0]  rx_sh;
wire       rx_fall = ~rx_s1 & rx_s2;
localparam [9:0] RX_SENTINEL = 10'b10_0000_0000;

// ---------------------------------------------------------------- IPL encoder
function [2:0] max3;
    input [2:0] x, y;
    begin max3 = (x > y) ? x : y; end
endfunction
wire [2:0] l_vb  = (f_vblank & inten[INTEN_VBLANK_BIT]) ? IRQ_VBLANK_LEVEL : 3'd0;
wire [2:0] l_hs  = (f_hsync  & inten[INTEN_HSYNC_BIT])  ? IRQ_HSYNC_LEVEL  : 3'd0;
wire [2:0] l_ps2 = (f_ps2rdy & inten[INTEN_PS2_BIT])    ? IRQ_PS2_LEVEL    : 3'd0;
wire [2:0] l_rx  = (f_serrx  & inten[INTEN_SERRX_BIT])  ? IRQ_SERRX_LEVEL  : 3'd0;
wire [2:0] l_nmi = nmi_s ? IRQ_NMI_LEVEL : 3'd0;
wire [2:0] level = max3(l_nmi, max3(max3(l_vb, l_hs), max3(l_ps2, l_rx)));

// ---------------------------------------------------------------- the one process
always @(posedge clk24 or negedge sysreset_n) begin
    if (!sysreset_n) begin
        ph2      <= 3'd0;
        pix      <= 8'd0;
        lo       <= 8'd0;
        active   <= 1'b0;
        inten    <= 4'd0;
        sdio     <= 3'b011;             // SDIO reset 0x0003: CS high, CLK high
        gpio     <= 6'b000111;          // GPIO reset 0x0007: PS/2 released, SERTX idle
        uio_out  <= 4'hF;               // USERIO reset 0x000F
        ps2data  <= 8'd0;
        serdata  <= 8'd0;
        f_vblank <= 1'b0; f_hsync <= 1'b0; f_ps2rdy <= 1'b0; f_serrx <= 1'b0;
        f_ps2err <= 1'b0; f_serrxovr <= 1'b0;
        hs_q     <= 1'b0; vs_q <= 1'b0; nmi_s <= 1'b0;
        ipl_n    <= 3'b111;
        tx_cnt   <= 8'd0; tx_sh <= 11'd0;
        ps2c_s1  <= 1'b1; ps2c_s2 <= 1'b1; ps2c_q <= 1'b1;
        ps2_cnt  <= 4'd0; ps2_sh <= 8'd0; ps2_par <= 1'b0; ps2_wd <= 3'd0;
        rx_s1    <= 1'b1; rx_s2 <= 1'b1; rx_busy <= 1'b0;
        rx_cnt   <= 8'd0; rx_sh <= 10'd0;
    end else begin
        // -------- phase counter, synchronised by LATCH
        if (latch) ph2 <= 3'd4; else ph2 <= ph2 + 3'd1;

        // -------- pixel path
        if (latch) begin                               // E4: capture word and DE
            pix    <= de ? d[15:8] : 8'd0;
            lo     <= d[7:0];
            active <= de;
        end else if (e0) begin                         // E0: second pixel of the word
            pix    <= active ? lo : 8'd0;
        end

        // -------- input synchronisers / edge detectors
        hs_q    <= hs;
        vs_q    <= vs;
        nmi_s   <= nmibtn;
        ps2c_s1 <= ps2_clk;
        ps2c_s2 <= ps2c_s1;
        ps2c_q  <= ps2c_s2;
        rx_s1   <= ser_rx;
        rx_s2   <= rx_s1;

        // -------- register writes (data captured at E0 of a serviced I/O write)
        if (wr_en) begin
            if (sel_intstat) begin                     // W1C bits
                if (d[INTSTAT_VBLANK_BIT])   f_vblank   <= 1'b0;
                if (d[INTSTAT_HSYNC_BIT])    f_hsync    <= 1'b0;
                if (d[INTSTAT_PS2ERR_BIT])   f_ps2err   <= 1'b0;
                if (d[INTSTAT_SERRXOVR_BIT]) f_serrxovr <= 1'b0;
            end
            if (sel_inten)  inten   <= d[3:0];
            if (sel_sdio)   sdio    <= d[2:0];
            if (sel_gpio)   gpio    <= d[5:0];
            if (sel_userio) uio_out <= d[USERIO_OUT_BIT +: 4];
        end
        // -------- read side effects
        if (rd_end & sel_ps2data) f_ps2rdy <= 1'b0;
        if (rd_end & sel_serdata) f_serrx  <= 1'b0;

        // -------- interrupt flags (set wins over clear)
        if (vs_rise) f_vblank <= 1'b1;
        if (hs_rise) f_hsync  <= 1'b1;
        ipl_n <= ~level;

        // -------- PS/2 receiver
        if (ps2_edge) begin
            ps2_wd <= 3'd0;
            case (ps2_cnt)
            4'd0: begin                                // start bit must be 0
                if (!ps2_dat_s) begin ps2_cnt <= 4'd1; ps2_par <= 1'b0; end
            end
            4'd9: begin                                // parity bit
                ps2_par <= ps2_par ^ ps2_dat_s;
                ps2_cnt <= 4'd10;
            end
            4'd10: begin                               // stop bit: frame complete
                ps2data  <= ps2_sh;
                f_ps2rdy <= 1'b1;
                f_ps2err <= ~(ps2_dat_s & ps2_par);    // odd parity: data^parity must be 1
                ps2_cnt  <= 4'd0;
            end
            default: begin                             // data bits 1..8, LSB first
                ps2_sh  <= {ps2_dat_s, ps2_sh[7:1]};
                ps2_par <= ps2_par ^ ps2_dat_s;
                ps2_cnt <= ps2_cnt + 4'd1;
            end
            endcase
        end else if (hs_rise) begin                    // watchdog: 4 HSYNC periods idle
            if (ps2_wd == 3'd4) ps2_cnt <= 4'd0;
            else ps2_wd <= ps2_wd + 3'd1;
        end

        // -------- serial receiver, 115200 8N1
        if (!rx_busy) begin
            if (rx_fall) begin                         // start edge
                rx_busy <= 1'b1;
                rx_sh   <= RX_SENTINEL;
                rx_cnt  <= SER_HALF_M1;                // to the middle of the start bit
            end
        end else if (rx_cnt != 8'd0) begin
            rx_cnt <= rx_cnt - 8'd1;
        end else begin                                 // sample point
            rx_cnt <= SER_BIT_M1;
            if (rx_sh == RX_SENTINEL) begin            // middle of the start bit
                if (rx_s2) rx_busy <= 1'b0;            // glitch: not a start bit
                else rx_sh <= {rx_s2, rx_sh[9:1]};
            end else if (rx_sh[0]) begin               // stop bit
                rx_busy <= 1'b0;
                if (rx_s2) begin
                    serdata <= rx_sh[9:2];
                    f_serrx <= 1'b1;
                    if (f_serrx) f_serrxovr <= 1'b1;
                end
            end else begin                             // data bits, LSB first
                rx_sh <= {rx_s2, rx_sh[9:1]};
            end
        end

        // -------- serial transmitter
        if (HW_TX != 0) begin
            if (!tx_busy) begin
                if (wr_en & sel_serdata) begin
                    tx_sh  <= {2'b11, d[SERDATA_DATA_BIT +: 8], 1'b0};
                    tx_cnt <= SER_BIT_M1;
                end
            end else if (tx_cnt != 8'd0) begin
                tx_cnt <= tx_cnt - 8'd1;
            end else begin
                tx_cnt <= SER_BIT_M1;
                tx_sh  <= {1'b0, tx_sh[10:1]};
            end
        end
    end
end

endmodule
