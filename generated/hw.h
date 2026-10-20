/* Generated from spec/board.json by tools/gen.py -- DO NOT EDIT; edit the spec and run `make gen`. */
#ifndef HW_H
#define HW_H
#include <stdint.h>

#define BOARD_NAME "other-m68k"
#define BOARD_REVISION 2
#define OSC_HZ 24000000UL
#define CPU_CLOCK_HZ 12000000UL
#define SLOT_CPU_CLOCKS 4

/* ---- memory regions ---- */
#define RAM_BASE 0x000000UL
#define RAM_SIZE 0x040000UL
#define RAM_END  (RAM_BASE + RAM_SIZE)
#define ROM_BASE 0xE00000UL
#define ROM_SIZE 0x020000UL
#define ROM_END  (ROM_BASE + ROM_SIZE)
#define IO_BASE 0xF00000UL
#define IO_SIZE 0x010000UL
#define IO_END  (IO_BASE + IO_SIZE)
#define SWITCHES_BASE 0xF10000UL
#define SWITCHES_SIZE 0x010000UL
#define SWITCHES_END  (SWITCHES_BASE + SWITCHES_SIZE)
#define DUART_BASE 0xF20000UL
#define DUART_SIZE 0x010000UL
#define DUART_END  (DUART_BASE + DUART_SIZE)

/* ---- RAM layout ---- */
#define VECTORS_BASE 0x000000UL
#define VECTORS_SIZE 0x000400UL
#define VECTORS_END  (VECTORS_BASE + VECTORS_SIZE)
#define SYSDATA_BASE 0x000400UL
#define SYSDATA_SIZE 0x003C00UL
#define SYSDATA_END  (SYSDATA_BASE + SYSDATA_SIZE)
#define USER_BASE 0x004000UL
#define USER_SIZE 0x016800UL
#define USER_END  (USER_BASE + USER_SIZE)
#define FB1_BASE 0x01A800UL
#define FB1_SIZE 0x012C00UL
#define FB1_END  (FB1_BASE + FB1_SIZE)
#define FB0_BASE 0x02D400UL
#define FB0_SIZE 0x012C00UL
#define FB0_END  (FB0_BASE + FB0_SIZE)
#define SYS_STACK_TOP SYSDATA_END
#define USER_STACK_TOP USER_END

/* ---- video ---- */
#define VIDEO_WIDTH 320
#define VIDEO_HEIGHT 240
#define VIDEO_BPP 8
#define VIDEO_BYTES_PER_LINE 320
#define VIDEO_PAGE_COUNT 2
#define FB_PAGE0_ADDR 0x02D400UL
#define FB_PAGE1_ADDR 0x01A800UL
#define FB_SIZE (VIDEO_BYTES_PER_LINE * VIDEO_HEIGHT)
#define RGB332(r, g, b) ((uint8_t)((((r) >> 5) << 5) | (((g) >> 5) << 2) | ((b) >> 6)))
#define VIDEO_NTSC_LINE_CLOCKS 764
#define VIDEO_NTSC_H_ACTIVE 640
#define VIDEO_NTSC_H_FRONT 16
#define VIDEO_NTSC_H_SYNC 56
#define VIDEO_NTSC_H_BACK 52
#define VIDEO_NTSC_LINES 262
#define VIDEO_NTSC_V_ACTIVE 240
#define VIDEO_NTSC_V_BOTTOM 3
#define VIDEO_NTSC_V_SYNC 3
#define VIDEO_NTSC_V_TOP 16
#define VIDEO_NTSC_FRAME_CLOCKS (764UL * 262UL)
#define VIDEO_PAL_LINE_CLOCKS 768
#define VIDEO_PAL_H_ACTIVE 640
#define VIDEO_PAL_H_FRONT 16
#define VIDEO_PAL_H_SYNC 56
#define VIDEO_PAL_H_BACK 56
#define VIDEO_PAL_LINES 312
#define VIDEO_PAL_V_ACTIVE 240
#define VIDEO_PAL_V_BOTTOM 30
#define VIDEO_PAL_V_SYNC 3
#define VIDEO_PAL_V_TOP 39
#define VIDEO_PAL_FRAME_CLOCKS (768UL * 312UL)

