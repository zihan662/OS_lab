// kernel/printf.c
#include "types.h"
#include "console.h"
#include "printf.h"

static char digits[] = "0123456789abcdef";
static void print_number(long num, int base, int sign) {
    char buf[32];
    int i = 0;
    unsigned int x;

    if (sign && num < 0) {
        x = -num;
        sign = 1;
    } else {
        x = num;
        sign = 0;
    }

    do {
        buf[i++] = digits[x % base];
        x /= base;
    } while (x != 0);

    if (sign) {
        buf[i++] = '-';
    }

    while (--i >= 0) {
        console_putc(buf[i]);
    }
}
int printf(const char *fmt, ...) {
    va_list ap;
    int i, c;
    char *s;

    // 检查是否为纯字符串（无 %）
    int has_percent = 0;
    for (i = 0; fmt[i]; i++) {
        if (fmt[i] == '%') {
            has_percent = 1;
            break;
        }
    }

    if (!has_percent) {
        console_puts((char*)fmt); // 直接输出纯字符串
        return 0;
    }
    va_start(ap, fmt);
    for (i = 0; (c = fmt[i] & 0xff) != 0; i++) {
        if (c != '%') {
            console_putc(c);
            continue;
        }
        c = fmt[++i] & 0xff;
        if (c == 0)
            break;
        switch (c) {
        case 'd':
            print_number(va_arg(ap, int), 10, 1);
            break;
        case 'x':
            print_number(va_arg(ap, int), 16, 0);
            break;
        case 's':
            s = va_arg(ap, char*);
            if (s == 0)
                s = "(null)";
            console_puts(s);
            break;
        case 'c':
            console_putc(va_arg(ap, int));
            break;
        case 'p':
            console_puts("0x");
            print_number(va_arg(ap, unsigned long), 16, 0);  // 使用 unsigned long 打印地址
            break;
        case '%':
            console_putc('%');
            break;
        default:
            console_putc('%');
            console_putc(c);
            break;
        }
    }
    va_end(ap);
    return 0;
}

static void sprintint(char *buf, int *i, int xx, int base, int sign) {
    char tmp[16];
    int j;
    unsigned int x;

    if (sign && xx < 0) {
        buf[(*i)++] = '-';
        x = -xx;
    } else {
        x = xx;
    }

    j = 0;
    do {
        tmp[j++] = digits[x % base];
    } while ((x /= base) != 0);

    while (--j >= 0)
        buf[(*i)++] = tmp[j];
}
void panic(char *s) {
  printf("panic: %s\n", s);
  for(;;);
}
int sprintf(char *buf, const char *fmt, ...) {
    va_list ap;
    int i = 0, c;
    char *s;

    va_start(ap, fmt);
    for (int j = 0; (c = fmt[j] & 0xff) != 0; j++) {
        if (c != '%') {
            buf[i++] = c;
            continue;
        }
        c = fmt[++j] & 0xff;
        if (c == 0)
            break;
        switch (c) {
        case 'd':
            sprintint(buf, &i, va_arg(ap, int), 10, 1);
            break;
        case 'x':
            sprintint(buf, &i, va_arg(ap, int), 16, 0);
            break;
        case 's':
            s = va_arg(ap, char*);
            if (s == 0)
                s = "(null)";
            while (*s)
                buf[i++] = *s++;
            break;
        case 'c':
            buf[i++] = va_arg(ap, int);
            break;
        case 'p':  // 添加 %p 支持
            buf[i++] = '0';
            buf[i++] = 'x';
            sprintint(buf, &i, va_arg(ap, unsigned long), 16, 0);
            break;
        case '%':
            buf[i++] = '%';
            break;
        default:
            buf[i++] = '%';
            buf[i++] = c;
            break;
        }
    }
    buf[i] = '\0';
    va_end(ap);
    return i;
}