#ifndef SYSCALL_H
#define SYSCALL_H

// 简易系统调用号定义（RISC-V：a7 传递号）
#define SYS_setpriority  1
#define SYS_getpriority  2
#define SYS_uptime       3
#define SYS_exit         4
#define SYS_write        5
#define SYS_procdump     6
#define SYS_open         7
#define SYS_close        8
#define SYS_read         9
#define SYS_getpid       10
#define SYS_wait         11
#define SYS_kill         12
#define SYS_fork         13
#define SYS_sbrk         14

// 统一的系统调用上下文（由陷入处理器从寄存器提取）
struct syscall_frame {
  unsigned long a0;
  unsigned long a1;
  unsigned long a2;
  unsigned long a3;
  unsigned long a4;
  unsigned long a5;
  unsigned long a6;
  unsigned long a7; // syscall number
  unsigned long sepc; // 原始 sepc
};

// 系统调用描述符
struct syscall_desc {
  long (*handler)(struct syscall_frame *f); // 统一处理函数
  const char *name;                         // 名称（用于调试）
  int arg_count;                            // 期望参数个数（0~6，可变参数可设为-1）
  int flags;                                // 权限/行为标志（预留）
};

// 系统调用表
extern struct syscall_desc syscall_table[];
extern const int syscall_table_size;

// 系统调用分发器
long syscall_dispatch(struct syscall_frame *f);

// 参数与用户指针提取辅助
int get_syscall_arg(struct syscall_frame *f, int n, long *arg);
int get_user_string(const char *u_str, char *k_buf, int max);
int get_user_buffer(const void *u_ptr, void *k_buf, int size);
int put_user_buffer(void *u_ptr, const void *k_buf, int size);
int validate_user_range(const void *u_ptr, int size);
int check_user_ptr(const void *u_ptr, int size);
int check_user_string(const char *u_str, int max);

// 权限检查（简单占位：返回 0 表示允许，<0 表示拒绝）
int syscall_check_perm(int sysno);

// 安全限额：限制单次读写与路径长度
#define SYS_MAX_XFER    4096
#define SYS_MAX_STRLEN  128

#endif // SYSCALL_H
