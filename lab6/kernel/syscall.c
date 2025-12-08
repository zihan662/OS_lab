#include "types.h"
#include "printf.h"
#include "console.h"
#include "syscall.h"
#include "proc.h"
#include "spinlock.h"
#include "memlayout.h"
#include "riscv.h"
#include "fs.h"
#include "pagetable.h"
#include "string.h"
// 调试开关：系统调用跟踪
int debug_syscalls = 0;

// 基础权限检查（占位实现）：全部允许
int syscall_check_perm(int sysno) {
  (void)sysno;
  return 0;
}

// 用户地址基本校验：非空、范围不越过 TRAPFRAME、且不溢出
static int check_user_va_perm(const void *u_ptr, int size, int need_write) {
  if (!u_ptr || size < 0) return -1;
  struct proc *p = get_current_process();
  if (!p || !p->pagetable) return -1;
  unsigned long base = (unsigned long)u_ptr;
  unsigned long end = base + (unsigned long)size;
  if (end < base) return -1;
  if (end >= (unsigned long)TRAPFRAME) return -1;
  for (uint64 v = PGROUNDDOWN(base); v < PGROUNDUP(end); v += PGSIZE) {
    pte_t *pte = walk_lookup(p->pagetable, v);
    if (!pte) return -1;
    pte_t e = *pte;
    if (!(e & PTE_V) || !(e & PTE_U)) return -1;
    if (need_write) { if (!(e & PTE_W)) return -1; } else { if (!(e & PTE_R)) return -1; }
  }
  return 0;
}

int validate_user_range(const void *u_ptr, int size) {
  return check_user_va_perm(u_ptr, size, 0);
}

// 统一指针检查入口（可扩展权限/页表校验）
int check_user_ptr(const void *u_ptr, int size) {
  return validate_user_range(u_ptr, size);
}

// 从用户区复制字符串到内核缓冲区（最多 max-1 字节，保证以 \0 结尾）
int get_user_string(const char *u_str, char *k_buf, int max) {
  if (!u_str || !k_buf || max <= 0) return -1;
  // 先做一个粗略范围校验，避免明显错误
  if (validate_user_range(u_str, 1) < 0) return -1;
  int i = 0;
  for (; i < max - 1; i++) {
    char c = u_str[i];
    if ((unsigned long)&u_str[i] >= (unsigned long)TRAPFRAME) return -1;
    k_buf[i] = c;
    if (c == '\0') break;
  }
  if (i == max - 1 && k_buf[i] != '\0') {
    // 未在上限内终止，避免路径截断引发 TOCTTOU
    return -1;
  }
  k_buf[i] = '\0';
  return i; // 返回已拷贝长度（不含终止符）
}

// 从用户区复制缓冲区到内核缓冲区（简单检查 + 逐字节复制）
int get_user_buffer(const void *u_ptr, void *k_buf, int size) {
  if (!u_ptr || !k_buf || size < 0) return -1;
  if (check_user_va_perm(u_ptr, size, 0) < 0) return -1;
  const char *src = (const char *)u_ptr;
  char *dst = (char *)k_buf;
  for (int i = 0; i < size; i++) {
    if ((unsigned long)&src[i] >= (unsigned long)TRAPFRAME) return -1;
    dst[i] = src[i];
  }
  return size;
}

// 将内核缓冲区复制回用户区（简化版）
int put_user_buffer(void *u_ptr, const void *k_buf, int size) {
  if (!u_ptr || !k_buf || size < 0) return -1;
  if (check_user_va_perm(u_ptr, size, 1) < 0) return -1;
  char *dst = (char *)u_ptr;
  const char *src = (const char *)k_buf;
  for (int i = 0; i < size; i++) {
    if ((unsigned long)&dst[i] >= (unsigned long)TRAPFRAME) return -1;
    dst[i] = src[i];
  }
  return size;
}

// 提取第 n 个参数（a0..a6），返回 0 表示成功
int get_syscall_arg(struct syscall_frame *f, int n, long *arg) {
  if (!f || !arg || n < 0 || n > 6) return -1;
  unsigned long vals[7] = { f->a0, f->a1, f->a2, f->a3, f->a4, f->a5, f->a6 };
  *arg = (long)vals[n];
  return 0;
}

