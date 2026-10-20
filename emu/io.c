/* CPLD2 registers, interrupt controller, soft switches.
 *
 * Registers are 16 bits at IO_BASE + 2n (n = A1..A3, mirrored through the
 * region).  CPLD2 has no UDS/LDS, so byte accesses are undefined on the real
 * board; here a byte read returns its half of the word and a byte write
 * updates only that half (the least surprising choice for software).
 */
#include <stdio.h>
#include <string.h>

#include "hw.h"
#include "emu_map.h"
#include "m68k.h"
#include "emu.h"
#include "io.h"
#include "video.h"
#include "serial.h"
#include "sdcard.h"
#include "audio.h"

static uint16_t flags;          /* INTSTAT bits 0..6 that are flags */
static uint16_t inten;
static uint16_t sdio;
static uint16_t gpio;
static uint16_t ps2data;
static uint16_t serdata;
static uint16_t userio;
static int nmi_held;
static int cur_ipl = -1;
static int pending_page;
static int pending_pal;

#define FLAG_MASK (INTSTAT_VBLANK | INTSTAT_HSYNC | INTSTAT_PS2RDY | INTSTAT_SERRX | \
                   INTSTAT_PS2ERR | INTSTAT_SERRXOVR)
#define W1C_MASK  (INTSTAT_VBLANK | INTSTAT_HSYNC | INTSTAT_PS2ERR | INTSTAT_SERRXOVR)
#define INTEN_MASK (INTEN_VBLANK | INTEN_HSYNC | INTEN_PS2 | INTEN_SERRX)
#define SDIO_OUT_MASK (SDIO_CS | SDIO_CLK | SDIO_MOSI)
#define GPIO_MASK (GPIO_PS2CLK | GPIO_PS2DAT | GPIO_SERTX | GPIO_AUDIO | GPIO_LED0 | GPIO_LED1)

static void io_trace(const char *what, uint32_t offset, uint16_t val)
{
    fprintf(stderr, "m68k-emu: io: %10llu PC=%06X %s %-7s %04X\n", (unsigned long long)emu_cycles(),
            m68k_get_reg(NULL, M68K_REG_PPC) & 0xFFFFFF, what,
            emu_io_reg_names[(offset & 0xE) >> 1], val);
}

void io_reset(void)
{
    flags = 0;
    inten = IO_INTEN_RESET;
    sdio = IO_SDIO_RESET;
    gpio = IO_GPIO_RESET;
    ps2data = 0;
    serdata = 0;
    userio = IO_USERIO_RESET;
    pending_page = 0;
    pending_pal = 0;
    cur_ipl = -1;
    sd_pins(sdio & SDIO_CS, (sdio & SDIO_CLK) != 0, (sdio & SDIO_MOSI) != 0);
    io_update_ipl();
}

/* ---- interrupt controller ---------------------------------------------- */

int io_current_ipl(void)
{
    int level = 0;
    if ((flags & INTSTAT_VBLANK) && (inten & INTEN_VBLANK) && level < IRQ_VBLANK_LEVEL)
        level = IRQ_VBLANK_LEVEL;
    if ((flags & INTSTAT_PS2RDY) && (inten & INTEN_PS2) && level < IRQ_PS2_LEVEL)
        level = IRQ_PS2_LEVEL;
    if ((flags & INTSTAT_SERRX) && (inten & INTEN_SERRX) && level < IRQ_SERRX_LEVEL)
        level = IRQ_SERRX_LEVEL;
    if ((flags & INTSTAT_HSYNC) && (inten & INTEN_HSYNC) && level < IRQ_HSYNC_LEVEL)
        level = IRQ_HSYNC_LEVEL;
    if (nmi_held)
        level = IRQ_NMI_LEVEL;
    return level;
}

void io_update_ipl(void)
{
    int level = io_current_ipl();
    if (level != cur_ipl) {
        cur_ipl = level;
        m68k_set_irq((unsigned)level);
        /* Musashi only samples IPL at m68k_execute() entry and on SR writes;
         * end the slice so the change is seen after this instruction. */
        m68k_end_timeslice();
    }
}

void io_set_flag(uint16_t mask)
{
    flags |= mask & FLAG_MASK;
    io_update_ipl();
}

int io_flag_set(uint16_t mask)
{
    return (flags & mask) != 0;
}

void io_set_nmi(int held)
{
    nmi_held = held;
    io_update_ipl();
}

void io_ps2_deliver(uint8_t byte, int frame_error)
{
    ps2data = byte;
    flags |= INTSTAT_PS2RDY;
    if (frame_error)
        flags |= INTSTAT_PS2ERR;
    io_update_ipl();
}

int io_ps2_ready(void)
{
    return (flags & INTSTAT_PS2RDY) != 0;
}

void io_serial_deliver(uint8_t byte)
{
    if (opts.io_trace)
        fprintf(stderr, "m68k-emu: io: %10llu serial RX byte %02X%s\n", (unsigned long long)emu_cycles(),
                byte, (flags & INTSTAT_SERRX) ? " (overrun)" : "");
    if (flags & INTSTAT_SERRX)
        flags |= INTSTAT_SERRXOVR;
    serdata = byte;
    flags |= INTSTAT_SERRX;
    io_update_ipl();
}

uint16_t io_gpio(void)
{
    return gpio;
}

int io_pending_page(void)
{
    return pending_page;
}

int io_pending_pal(void)
{
    return pending_pal;
}

/* ---- registers ---------------------------------------------------------- */

