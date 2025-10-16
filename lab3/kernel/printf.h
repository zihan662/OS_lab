// kernel/printf.h
#ifndef PRINTF_H
#define PRINTF_H

#include <stdarg.h>

int printf(const char *fmt, ...);
int sprintf(char *buf, const char *fmt, ...);
void panic(char *s);
#endif