// ---- 具体 syscall 处理适配器 ----

extern uint64 uptime(void);
extern int sys_write(int fd, const char *buf, int n);
extern int setpriority(int pid, int value);
extern int getpriority(int pid);
extern void proc_dump_detailed(void);
extern void exit_process(int status);
extern struct proc* get_current_process(void);

static long h_setpriority(struct syscall_frame *f) {
  return (long)setpriority((int)f->a0, (int)f->a1);
}

static long h_getpriority(struct syscall_frame *f) {
  return (long)getpriority((int)f->a0);
}

static long h_uptime(struct syscall_frame *f) {
  (void)f;
  return (long)uptime();
}

static long h_exit(struct syscall_frame *f) {
  exit_process((int)f->a0);
  return 0; // 不返回，防御性设置
}

static long h_procdump(struct syscall_frame *f) {
  (void)f;
  proc_dump_detailed();
  return 0;
}

static long h_write(struct syscall_frame *f) {
  int fd = (int)f->a0;
  const char *u_buf = (const char *)f->a1;
  int n = (int)f->a2;
  if (n < 0) return -1;
  if (n > SYS_MAX_XFER) n = SYS_MAX_XFER; // 限制传输大小
  // 路由：fd==1/2 走控制台；否则走文件系统 fake FS 写入
  const int CHUNK = 128;
  char tmp[CHUNK];
  int written = 0;
  while (written < n) {
    int m = n - written;
    if (m > CHUNK) m = CHUNK;
    if (get_user_buffer(u_buf + written, tmp, m) < 0) return -1;
    int r;
    if (fd == 1 || fd == 2) {
      // 控制台输出
      r = sys_write(fd, tmp, m);
    } else {
      // 文件写入（fake FS）
      r = write(fd, tmp, m);
    }
    if (r < 0) return -1;
    written += m;
  }
  return (long)written;
}

static long h_open(struct syscall_frame *f) {
  const char *u_path = (const char *)f->a0;
  int flags = (int)f->a1;
  char k_path[SYS_MAX_STRLEN];
  if (get_user_string(u_path, k_path, sizeof(k_path)) < 0) return -1;
  return (long)open(k_path, flags);
}

static long h_close(struct syscall_frame *f) {
  int fd = (int)f->a0;
  return (long)close(fd);
}

static long h_read(struct syscall_frame *f) {
  int fd = (int)f->a0;
  void *u_buf = (void *)f->a1;
  int n = (int)f->a2;
  if (n < 0) return -1;
  if (n > SYS_MAX_XFER) n = SYS_MAX_XFER; // 限制传输大小
  const int CHUNK = 128;
  char tmp[CHUNK];
  int total = 0;
  while (total < n) {
    int m = n - total;
    if (m > CHUNK) m = CHUNK;
    int r = read(fd, tmp, m);
    if (r < 0) return -1;
    if (r == 0) break; // EOF
    if (put_user_buffer((char*)u_buf + total, tmp, r) < 0) return -1;
    total += r;
  }
  return (long)total;
}

static long h_getpid(struct syscall_frame *f) {
  (void)f;
  struct proc *p = get_current_process();
  return p ? (long)p->pid : -1;
}

static long h_wait(struct syscall_frame *f) {
  (void)f;
  return (long)wait_process(0);
}

static long h_kill(struct syscall_frame *f) {
  int pid = (int)f->a0;
  struct proc *p = find_proc_by_pid(pid);
  if (!p) return -1;
  struct proc *cur = get_current_process();
  // 权限约束：仅允许自杀或终止自己的子进程
  if (!(cur && (cur == p || p->parent == cur))) {
    printf("kill: permission denied pid=%d by pid=%d\n", pid, cur ? cur->pid : -1);
    return -1;
  }
  acquire(&p->lock);
  p->killed = 1;
  int is_self = (cur && cur->pid == pid);
  release(&p->lock);
  if (is_self) {
    exit_process(-1);
  }
  // 简化：不强制立即终止他进程，由调度/检查点处理
  return 0;
}

