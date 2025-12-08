#ifndef USER_H
#define USER_H

#include <stdint.h>
#include <stdarg.h>
// 从用户目录相对包含内核的系统调用号定义
#include "../kernel/syscall.h"

static inline uint64_t syscall0(uint64_t num)
{
  uint64_t ret;
  asm volatile(
    "mv a7, %1\n\t"
    "ecall\n\t"
    "mv %0, a0\n\t"
    : "=r"(ret)
    : "r"(num)
    : "a0","a7","memory"
  );
  return ret;
}

static inline uint64_t syscall1(uint64_t num, uint64_t arg0)
{
  uint64_t ret;
  asm volatile(
    "mv a0, %1\n\t"
    "mv a7, %2\n\t"
    "ecall\n\t"
    "mv %0, a0\n\t"
    : "=r"(ret)
    : "r"(arg0), "r"(num)
    : "a0","a7","memory"
  );
  return ret;
}

static inline uint64_t syscall2(uint64_t num, uint64_t arg0, uint64_t arg1)
{
  uint64_t ret;
  asm volatile(
    "mv a0, %1\n\t"
    "mv a1, %2\n\t"
    "mv a7, %3\n\t"
    "ecall\n\t"
    "mv %0, a0\n\t"
    : "=r"(ret)
    : "r"(arg0), "r"(arg1), "r"(num)
    : "a0","a1","a7","memory"
  );
  return ret;
}

static inline uint64_t syscall3(uint64_t num, uint64_t arg0, uint64_t arg1, uint64_t arg2)
{
  uint64_t ret;
  asm volatile(
    "mv a0, %1\n\t"
    "mv a1, %2\n\t"
    "mv a2, %3\n\t"
    "mv a7, %4\n\t"
    "ecall\n\t"
    "mv %0, a0\n\t"
    : "=r"(ret)
    : "r"(arg0), "r"(arg1), "r"(arg2), "r"(num)
    : "a0","a1","a2","a7","memory"
  );
  return ret;
}

// 用户态接口：返回自启动以来的时钟 tick 数
static inline uint64_t uptime(void)
{
  return syscall0(SYS_uptime);
}

// 用户态接口：设置进程优先级（用户态封装）
static inline int usys_setpriority(int pid, int value)
{
  return (int)syscall2(SYS_setpriority, (uint64_t)pid, (uint64_t)value);
}

// 用户态接口：查询进程优先级（用户态封装）
static inline int usys_getpriority(int pid)
{
  return (int)syscall2(SYS_getpriority, (uint64_t)pid, 0);
}
// 用户态接口：打印进程表（PID PRIORITY STATE TICKS）
static inline int usys_procdump(void)
{
  return (int)syscall0(SYS_procdump);
}
// ==== 基础用户态库函数（最小实现，避免与内核符号冲突）====
static inline int usys_write(int fd, const void *buf, int n)
{
  return (int)syscall3(SYS_write, (uint64_t)fd, (uint64_t)buf, (uint64_t)n);
}

// 进程控制类
static inline int usys_getpid(void)
{
  return (int)syscall0(SYS_getpid);
}

static inline int usys_wait(void)
{
  return (int)syscall0(SYS_wait);
}

static inline int usys_kill(int pid)
{
  return (int)syscall1(SYS_kill, (uint64_t)pid);
}

static inline int usys_fork(void)
{
  return (int)syscall0(SYS_fork);
}

// 保持兼容：原有 exit() 不带参数，默认 status=0
static inline void usys_exit(void)
{
  syscall1(SYS_exit, 0);
  for(;;) { }
}

// 可选：带退出码的 exit 版本
static inline void usys_exit_status(int status)
{
  syscall1(SYS_exit, (uint64_t)status);
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
static inline int usys_printf(int fd, const char *fmt, ...)
{
  if (!fmt) return -1;
  (void)usys_write(fd, "[user] ", 7);
  char buf[256];
  int bi = 0;
  const char *p = fmt;
  va_list ap;
  va_start(ap, fmt);
  #define FLUSH() do { if (bi > 0) { (void)usys_write(fd, buf, bi); bi = 0; } } while(0)
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
  (void)usys_write(fd, "", 0);
  #undef FLUSH
  #undef PUTC
  return 0;
}

// 文件操作类
static inline int usys_open(const char *path, int flags)
{
  return (int)syscall2(SYS_open, (uint64_t)path, (uint64_t)flags);
}

static inline int usys_close(int fd)
{
  return (int)syscall1(SYS_close, (uint64_t)fd);
}

static inline int usys_read(int fd, void *buf, int n)
{
  return (int)syscall3(SYS_read, (uint64_t)fd, (uint64_t)buf, (uint64_t)n);
}

// 内存管理类（当前内核不支持，返回 -1）
static inline void* usys_sbrk(int incr)
{
  return (void*)syscall1(SYS_sbrk, (uint64_t)incr);
}

#endif // USER_H
