#ifndef SYSCALL_H
#define SYSCALL_H

// 简易系统调用号定义（RISC-V：a7 传递号）
#define SYS_setpriority  1
#define SYS_getpriority  2
#define SYS_uptime       3
#define SYS_exit         4
#define SYS_write        5
#define SYS_procdump     6

#endif // SYSCALL_H