/* ---- registers ---- */
#define IO_BASE 0xF00000UL
#define IO_INTSTAT_OFFSET 0
#define IO_INTSTAT_ADDR (IO_BASE + 0)
#define IO_INTSTAT (*(volatile uint16_t *)IO_INTSTAT_ADDR)
#define INTSTAT_VBLANK_BIT 0
#define INTSTAT_VBLANK (1u << 0)
#define INTSTAT_HSYNC_BIT 1
#define INTSTAT_HSYNC (1u << 1)
#define INTSTAT_PS2RDY_BIT 2
#define INTSTAT_PS2RDY (1u << 2)
#define INTSTAT_DUARTIRQ_BIT 3
#define INTSTAT_DUARTIRQ (1u << 3)
#define INTSTAT_PS2ERR_BIT 5
#define INTSTAT_PS2ERR (1u << 5)
#define INTSTAT_NMIBTN_BIT 7
#define INTSTAT_NMIBTN (1u << 7)
#define INTSTAT_SDMISO_BIT 8
#define INTSTAT_SDMISO (1u << 8)
#define INTSTAT_PS2CLK_BIT 9
#define INTSTAT_PS2CLK (1u << 9)
#define INTSTAT_PS2DAT_BIT 10
#define INTSTAT_PS2DAT (1u << 10)
#define INTSTAT_PALJMP_BIT 12
#define INTSTAT_PALJMP (1u << 12)
#define INTSTAT_VSYNCPIN_BIT 13
#define INTSTAT_VSYNCPIN (1u << 13)
#define INTSTAT_DEPIN_BIT 14
#define INTSTAT_DEPIN (1u << 14)
#define INTSTAT_HSYNCPIN_BIT 15
#define INTSTAT_HSYNCPIN (1u << 15)
#define IO_INTEN_OFFSET 2
#define IO_INTEN_ADDR (IO_BASE + 2)
#define IO_INTEN (*(volatile uint16_t *)IO_INTEN_ADDR)
#define IO_INTEN_RESET 0x0000
#define INTEN_VBLANK_BIT 0
#define INTEN_VBLANK (1u << 0)
#define INTEN_HSYNC_BIT 1
#define INTEN_HSYNC (1u << 1)
#define INTEN_PS2_BIT 2
#define INTEN_PS2 (1u << 2)
#define INTEN_DUART_BIT 3
#define INTEN_DUART (1u << 3)
#define IO_SDIO_OFFSET 4
#define IO_SDIO_ADDR (IO_BASE + 4)
#define IO_SDIO (*(volatile uint16_t *)IO_SDIO_ADDR)
#define IO_SDIO_RESET 0x0003
#define SDIO_CS_BIT 0
#define SDIO_CS (1u << 0)
#define SDIO_CLK_BIT 1
#define SDIO_CLK (1u << 1)
#define SDIO_MOSI_BIT 2
#define SDIO_MOSI (1u << 2)
#define IO_GPIO_OFFSET 6
#define IO_GPIO_ADDR (IO_BASE + 6)
#define IO_GPIO (*(volatile uint16_t *)IO_GPIO_ADDR)
#define IO_GPIO_RESET 0x0003
#define GPIO_PS2CLK_BIT 0
#define GPIO_PS2CLK (1u << 0)
#define GPIO_PS2DAT_BIT 1
#define GPIO_PS2DAT (1u << 1)
#define GPIO_AUDIO_BIT 2
#define GPIO_AUDIO (1u << 2)
#define GPIO_LED0_BIT 3
#define GPIO_LED0 (1u << 3)
#define GPIO_LED1_BIT 4
#define GPIO_LED1 (1u << 4)
#define IO_PS2DATA_OFFSET 8
#define IO_PS2DATA_ADDR (IO_BASE + 8)
#define IO_PS2DATA (*(volatile uint16_t *)IO_PS2DATA_ADDR)
#define PS2DATA_DATA_BIT 0
#define PS2DATA_DATA_SHIFT 0
#define PS2DATA_DATA_WIDTH 8
#define PS2DATA_DATA_MASK 0x00FF
#define IO_USERIO_OFFSET 12
#define IO_USERIO_ADDR (IO_BASE + 12)
#define IO_USERIO (*(volatile uint16_t *)IO_USERIO_ADDR)
#define IO_USERIO_RESET 0x000F
#define USERIO_OUT_BIT 0
#define USERIO_OUT_SHIFT 0
#define USERIO_OUT_WIDTH 4
#define USERIO_OUT_MASK 0x000F
#define USERIO_IN_BIT 8
#define USERIO_IN_SHIFT 8
#define USERIO_IN_WIDTH 4
#define USERIO_IN_MASK 0x0F00
#define IO_ID_OFFSET 14
#define IO_ID_ADDR (IO_BASE + 14)
#define IO_ID (*(volatile uint16_t *)IO_ID_ADDR)
#define IO_ID_RESET 0x6802
#define ID_REV_BIT 0
#define ID_REV_SHIFT 0
#define ID_REV_WIDTH 8
#define ID_REV_MASK 0x00FF
#define ID_MAGIC_BIT 8
#define ID_MAGIC_SHIFT 8
#define ID_MAGIC_WIDTH 8
#define ID_MAGIC_MASK 0xFF00
#define SW_BASE 0xF10000UL
#define SW_PAGE0_OFFSET 0
#define SW_PAGE0_ADDR (SW_BASE + 0)
#define SW_PAGE0 (*(volatile uint16_t *)SW_PAGE0_ADDR)
#define SW_PAGE1_OFFSET 2
#define SW_PAGE1_ADDR (SW_BASE + 2)
#define SW_PAGE1 (*(volatile uint16_t *)SW_PAGE1_ADDR)
#define SW_NTSC_OFFSET 4
#define SW_NTSC_ADDR (SW_BASE + 4)
#define SW_NTSC (*(volatile uint16_t *)SW_NTSC_ADDR)
#define SW_PAL_OFFSET 6
#define SW_PAL_ADDR (SW_BASE + 6)
#define SW_PAL (*(volatile uint16_t *)SW_PAL_ADDR)
/* soft switches: writing any value performs the action */
#define SW_PAGE0_SET() do { SW_PAGE0 = 0; } while (0)
#define SW_PAGE1_SET() do { SW_PAGE1 = 0; } while (0)
#define SW_NTSC_SET() do { SW_NTSC = 0; } while (0)
#define SW_PAL_SET() do { SW_PAL = 0; } while (0)
#define DUART_BASE 0xF20000UL
#define DUART_MRA_OFFSET 1
#define DUART_MRA_ADDR (DUART_BASE + 1)
#define DUART_MRA (*(volatile uint8_t *)DUART_MRA_ADDR)
#define DUART_SRA_OFFSET 3
#define DUART_SRA_ADDR (DUART_BASE + 3)
#define DUART_SRA (*(volatile uint8_t *)DUART_SRA_ADDR)
#define SRA_RXRDY_BIT 0
#define SRA_RXRDY (1u << 0)
#define SRA_FFULL_BIT 1
#define SRA_FFULL (1u << 1)
#define SRA_TXRDY_BIT 2
#define SRA_TXRDY (1u << 2)
#define SRA_TXEMT_BIT 3
#define SRA_TXEMT (1u << 3)
#define SRA_OE_BIT 4
#define SRA_OE (1u << 4)
#define SRA_PE_BIT 5
#define SRA_PE (1u << 5)
#define SRA_FE_BIT 6
#define SRA_FE (1u << 6)
#define SRA_RB_BIT 7
#define SRA_RB (1u << 7)
#define DUART_CSRA_OFFSET 3
#define DUART_CSRA_ADDR (DUART_BASE + 3)
#define DUART_CSRA (*(volatile uint8_t *)DUART_CSRA_ADDR)
#define DUART_CRA_OFFSET 5
#define DUART_CRA_ADDR (DUART_BASE + 5)
#define DUART_CRA (*(volatile uint8_t *)DUART_CRA_ADDR)
#define DUART_RBA_OFFSET 7
#define DUART_RBA_ADDR (DUART_BASE + 7)
#define DUART_RBA (*(volatile uint8_t *)DUART_RBA_ADDR)
#define DUART_TBA_OFFSET 7
#define DUART_TBA_ADDR (DUART_BASE + 7)
#define DUART_TBA (*(volatile uint8_t *)DUART_TBA_ADDR)
#define DUART_IPCR_OFFSET 9
#define DUART_IPCR_ADDR (DUART_BASE + 9)
#define DUART_IPCR (*(volatile uint8_t *)DUART_IPCR_ADDR)
#define DUART_ACR_OFFSET 9
#define DUART_ACR_ADDR (DUART_BASE + 9)
#define DUART_ACR (*(volatile uint8_t *)DUART_ACR_ADDR)
#define DUART_ISR_OFFSET 11
#define DUART_ISR_ADDR (DUART_BASE + 11)
#define DUART_ISR (*(volatile uint8_t *)DUART_ISR_ADDR)
#define ISR_TXRDYA_BIT 0
#define ISR_TXRDYA (1u << 0)
#define ISR_RXRDYA_BIT 1
#define ISR_RXRDYA (1u << 1)
#define ISR_DBA_BIT 2
#define ISR_DBA (1u << 2)
#define ISR_CTR_BIT 3
#define ISR_CTR (1u << 3)
#define ISR_TXRDYB_BIT 4
#define ISR_TXRDYB (1u << 4)
#define ISR_RXRDYB_BIT 5
#define ISR_RXRDYB (1u << 5)
#define ISR_DBB_BIT 6
#define ISR_DBB (1u << 6)
#define ISR_IPC_BIT 7
#define ISR_IPC (1u << 7)
#define DUART_IMR_OFFSET 11
#define DUART_IMR_ADDR (DUART_BASE + 11)
#define DUART_IMR (*(volatile uint8_t *)DUART_IMR_ADDR)
#define IMR_TXRDYA_BIT 0
#define IMR_TXRDYA (1u << 0)
#define IMR_RXRDYA_BIT 1
#define IMR_RXRDYA (1u << 1)
#define IMR_DBA_BIT 2
#define IMR_DBA (1u << 2)
#define IMR_CTR_BIT 3
#define IMR_CTR (1u << 3)
#define IMR_TXRDYB_BIT 4
#define IMR_TXRDYB (1u << 4)
#define IMR_RXRDYB_BIT 5
#define IMR_RXRDYB (1u << 5)
#define IMR_DBB_BIT 6
#define IMR_DBB (1u << 6)
#define IMR_IPC_BIT 7
#define IMR_IPC (1u << 7)
#define DUART_CUR_OFFSET 13
#define DUART_CUR_ADDR (DUART_BASE + 13)
#define DUART_CUR (*(volatile uint8_t *)DUART_CUR_ADDR)
#define DUART_CTUR_OFFSET 13
#define DUART_CTUR_ADDR (DUART_BASE + 13)
#define DUART_CTUR (*(volatile uint8_t *)DUART_CTUR_ADDR)
#define DUART_CLR_OFFSET 15
#define DUART_CLR_ADDR (DUART_BASE + 15)
#define DUART_CLR (*(volatile uint8_t *)DUART_CLR_ADDR)
#define DUART_CTLR_OFFSET 15
#define DUART_CTLR_ADDR (DUART_BASE + 15)
#define DUART_CTLR (*(volatile uint8_t *)DUART_CTLR_ADDR)
#define DUART_MRB_OFFSET 17
#define DUART_MRB_ADDR (DUART_BASE + 17)
#define DUART_MRB (*(volatile uint8_t *)DUART_MRB_ADDR)
#define DUART_SRB_OFFSET 19
#define DUART_SRB_ADDR (DUART_BASE + 19)
#define DUART_SRB (*(volatile uint8_t *)DUART_SRB_ADDR)
#define SRB_RXRDY_BIT 0
#define SRB_RXRDY (1u << 0)
#define SRB_FFULL_BIT 1
#define SRB_FFULL (1u << 1)
#define SRB_TXRDY_BIT 2
#define SRB_TXRDY (1u << 2)
#define SRB_TXEMT_BIT 3
#define SRB_TXEMT (1u << 3)
#define SRB_OE_BIT 4
#define SRB_OE (1u << 4)
#define SRB_PE_BIT 5
#define SRB_PE (1u << 5)
#define SRB_FE_BIT 6
#define SRB_FE (1u << 6)
#define SRB_RB_BIT 7
#define SRB_RB (1u << 7)
#define DUART_CSRB_OFFSET 19
#define DUART_CSRB_ADDR (DUART_BASE + 19)
#define DUART_CSRB (*(volatile uint8_t *)DUART_CSRB_ADDR)
#define DUART_CRB_OFFSET 21
#define DUART_CRB_ADDR (DUART_BASE + 21)
#define DUART_CRB (*(volatile uint8_t *)DUART_CRB_ADDR)
#define DUART_RBB_OFFSET 23
#define DUART_RBB_ADDR (DUART_BASE + 23)
#define DUART_RBB (*(volatile uint8_t *)DUART_RBB_ADDR)
#define DUART_TBB_OFFSET 23
#define DUART_TBB_ADDR (DUART_BASE + 23)
#define DUART_TBB (*(volatile uint8_t *)DUART_TBB_ADDR)
#define DUART_IVR_OFFSET 25
#define DUART_IVR_ADDR (DUART_BASE + 25)
#define DUART_IVR (*(volatile uint8_t *)DUART_IVR_ADDR)
#define DUART_IP_OFFSET 27
#define DUART_IP_ADDR (DUART_BASE + 27)
#define DUART_IP (*(volatile uint8_t *)DUART_IP_ADDR)
#define DUART_OPCR_OFFSET 27
#define DUART_OPCR_ADDR (DUART_BASE + 27)
#define DUART_OPCR (*(volatile uint8_t *)DUART_OPCR_ADDR)
#define DUART_START_CTR_OFFSET 29
#define DUART_START_CTR_ADDR (DUART_BASE + 29)
#define DUART_START_CTR (*(volatile uint8_t *)DUART_START_CTR_ADDR)
#define DUART_OPR_SET_OFFSET 29
#define DUART_OPR_SET_ADDR (DUART_BASE + 29)
#define DUART_OPR_SET (*(volatile uint8_t *)DUART_OPR_SET_ADDR)
#define DUART_STOP_CTR_OFFSET 31
#define DUART_STOP_CTR_ADDR (DUART_BASE + 31)
#define DUART_STOP_CTR (*(volatile uint8_t *)DUART_STOP_CTR_ADDR)
#define DUART_OPR_RESET_OFFSET 31
#define DUART_OPR_RESET_ADDR (DUART_BASE + 31)
#define DUART_OPR_RESET (*(volatile uint8_t *)DUART_OPR_RESET_ADDR)

