/* MC68681 DUART mode/command values used by the BIOS. Plain #defines so that both C
 * (via bios.h) and the preprocessed .S files (after hw.inc) can use them; the register
 * addresses and bit positions come from generated/hw.h / hw.inc. Values from the
 * MC68681 data sheet, section 4.2 (register descriptions), and spec/board.json `serial`.
 */
#ifndef DUART_H
#define DUART_H

/* CRA/CRB miscellaneous commands, CR[6:4] (data sheet 4.2.7.2) */
#define DUART_CR_RESET_MR      0x10    /* reset mode register pointer to MR1 */
#define DUART_CR_RESET_RX      0x20    /* reset receiver (disables it, clears RxRDY/FFULL, FIFO) */
#define DUART_CR_RESET_TX      0x30    /* reset transmitter (disables it, clears TxRDY/TxEMT) */
#define DUART_CR_RESET_ERR     0x40    /* reset error status: clears SR RB/PE/FE/OE */
#define DUART_CR_RESET_BRK     0x50    /* reset break-change interrupt */
/* CR[3:2] transmitter and CR[1:0] receiver enables (4.2.7.3 / 4.2.7.4) */
#define DUART_CR_TX_ENABLE     0x04
#define DUART_CR_RX_ENABLE     0x01
#define DUART_CR_ENABLE        (DUART_CR_TX_ENABLE + DUART_CR_RX_ENABLE)   /* 0x05 */

/* MR1: [1:0]=11 8 bits/char, [4:3]=10 no parity, no RxRTS, RxRDY interrupt (4.2.3) */
#define DUART_MR1_8N           0x13
/* MR2: [3:0]=0111 one stop bit, normal mode, no CTS/RTS (4.2.4) */
#define DUART_MR2_1STOP        0x07
/* ACR: [7] baud-rate set (SERIAL_ACR_BRG_SET from the spec), counter/timer unused,
 * no input-port change interrupts (4.2.8) */
#define DUART_ACR_VALUE        (SERIAL_ACR_BRG_SET << 7)

#endif
