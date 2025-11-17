#ifndef PROC_H
#define PROC_H

#include "types.h"
#include "spinlock.h"
#include "pagetable.h"

// 最大进程数（可根据内存调优）
#define NPROC 64

// PID 映射桶大小（用于加速 pid→proc 查找）
#define PIDMAP_SIZE 128

// 优先级相关配置
#define PRIORITY_MIN 0
#define PRIORITY_MAX 10
#define PRIORITY_DEFAULT 5
// aging：等待时长每达到该阈值，提升 1 级优先级（至多到 MAX）
#define AGING_INTERVAL 5

// 进程状态
enum procstate {
  UNUSED = 0,
  USED,
  RUNNABLE,
  RUNNING,
  SLEEPING,
  ZOMBIE,
};

// 调度上下文：保存 RISC-V 的被调用者保存寄存器
struct context {
  uint64 ra;   // 返回地址
  uint64 sp;   // 栈指针
  uint64 s0;
  uint64 s1;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
};

struct proc {
  struct spinlock lock;       // 保护此进程的可变字段
  enum procstate state;       // 调度状态

  int pid;                    // 进程唯一标识
  int xstate;                 // 退出码
  int killed;                 // 异步终止标记

  struct proc *parent;        // 父进程
  void *chan;                 // 睡眠通道（留作未来 sleep/wakeup）

  // 地址空间（未来用户态支持）
  pagetable_t pagetable;      // 用户页表根（当前为占位）

  // 内核栈
  void *kstack;               // 内核栈基地址（页对齐）
  uint64 kstack_size;         // 内核栈大小（字节）

  // 进程入口（内核线程风格）
  void (*entry)(void);

  // 调度上下文
  struct context context;     // 用于 swtch 的寄存器快照

  // 辅助：PID 哈希桶链表指针
  struct proc *pid_next;

  // 调试名
  char name[16];

  // ---- 调度属性：优先级与时间统计 ----
  int priority;               // 进程优先级 (0~10)，默认 5，值越大越高
  int ticks;                  // 已用 CPU 时间（按时钟中断累计）
  int wait_time;              // 等待时长（RUNNABLE 态累计，用于 aging）
  int slice_ticks;            // MLFQ：当前时间片内已用 tick 数
  int need_resched;           // 时间片用尽请求抢占（软抢占标志）
};

// 核心接口
struct proc* alloc_process(void);     // 分配进程结构
void free_process(struct proc *p);    // 释放进程资源
int create_process(void (*entry)(void)); // 创建新进程，返回 pid 或 <0
int create_process_named(void (*entry)(void), const char *name); // 创建并命名
void exit_process(int status);        // 终止当前进程
int wait_process(int *status);        // 等待子进程，返回子 pid 或 -1
int waitpid(int pid, int *status);    // 等待特定子进程退出，返回 pid 或 -1

// 额外辅助接口
void proc_init(void);                 // 初始化进程子系统
struct proc* find_proc_by_pid(int pid); // 快速查找 PID

// 当前进程（简单占位，待调度器接入）
struct proc* get_current_process(void);
void set_current_process(struct proc *p);

// 上下文切换
void swtch(struct context *old, struct context *new);

// 进程同步原语
void sleep(void *chan, struct spinlock *lk);
void wakeup(void *chan);

// 新增：调度/让出原语原型
void yield(void);
void scheduler(void);

// 调试输出：打印进程表
void proc_dump_table(void);
void proc_dump_detailed(void);

// 每个时钟中断回调：进行调度统计与 aging
void proc_on_tick(void);

// 系统调用接口：调整/查询优先级
int setpriority(int pid, int value);
int getpriority(int pid);

#endif // PROC_H