/* ---- interrupts (autovectored) ---- */
#define IRQ_VBLANK_LEVEL 1
#define IRQ_VBLANK_VECTOR 25
#define IRQ_VBLANK_VECTOR_ADDR 0x064
#define IRQ_PS2_LEVEL 2
#define IRQ_PS2_VECTOR 26
#define IRQ_PS2_VECTOR_ADDR 0x068
#define IRQ_DUART_LEVEL 3
#define IRQ_DUART_VECTOR 27
#define IRQ_DUART_VECTOR_ADDR 0x06C
#define IRQ_HSYNC_LEVEL 4
#define IRQ_HSYNC_VECTOR 28
#define IRQ_HSYNC_VECTOR_ADDR 0x070
#define IRQ_NMI_LEVEL 7
#define IRQ_NMI_VECTOR 31
#define IRQ_NMI_VECTOR_ADDR 0x07C

/* ---- syscall ABI ---- */
#define SYSCALL_TRAP 15
#define SYSCALL_VECTOR 47

/* ---- serial (68681 channel A) ---- */
#define SERIAL_BAUD 38400
#define DUART_XTAL_HZ 3686400UL
#define SERIAL_CSR_VALUE 0xCC   /* CSRA: RX and TX baud code */
#define SERIAL_ACR_BRG_SET 1  /* ACR[7] */

#endif /* HW_H */
