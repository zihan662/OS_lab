#include "console.h"
#include "printf.h"
#include <stdarg.h>
#define BACKSPACE 0x100

extern void uart_putc(char c);
void console_putc(char c) {
    if (c == BACKSPACE) {
        uart_putc('\b');
        uart_putc(' ');
        uart_putc('\b');
    } else {
        uart_putc(c);
    }
}
void console_puts(const char *s) {
    while (*s) {
        console_putc(*s);
        s++;
    }
}
static char digits[] = "0123456789abcdef";
static void print_number(int num, int base, int sign) {
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

void clear_screen(void) {
    console_puts("\033[2J");  // 清除整个屏幕
    console_puts("\033[H");   // 光标回到左上角
}
void goto_xy(int x, int y) {
    char buf[16];
    sprintf(buf, "\033[%d;%dH", y, x);  // ANSI 格式: \033[y;xH
    console_puts(buf);
}

void clear_line(void) {
    console_puts("\033[K");  // 清除光标到行末
}
// 辅助函数: vprintf 实现可变参数格式化
static int vprintf(const char *fmt, va_list ap) {
    int i, c;
    char *s;

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
        case '%':
            console_putc('%');
            break;
        default:
            console_putc('%');
            console_putc(c);
            break;
        }
    }
    return 0;
}
int printf_color(int color, const char *fmt, ...) {
    va_list ap;
    char buf[16];

    // 设置前景色: \033[3xm, x 是颜色代码 (30-37)
    if (color >= COLOR_BLACK && color <= COLOR_WHITE) {
        sprintf(buf, "\033[%dm", color + 30);
        console_puts(buf);
    }

    // 格式化输出
    va_start(ap, fmt);
    int ret = vprintf(fmt, ap);  // 使用 vprintf 处理可变参数
    va_end(ap);

    // 重置颜色: \033[0m
    console_puts("\033[0m");

    return ret;
}
