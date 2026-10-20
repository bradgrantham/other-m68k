/* Serial port: bit-banged TX decoder, optional hardware TX, stdin RX. */
#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>

/* Open the log file (may be NULL) and prepare stdin.  Returns 0 or -1. */
int  serial_init(const char *log_path, int use_stdin);
void serial_shutdown(void);
void serial_reset(void);

/* GPIO.SERTX changed to `level` at `cycle`. */
void serial_tx_edge(uint64_t cycle, int level);

/* Decode any complete frames whose stop bit lies before `now`.  Called at the
 * end of each scanline; serial_flush() assumes the line idles high. */
void serial_tx_decode(uint64_t now);
void serial_flush(void);

/* Hardware transmitter (--hw-serial-tx): SERDATA write. */
void serial_hw_tx(uint8_t byte, uint64_t now);
int  serial_hw_busy(uint64_t now);

/* Poll stdin and deliver at most one byte to SERDATA (rate limited). */
void serial_rx_poll(uint64_t now);

/* Statistics. */
extern unsigned long serial_tx_bytes;
extern unsigned long serial_tx_framing_errors;

#endif
