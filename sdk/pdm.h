/* pdm.h: turn unsigned 8-bit samples into PDM pattern bytes for the AUDIO shifter.
 *
 * The board plays one pattern byte per video line (15707 Hz NTSC, 15625 PAL); the eight
 * bits leave the pin at 8x that rate and the RC filter averages them, so the fraction of
 * 1 bits is the output level. pdm_encode is a first-order sigma-delta modulator: an 8-bit
 * accumulator carried from sample to sample (and from call to call, so buffer boundaries
 * do not click) adds 2 * (sample >> 1) eight times per sample and each carry out becomes a
 * bit, most significant first. Sample 128 is the silence level and encodes to 0x55/0xAA.
 *
 * pdm_init builds the 64 KB table pdm_table[(acc << 7) | (sample >> 1)] =
 * (new_acc << 8) | pattern in .bss (about 0.4 s at 12 MHz; the time is printed by
 * examples/tune.c). pdm_encode then costs about 83 clocks per sample in C
 * (load, or, shift, index, two stores, mask), so with the 242-clock line interrupt
 * a program keeps roughly 450 of the 764 clocks per sample for synthesis.
 *
 * Typical use with the BIOS stream (see examples/tune.c):
 *     pdm_init();
 *     uint8_t *b = sys_audio_start(256);            // 16 ms per buffer
 *     while (more) { make_samples(tmp, 256); pdm_encode(b, tmp, 256); b = sys_audio_next(b, 1); }
 *     sys_audio_last(NULL, 1);
 */
#ifndef PDM_H
#define PDM_H
#include <stdint.h>

extern uint16_t pdm_table[32768];

void    pdm_init(void);                                           /* build the table; call once */
void    pdm_reset(void);                                          /* accumulator back to 0 */
void    pdm_encode(uint8_t *dst, const uint8_t *samples, unsigned n);
uint8_t pdm_silence(void);                                        /* the idle pattern, 0xAA */

#endif
