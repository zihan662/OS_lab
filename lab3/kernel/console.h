// kernel/console.h
#ifndef CONSOLE_H
#define CONSOLE_H

#define COLOR_BLACK   0
#define COLOR_RED     1
#define COLOR_GREEN   2
#define COLOR_YELLOW  3
#define COLOR_BLUE    4
#define COLOR_MAGENTA 5
#define COLOR_CYAN    6
#define COLOR_WHITE   7

void console_putc(char c);
void console_puts(const char *s);
void clear_screen(void);
void goto_xy(int x, int y);
void clear_line(void);
int printf_color(int color, const char *fmt, ...);

#endif