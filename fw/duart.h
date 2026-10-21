/* MC68681 DUART mode/command values used by the BIOS. The register addresses, bit
 * positions and all values below come from generated/hw.h / hw.inc (spec/board.json
 * `serial`); this header only adds the names the BIOS code uses.  Data sheet refs: 4.2.3
 * (MR1), 4.2.4 (MR2), 4.2.7 (CR), 4.2.8 (ACR). */
#ifndef DUART_H
#define DUART_H

#define DUART_MR1_8N           SERIAL_MR1_VALUE   /* 8 bits/char, no parity, RxRDY interrupt */
#define DUART_MR2_1STOP        SERIAL_MR2_VALUE   /* one stop bit, normal mode, no CTS/RTS */
#define DUART_ACR_VALUE        SERIAL_ACR_VALUE   /* 0x00: baud-rate set 1, counter/timer unused */
/* DUART_CR_RESET_MR/RX/TX/ERR/BRK and DUART_CR_ENABLE come from hw.h / hw.inc */

#endif
