#ifndef USER_H
#define USER_H

#include <stdint.h>
#include <stdarg.h>
// 从用户目录相对包含内核的系统调用号定义
#include "../kernel/syscall.h"

static inline uint64_t syscall0(uint64_t num)
{
  register uint64_t a7 asm("a7") = num;
  register uint64_t a0 asm("a0");
  asm volatile("ecall" : "=r"(a0) : "r"(a7) : "memory");
  return a0;
}

static inline uint64_t syscall1(uint64_t num, uint64_t arg0)
{
  register uint64_t a0 asm("a0") = arg0;
  register uint64_t a7 asm("a7") = num;
  asm volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");
  return a0;
}

static inline uint64_t syscall2(uint64_t num, uint64_t arg0, uint64_t arg1)
{
  register uint64_t a0 asm("a0") = arg0;
  register uint64_t a1 asm("a1") = arg1;
  register uint64_t a7 asm("a7") = num;
  asm volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a7) : "memory");
  return a0;
}

static inline uint64_t syscall3(uint64_t num, uint64_t arg0, uint64_t arg1, uint64_t arg2)
{
  register uint64_t a0 asm("a0") = arg0;
  register uint64_t a1 asm("a1") = arg1;
  register uint64_t a2 asm("a2") = arg2;
  register uint64_t a7 asm("a7") = num;
  asm volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a7) : "memory");
  return a0;
}

// 用户态接口：返回自启动以来的时钟 tick 数
static inline uint64_t uptime(void)
{
  return syscall0(SYS_uptime);
}

// 用户态接口：设置进程优先级
static inline int setpriority(int pid, int value)
{
  return (int)syscall2(SYS_setpriority, (uint64_t)pid, (uint64_t)value);
}

// 用户态接口：查询进程优先级
static inline int getpriority(int pid)
{
  return (int)syscall2(SYS_getpriority, (uint64_t)pid, 0);
}
// 用户态接口：打印进程表（PID PRIORITY STATE TICKS）
static inline int procdump(void)
{
  return (int)syscall0(SYS_procdump);
}
// ==== 基础用户态库函数（最小实现）====
static inline int write(int fd, const void *buf, int n)
{
  return (int)syscall3(SYS_write, (uint64_t)fd, (uint64_t)buf, (uint64_t)n);
}

static inline void exit(void)
{
  syscall1(SYS_exit, 0);
  for(;;) { }
}

static inline int atoi(const char *s)
{
  if (!s) return 0;
  int sign = 1;
  if (*s == '-') { sign = -1; s++; }
  int v = 0;
  while (*s >= '0' && *s <= '9') { v = v*10 + (*s - '0'); s++; }
  return sign * v;
}

// 简易 printf，仅支持 %s/%d/%c 以及普通文本，输出到 fd
static inline int printf(int fd, const char *fmt, ...)
{
  if (!fmt) return -1;
  char buf[256];
  int bi = 0;
  const char *p = fmt;
  va_list ap;
  va_start(ap, fmt);
  #define FLUSH() do { if (bi > 0) { (void)write(fd, buf, bi); bi = 0; } } while(0)
  #define PUTC(ch) do { char _c = (char)(ch); if (bi < (int)sizeof(buf)) buf[bi++] = _c; else { FLUSH(); buf[bi++] = _c; } } while(0)
  while (*p) {
    if (*p == '%') {
      p++;
      if (*p == 'd') {
        long v = (long)va_arg(ap, int);
        if (v == 0) { PUTC('0'); p++; continue; }
        if (v < 0) { PUTC('-'); v = -v; }
        char tmp[32]; int i = 0;
        while (v && i < 31) { tmp[i++] = (char)('0' + (v % 10)); v /= 10; }
        while (i--) PUTC(tmp[i]);
        p++;
      } else if (*p == 's') {
        const char *s = va_arg(ap, const char*);
        if (!s) s = "(null)";
        while (*s) PUTC(*s++);
        p++;
      } else if (*p == 'c') {
        int c = va_arg(ap, int);
        PUTC((char)c);
        p++;
      } else if (*p == '%') {
        PUTC('%');
        p++;
      } else {
        PUTC('%');
        if (*p) PUTC(*p++);
      }
    } else {
      PUTC(*p++);
    }
  }
  va_end(ap);
  FLUSH();
  #undef FLUSH
  #undef PUTC
  return 0;
}

#endif // USER_H