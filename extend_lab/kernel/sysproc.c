#include "types.h"
#include "timer.h"
#include "console.h"
#include "proc.h"
#include "riscv.h"

// 内核侧系统调用实现：返回启动以来的时钟中断计数
uint64 uptime(void) {
  return timer_ticks();
}

// 简化版 write：忽略 fd，将用户缓冲区内容输出到控制台
int sys_write(int fd, const char *buf, int n) {
  (void)fd;
  if (!buf || n < 0) return -1;
  int state = intr_get();
  intr_off();
  for (int i = 0; i < n; i++) {
    console_putc(buf[i]);
  }
  intr_on(state);
  return n;
}

// 进程退出系统调用
void sys_exit(int status) {
  exit_process(status);
}