static uint16_t intstat_value(void)
{
    uint64_t now = emu_cycles();
    uint16_t v = flags;
    if (serial_hw_busy(now))
        v |= INTSTAT_SERTXBUSY;
    if (nmi_held)
        v |= INTSTAT_NMIBTN;
    if (sd_miso())
        v |= INTSTAT_SDMISO;
    /* PS/2 lines are open-drain with pull-ups: high unless the GPIO drives
     * them low (the keyboard is idle between frames). */
    if (gpio & GPIO_PS2CLK)
        v |= INTSTAT_PS2CLK;
    if (gpio & GPIO_PS2DAT)
        v |= INTSTAT_PS2DAT;
    v |= INTSTAT_SERRXPIN;          /* idle high */
    if (opts.pal_jumper)
        v |= INTSTAT_PALJMP;
    v |= video_live_pins();
    return v;
}

static uint16_t sdio_value(void)
{
    return (uint16_t)((sdio & SDIO_OUT_MASK) | (sd_miso() ? SDIO_MISO : 0));
}

static uint16_t userio_value(void)
{
    /* Nothing external drives the header pins: they read back the drive. */
    return (uint16_t)((userio & USERIO_OUT_MASK) | ((userio & USERIO_OUT_MASK) << USERIO_IN_SHIFT));
}

uint16_t io_peek16(uint32_t offset)
{
    switch (offset & 0xE) {
    case IO_INTSTAT_OFFSET: return intstat_value();
    case IO_INTEN_OFFSET:   return inten;
    case IO_SDIO_OFFSET:    return sdio_value();
    case IO_GPIO_OFFSET:    return gpio;
    case IO_PS2DATA_OFFSET: return ps2data;
    case IO_SERDATA_OFFSET: return serdata;
    case IO_USERIO_OFFSET:  return userio_value();
    case IO_ID_OFFSET:      return IO_ID_RESET;
    }
    return 0xFFFF;
}

uint16_t io_read16(uint32_t offset, int lanes)
{
    (void)lanes;    /* a byte read still performs the full register read */
    uint16_t v = io_peek16(offset);
    if (opts.io_trace)
        io_trace("rd", offset, v);
    switch (offset & 0xE) {
    case IO_PS2DATA_OFFSET:
        if (flags & INTSTAT_PS2RDY) {
            flags &= (uint16_t)~INTSTAT_PS2RDY;
            io_update_ipl();
        }
        break;
    case IO_SERDATA_OFFSET:
        if (flags & INTSTAT_SERRX) {
            flags &= (uint16_t)~INTSTAT_SERRX;
            io_update_ipl();
        }
        break;
    default:
        break;
    }
    return v;
}

static uint16_t lane_merge(uint16_t old, uint16_t val, int lanes)
{
    uint16_t mask = (uint16_t)(((lanes & 2) ? 0xFF00 : 0) | ((lanes & 1) ? 0x00FF : 0));
    return (uint16_t)((old & ~mask) | (val & mask));
}

static void gpio_write(uint16_t val)
{
    uint16_t old = gpio;
    gpio = val & GPIO_MASK;
    uint16_t changed = old ^ gpio;
    if (changed & GPIO_SERTX)
        serial_tx_edge(emu_cycles(), (gpio & GPIO_SERTX) != 0);
    if (changed & GPIO_AUDIO)
        audio_edge(emu_cycles(), (gpio & GPIO_AUDIO) != 0);
}

void io_write16(uint32_t offset, uint16_t val, int lanes)
{
    if (opts.io_trace)
        io_trace(lanes == 3 ? "wr" : (lanes == 2 ? "wrU" : "wrL"), offset, val);
    switch (offset & 0xE) {
    case IO_INTSTAT_OFFSET:
        val = lane_merge(0, val, lanes);
        if (val & W1C_MASK) {
            flags &= (uint16_t)~(val & W1C_MASK);
            io_update_ipl();
        }
        break;
    case IO_INTEN_OFFSET:
        inten = lane_merge(inten, val, lanes) & INTEN_MASK;
        io_update_ipl();
        break;
    case IO_SDIO_OFFSET:
        sdio = lane_merge(sdio, val, lanes) & SDIO_OUT_MASK;
        sd_pins((sdio & SDIO_CS) != 0, (sdio & SDIO_CLK) != 0, (sdio & SDIO_MOSI) != 0);
        break;
    case IO_GPIO_OFFSET:
        gpio_write(lane_merge(gpio, val, lanes));
        break;
    case IO_PS2DATA_OFFSET:
        break;                      /* read only */
    case IO_SERDATA_OFFSET:
        if (opts.hw_serial_tx && (lanes & 1))
            serial_hw_tx((uint8_t)val, emu_cycles());
        break;
    case IO_USERIO_OFFSET:
        userio = lane_merge(userio, val, lanes) & USERIO_OUT_MASK;
        break;
    case IO_ID_OFFSET:
        break;                      /* read only */
    }
}

void io_switch_write(uint32_t offset)
{
    if (opts.io_trace)
        fprintf(stderr, "m68k-emu: io: %10llu PC=%06X switch %s\n", (unsigned long long)emu_cycles(),
                m68k_get_reg(NULL, M68K_REG_PPC) & 0xFFFFFF, emu_switches_reg_names[(offset & 6) >> 1]);
    switch (offset & 6) {
    case SW_PAGE0_OFFSET: pending_page = 0; break;
    case SW_PAGE1_OFFSET: pending_page = 1; break;
    case SW_NTSC_OFFSET:  pending_pal = 0;  break;
    case SW_PAL_OFFSET:   pending_pal = 1;  break;
    }
}
