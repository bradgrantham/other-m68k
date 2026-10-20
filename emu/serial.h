/* Serial console back end: stdout/--serial-log for DUART channel A output,
 * stdin (raw mode when a tty) and --serial-type for channel A input,
 * --duart-b-log for channel B output. */
#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>

/* Open the log files (either may be NULL) and prepare stdin.  Returns 0 or -1. */
int  serial_init(const char *log_path, const char *b_log_path, int use_stdin);
void serial_shutdown(void);

/* A byte left the channel A / channel B transmitter. */
void serial_tx_byte(uint8_t b);
void serial_b_tx_byte(uint8_t b);

/* Next byte for the channel A receiver, or -1 if none is waiting.  Injected
 * text (--serial-type) comes first, then stdin. */
int  serial_rx_byte(void);
void serial_inject_text(const char *s);

void serial_flush(void);

#endif