static long h_fork(struct syscall_frame *f) {
  struct proc *parent = get_current_process();
  if (!parent || !parent->pagetable) {
    return -1;
  }
  // 分配子进程结构
  struct proc *child = alloc_process();
  if (!child) return -1;
  acquire(&child->lock);
  child->parent = parent;
  // 为子进程创建独立用户页表，并初始化内核映射
  pagetable_t cpt = create_pagetable();
  if (!cpt) { release(&child->lock); free_process(child); return -1; }
  if (init_user_pagetable(cpt) != 0) { release(&child->lock); destroy_pagetable(cpt); free_process(child); return -1; }
  // 复制父进程的用户地址空间（低于 TRAPFRAME 的 U 页）
  if (copy_user_space(cpt, parent->pagetable) != 0) { release(&child->lock); destroy_pagetable(cpt); free_process(child); return -1; }
  child->pagetable = cpt;
  // 设置子进程用户返回点与栈指针：从当前陷入上下文获取
  child->u_sepc = f->sepc + 4; // 从 fork 的 ecall 返回点继续
  child->u_sp = r_sscratch();  // 原始用户栈指针在 sscratch 中
  // 子进程入口：恢复到用户态并返回 0
  extern void user_fork_entry(void);
  memset(&child->context, 0, sizeof(child->context));
  child->context.sp = (uint64)child->kstack + child->kstack_size;
  extern void kernel_thread_stub(void);
  child->context.ra = (uint64)kernel_thread_stub;
  child->entry = user_fork_entry;
  child->state = RUNNABLE;
  int cpid = child->pid;
  release(&child->lock);
  // 父进程返回子 pid，子进程在 user_fork_entry 中返回 0
  return (long)cpid;
}

static long h_sbrk(struct syscall_frame *f) {
  // 未实现用户堆与页分配，返回不支持
  (void)f;
  printf("sys_sbrk: not supported (no user heap)\n");
  return -1;
}

// 系统调用表
struct syscall_desc syscall_table[] = {
  [SYS_setpriority] = { h_setpriority, "setpriority", 2, 0 },
  [SYS_getpriority] = { h_getpriority, "getpriority", 1, 0 },
  [SYS_uptime]      = { h_uptime,      "uptime",      0, 0 },
  [SYS_exit]        = { h_exit,        "exit",        1, 0 },
  [SYS_write]       = { h_write,       "write",       3, 0 },
  [SYS_procdump]    = { h_procdump,    "procdump",    0, 0 },
  [SYS_open]        = { h_open,        "open",        2, 0 },
  [SYS_close]       = { h_close,       "close",       1, 0 },
  [SYS_read]        = { h_read,        "read",        3, 0 },
  [SYS_getpid]      = { h_getpid,      "getpid",      0, 0 },
  [SYS_wait]        = { h_wait,        "wait",        0, 0 },
  [SYS_kill]        = { h_kill,        "kill",        1, 0 },
  [SYS_fork]        = { h_fork,        "fork",        0, 0 },
  [SYS_sbrk]        = { h_sbrk,        "sbrk",        1, 0 },
};

const int syscall_table_size = sizeof(syscall_table) / sizeof(syscall_table[0]);

// 统一分发器
long syscall_dispatch(struct syscall_frame *f) {
  if (!f) return -1;
  int sysno = (int)f->a7;
  if (sysno < 0 || sysno >= syscall_table_size) {
    printf("syscall: unknown num=%d sepc=%p\n", sysno, (void*)f->sepc);
    return -1;
  }
  // 权限检查
  if (syscall_check_perm(sysno) < 0) {
    printf("syscall: perm denied num=%d pid=%d\n", sysno, get_current_process() ? get_current_process()->pid : -1);
    return -1;
  }
  struct syscall_desc *d = &syscall_table[sysno];
  if (!d->handler) {
    printf("syscall: missing handler num=%d\n", sysno);
    return -1;
  }
  // 可选：参数个数检查（示意用途）
  if (d->arg_count >= 0 && d->arg_count > 6) {
    printf("syscall: invalid arg_count num=%d count=%d\n", sysno, d->arg_count);
    return -1;
  }
  long ret = d->handler(f);
  return ret;
}
