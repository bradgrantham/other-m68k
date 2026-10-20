/* Small freestanding printf: %c %s %d %i %u %x %X %p %%, flags '-' '0', width, 'l'/'h'
 * length modifiers (int and long are both 32-bit on the target). No floating point. */
#include "bios.h"

struct snctx { char *buf; size_t size; size_t pos; };

static void sn_out(void *vctx, char c)
{
    struct snctx *ctx = vctx;
    if (ctx->pos + 1 < ctx->size)
        ctx->buf[ctx->pos] = c;
    ctx->pos++;
}

static int emit_padded(fmt_out_fn out, void *ctx, const char *s, int len, int width,
                       int left, int zero, int neg_sign)
{
    int n = 0;
    int pad = width - len - (neg_sign ? 1 : 0);
    if (!left && !zero)
        while (pad-- > 0) { out(ctx, ' '); n++; }
    if (neg_sign) { out(ctx, '-'); n++; }
    if (!left && zero)
        while (pad-- > 0) { out(ctx, '0'); n++; }
    while (len-- > 0) { out(ctx, *s++); n++; }
    if (left)
        while (pad-- > 0) { out(ctx, ' '); n++; }
    return n;
}

int kvformat(fmt_out_fn out, void *ctx, const char *fmt, va_list ap)
{
    int n = 0;
    char tmp[12];

    while (*fmt) {
        char c = *fmt++;
        int left = 0, zero = 0, width = 0, is_long = 0;
        if (c != '%') { out(ctx, c); n++; continue; }
        for (;;) {
            if (*fmt == '-') { left = 1; fmt++; }
            else if (*fmt == '0') { zero = 1; fmt++; }
            else break;
        }
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
        while (*fmt == 'l' || *fmt == 'h') { if (*fmt == 'l') is_long = 1; fmt++; }
        c = *fmt++;
        switch (c) {
        case '%': out(ctx, '%'); n++; break;
        case 'c': tmp[0] = (char)va_arg(ap, int);
                  n += emit_padded(out, ctx, tmp, 1, width, left, 0, 0); break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            n += emit_padded(out, ctx, s, (int)strlen(s), width, left, 0, 0);
            break;
        }
        case 'd': case 'i': case 'u': case 'x': case 'X': case 'p': {
            unsigned long v;
            int neg = 0, i = sizeof tmp;
            unsigned base = (c == 'x' || c == 'X' || c == 'p') ? 16 : 10;
            const char *digits = (c == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
            if (c == 'p') { v = (unsigned long)(uintptr_t)va_arg(ap, void *); zero = 1; width = 8; }
            else if (c == 'd' || c == 'i') {
                long sv = is_long ? va_arg(ap, long) : va_arg(ap, int);
                if (sv < 0) { neg = 1; v = (unsigned long)(-(sv + 1)) + 1; } else v = (unsigned long)sv;
            } else v = is_long ? va_arg(ap, unsigned long) : va_arg(ap, unsigned);
            do { tmp[--i] = digits[v % base]; v /= base; } while (v && i > 0);
            n += emit_padded(out, ctx, tmp + i, (int)sizeof tmp - i, width, left, zero, neg);
            break;
        }
        case 0: return n;
        default: out(ctx, '%'); out(ctx, c); n += 2; break;
        }
    }
    return n;
}

int kvsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    struct snctx ctx = { buf, size, 0 };
    int n = kvformat(sn_out, &ctx, fmt, ap);
    if (size) buf[ctx.pos < size ? ctx.pos : size - 1] = 0;
    return n;
}

int ksnprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = kvsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

#ifndef HOST_TEST
static void con_out(void *ctx, char c) { (void)ctx; putchar_both(c); }

int kprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = kvformat(con_out, 0, fmt, ap);
    va_end(ap);
    return n;
}
#endif
