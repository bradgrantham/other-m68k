// Generated from spec/board.json by tools/gen.py -- DO NOT EDIT; edit the spec and run `make gen`.
`ifndef BOARD_PARAMS_VH
`define BOARD_PARAMS_VH

localparam OSC_HZ = 24000000;
localparam CPU_CLOCK_HZ = 12000000;
localparam SLOT_FAST_CLOCKS = 8;

localparam [23:0] RAM_BASE = 24'h000000;
localparam [23:0] RAM_SIZE = 24'h040000;
localparam RAM_DECODE_BITS = 6; // compare A23..A18 against RAM_BASE
localparam [23:0] ROM_BASE = 24'hE00000;
localparam [23:0] ROM_SIZE = 24'h020000;
localparam ROM_DECODE_BITS = 7; // compare A23..A17 against ROM_BASE
localparam [23:0] IO_BASE = 24'hF00000;
localparam [23:0] IO_SIZE = 24'h010000;
localparam IO_DECODE_BITS = 8; // compare A23..A16 against IO_BASE
localparam [23:0] SWITCHES_BASE = 24'hF10000;
localparam [23:0] SWITCHES_SIZE = 24'h010000;
localparam SWITCHES_DECODE_BITS = 8; // compare A23..A16 against SWITCHES_BASE
localparam [23:0] DUART_BASE = 24'hF20000;
localparam [23:0] DUART_SIZE = 24'h010000;
localparam DUART_DECODE_BITS = 8; // compare A23..A16 against DUART_BASE

localparam [16:0] FB_PAGE0_WORD = 17'h16A00; // byte address 0x02D400 >> 1
localparam [16:0] FB_PAGE1_WORD = 17'h0D400; // byte address 0x01A800 >> 1
// NTSC horizontal values in slots (4 CPU clocks each), vertical in lines
localparam NTSC_H_TOTAL  = 191;
localparam NTSC_H_ACTIVE = 160;
localparam NTSC_H_SYNC_START = 164;
localparam NTSC_H_SYNC_END   = 178;
localparam NTSC_V_TOTAL  = 262;
localparam NTSC_V_ACTIVE = 240;
localparam NTSC_V_SYNC_START = 243;
localparam NTSC_V_SYNC_END   = 246;
// PAL horizontal values in slots (4 CPU clocks each), vertical in lines
localparam PAL_H_TOTAL  = 192;
localparam PAL_H_ACTIVE = 160;
localparam PAL_H_SYNC_START = 164;
localparam PAL_H_SYNC_END   = 178;
localparam PAL_V_TOTAL  = 312;
localparam PAL_V_ACTIVE = 240;
localparam PAL_V_SYNC_START = 270;
localparam PAL_V_SYNC_END   = 273;

localparam [3:0] IO_INTSTAT_OFF = 4'd0; // byte offset 0
localparam INTSTAT_VBLANK_BIT = 0;
localparam INTSTAT_HSYNC_BIT = 1;
localparam INTSTAT_PS2RDY_BIT = 2;
localparam INTSTAT_DUARTIRQ_BIT = 3;
localparam INTSTAT_PS2ERR_BIT = 5;
localparam INTSTAT_NMIBTN_BIT = 7;
localparam INTSTAT_SDMISO_BIT = 8;
localparam INTSTAT_PS2CLK_BIT = 9;
localparam INTSTAT_PS2DAT_BIT = 10;
localparam INTSTAT_PALJMP_BIT = 12;
localparam INTSTAT_VSYNCPIN_BIT = 13;
localparam INTSTAT_DEPIN_BIT = 14;
localparam INTSTAT_HSYNCPIN_BIT = 15;
localparam [3:0] IO_INTEN_OFF = 4'd1; // byte offset 2
localparam INTEN_VBLANK_BIT = 0;
localparam INTEN_HSYNC_BIT = 1;
localparam INTEN_PS2_BIT = 2;
localparam INTEN_DUART_BIT = 3;
localparam [3:0] IO_SDIO_OFF = 4'd2; // byte offset 4
localparam SDIO_CS_BIT = 0;
localparam SDIO_CLK_BIT = 1;
localparam SDIO_MOSI_BIT = 2;
localparam [3:0] IO_GPIO_OFF = 4'd3; // byte offset 6
localparam GPIO_PS2CLK_BIT = 0;
localparam GPIO_PS2DAT_BIT = 1;
localparam GPIO_AUDIO_BIT = 2;
localparam GPIO_LED0_BIT = 3;
localparam GPIO_LED1_BIT = 4;
localparam [3:0] IO_PS2DATA_OFF = 4'd4; // byte offset 8
localparam PS2DATA_DATA_BIT = 0;
localparam [3:0] IO_USERIO_OFF = 4'd6; // byte offset 12
localparam USERIO_OUT_BIT = 0;
localparam USERIO_IN_BIT = 8;
localparam [3:0] IO_ID_OFF = 4'd7; // byte offset 14
localparam ID_REV_BIT = 0;
localparam ID_MAGIC_BIT = 8;
localparam [3:0] SW_PAGE0_OFF = 4'd0; // byte offset 0
localparam [3:0] SW_PAGE1_OFF = 4'd1; // byte offset 2
localparam [3:0] SW_NTSC_OFF = 4'd2; // byte offset 4
localparam [3:0] SW_PAL_OFF = 4'd3; // byte offset 6
localparam [15:0] ID_VALUE = 16'h6802;

localparam [2:0] IRQ_VBLANK_LEVEL = 3'd1;
localparam [2:0] IRQ_PS2_LEVEL = 3'd2;
localparam [2:0] IRQ_DUART_LEVEL = 3'd3;
localparam [2:0] IRQ_HSYNC_LEVEL = 3'd4;
localparam [2:0] IRQ_NMI_LEVEL = 3'd7;

`endif
