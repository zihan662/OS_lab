#include "types.h"
#include "string.h"
#include "printf.h"
#include "spinlock.h"
#include "pmm.h"
#include "proc.h"
#include "cpu.h"
#include "riscv.h"

static struct proc proctable[NPROC];
static struct spinlock proc_table_lock;

static int nextpid = 1;
static struct spinlock pid_lock;

// 简单 PID 映射桶：pid % PIDMAP_SIZE -> 链表头
static struct proc *pidmap[PIDMAP_SIZE];

// 当前进程占位（后续由调度器管理）
static struct proc *current;

// Scheduling log throttling: remember last picked pid and a pick counter
static int last_sched_pid = -1;
static int sched_pick_counter = 0;

void proc_init(void) {
  initlock(&proc_table_lock, "proc_table");
  initlock(&pid_lock, "pid_lock");
  memset(pidmap, 0, sizeof(pidmap));

  for (int i = 0; i < NPROC; i++) {
    initlock(&proctable[i].lock, "proc");
    proctable[i].state = UNUSED;
    proctable[i].pid = 0;
    proctable[i].xstate = 0;
    proctable[i].killed = 0;
    proctable[i].parent = 0;
    proctable[i].chan = 0;
    proctable[i].pagetable = 0;
    proctable[i].kstack = 0;
    proctable[i].kstack_size = 0;
    proctable[i].entry = 0;
    proctable[i].pid_next = 0;
    memset(proctable[i].name, 0, sizeof(proctable[i].name));
  }
  current = 0;
}

static void pidmap_insert(struct proc *p) {
  int b = p->pid % PIDMAP_SIZE;
  // 保护桶结构变更
  acquire(&proc_table_lock);
  p->pid_next = pidmap[b];
  pidmap[b] = p;
  release(&proc_table_lock);
}

static void pidmap_remove(struct proc *p) {
  int b = p->pid % PIDMAP_SIZE;
  acquire(&proc_table_lock);
  struct proc *prev = 0, *cur = pidmap[b];
  while (cur) {
    if (cur == p) {
      if (prev) prev->pid_next = cur->pid_next; else pidmap[b] = cur->pid_next;
      break;
    }
    prev = cur;
    cur = cur->pid_next;
  }
  release(&proc_table_lock);
  p->pid_next = 0;
}

struct proc* find_proc_by_pid(int pid) {
  int b = pid % PIDMAP_SIZE;
  acquire(&proc_table_lock);
  struct proc *cur = pidmap[b];
  while (cur) {
    if (cur->pid == pid) { break; }
    cur = cur->pid_next;
  }
  release(&proc_table_lock);
  return cur;
}
// MLFQ：按优先级定义时间片长度（高优先级→更短时间片）
static inline int mlfq_slice_for(int prio) {
  // 约定：PRIORITY_MAX=10 → 2 ticks；PRIORITY_MIN=0 → 22 ticks
  // 线性插值：slice = 22 - 2*(prio)
  int base = 22 - 2 * prio;
  if (base < 2) base = 2;
  return base;
}
struct proc* alloc_process(void) {
  acquire(&proc_table_lock);
  struct proc *p = 0;
  for (int i = 0; i < NPROC; i++) {
    if (proctable[i].state == UNUSED) {
      p = &proctable[i];
      acquire(&p->lock);
      p->state = USED;
      release(&proc_table_lock);
      goto got;
    }
  }
  release(&proc_table_lock);
  return 0;

 got:
  // 分配 PID
  acquire(&pid_lock);
  p->pid = nextpid++;
  release(&pid_lock);

  // 分配内核栈（1 页）
  p->kstack_size = PGSIZE;
  p->kstack = alloc_page();
  if (p->kstack == 0) {
    p->state = UNUSED;
    release(&p->lock);
    return 0;
  }

  // 清理其余字段
  p->xstate = 0;
  p->killed = 0;
  p->parent = 0;
  p->chan = 0;
  p->pagetable = 0;
  p->entry = 0;
  memset(p->name, 0, sizeof(p->name));

  // 初始化调度属性
  p->priority = PRIORITY_DEFAULT;
  p->ticks = 0;
  p->wait_time = 0;
  p->slice_ticks = 0;
  p->need_resched = 0;

  // 加入 PID 映射
  pidmap_insert(p);

  release(&p->lock);
  return p;
}

void free_process(struct proc *p) {
  if (!p) return;
  acquire(&p->lock);

  // 从 PID 映射移除
  pidmap_remove(p);

  // 释放内核栈
  if (p->kstack) {
    free_page(p->kstack);
    p->kstack = 0;
    p->kstack_size = 0;
  }

  // 地址空间释放由未来 free_user_space 负责（当前未使用）
  p->pagetable = 0;

  p->pid = 0;
  p->xstate = 0;
  p->killed = 0;
  p->parent = 0;
  p->chan = 0;
  p->entry = 0;
  memset(p->name, 0, sizeof(p->name));

  // 重置调度属性
  p->priority = PRIORITY_MIN;
  p->ticks = 0;
  p->wait_time = 0;
  p->slice_ticks = 0;
  p->need_resched = 0;

  p->state = UNUSED;
  release(&p->lock);
}

int create_process_named(void (*entry)(void), const char *name) {
  struct proc *p = alloc_process();
  if (!p) return -1;

  acquire(&p->lock);
  p->entry = entry;
  p->parent = get_current_process();
  memset(&p->context, 0, sizeof(p->context));
  p->context.sp = (uint64)p->kstack + p->kstack_size;
  extern void kernel_thread_stub(void);
  p->context.ra = (uint64)kernel_thread_stub;
  p->state = RUNNABLE;
  if (name) {
    int i = 0;
    while (i < (int)sizeof(p->name) - 1 && name[i] != '\0') {
      p->name[i] = name[i];
      i++;
    }
    p->name[i] = '\0';
  }
  int pid = p->pid;
  release(&p->lock);
  return pid;
}

int create_process(void (*entry)(void)) {
  return create_process_named(entry, "kthread");
}

void exit_process(int status) {
  struct proc *cur = get_current_process();
  if (!cur) return;
  int int_state = intr_get();
  intr_off();
  acquire(&cur->lock);
  cur->xstate = status;
  cur->state = ZOMBIE;
  // 释放锁后直接切回调度器，不再运行该进程
  release(&cur->lock);
  struct cpu *c = mycpu();
  swtch(&cur->context, &c->context);
  // 不应该返回；恢复中断仅为防御
  intr_on(int_state);
}

int wait_process(int *status) {
  struct proc *cur = get_current_process();
  if (!cur) return -1;

  // 非阻塞扫描，找到首个 ZOMBIE 子进程并回收
  for (int i = 0; i < NPROC; i++) {
    struct proc *p = &proctable[i];
    acquire(&p->lock);
    int is_child = (p->parent == cur);
    int is_zombie = (p->state == ZOMBIE);
    if (is_child && is_zombie) {
      int pid = p->pid;
      int x = p->xstate;
      release(&p->lock);
      if (status) *status = x;
      free_process(p);
      return pid;
    }
    release(&p->lock);
  }
  return -1; // 暂不阻塞，调度器接入后可实现睡眠等待
}

// 简单阻塞等待指定 pid 的子进程：轮询 + yield
int waitpid(int pid, int *status) {
  struct proc *cur = get_current_process();
  if (!cur) return -1;
  for (;;) {
    struct proc *p = find_proc_by_pid(pid);
    if (!p || p->parent != cur) {
      // 不是当前父的子进程或已不存在
      return -1;
    }
    acquire(&p->lock);
    int is_zombie = (p->state == ZOMBIE);
    int x = p->xstate;
    release(&p->lock);
    if (is_zombie) {
      if (status) *status = x;
      free_process(p);
      return pid;
    }
    // 让出 CPU，等待子进程运行退出
    yield();
  }
}

struct proc* get_current_process(void) { return current; }
void set_current_process(struct proc *p) { current = p; }

// 内核线程入口桩：作为首次 swtch 的 RA 目标
void kernel_thread_stub(void) {
  struct proc *cur = get_current_process();
  if (cur && cur->entry) {
    cur->entry();
  }
  // 线程函数返回则退出
  exit_process(0);
  // 不应该返回；避免未定义行为
  for(;;) { }
}

// 让出 CPU：将当前进程置为 RUNNABLE 并切回调度器
void yield(void) {
  struct proc *p = get_current_process();
  if (!p) return;
  acquire(&p->lock);
  // 交互型进程（主动让出）不会被 MLFQ 在此处降级；重置片内计数
  p->slice_ticks = 0;
  p->need_resched = 0;
  p->state = RUNNABLE;
  release(&p->lock);
  struct cpu *c = mycpu();
  // 返回到调度器上下文
  swtch(&p->context, &c->context);
}

// 软抢占：在安全点检查是否用满时间片，若需要则让出 CPU
void preempt_check(void) {
  struct proc *p = get_current_process();
  if (!p) return;
  acquire(&p->lock);
  int need = (p->state == RUNNING) && p->need_resched;
  if (need) {
    p->slice_ticks = 0;
    p->need_resched = 0;
    p->state = RUNNABLE;
    release(&p->lock);
    struct cpu *c = mycpu();
    swtch(&p->context, &c->context);
    return;
  }
  release(&p->lock);
}

// 等待条件满足：避免 lost wakeup，正确使用锁与中断
void sleep(void *chan, struct spinlock *lk) {
  struct proc *p = get_current_process();
  if (!p) return;
  int int_state = intr_get();
  intr_off();
  // 保护状态与通道设置
  acquire(&p->lock);
  // 释放调用者锁，防止死锁；必须在持有 p->lock 的情况下释放以避免竞争
  release(lk);
  // 标记睡眠通道与状态
  p->chan = chan;
  p->state = SLEEPING;
  // 在切回调度器前释放 p->lock，避免单核下死锁
  release(&p->lock);
  // 切回调度器
  struct cpu *c = mycpu();
  swtch(&p->context, &c->context);
  // 被唤醒后重新获取 p->lock 并清理通道
  acquire(&p->lock);
  p->chan = 0;
  // 唤醒后先重新获取调用者锁，再释放 p->lock
  acquire(lk);
  release(&p->lock);
  // 恢复进入时的中断状态
  intr_on(int_state);
}

// 唤醒等待特定条件的所有进程
void wakeup(void *chan) {
  int int_state = intr_get();
  intr_off();
  for (int i = 0; i < NPROC; i++) {
    struct proc *p = &proctable[i];
    acquire(&p->lock);
    if (p->state == SLEEPING && p->chan == chan) {
      p->state = RUNNABLE;
      // 被唤醒后开始等待计时（由时钟中断统计）
    }
    release(&p->lock);
  }
  intr_on(int_state);
}

// 计算有效优先级：基础优先级 + aging 提升（按等待时长分段提升）
static int effective_priority(const struct proc *p) {
  int boost = p->wait_time / AGING_INTERVAL;
  int eff = p->priority + boost;
  if (eff > PRIORITY_MAX) eff = PRIORITY_MAX;
  if (eff < PRIORITY_MIN) eff = PRIORITY_MIN;
  return eff;
}

// 优先级调度器：选择有效优先级最高的 RUNNABLE 进程
void scheduler(void) {
  struct cpu *c = mycpu();
  c->proc = 0;
  static int rr_cursor = 0; // 轮转起点，用于等优先级的公平选择
  for(;;) {
    intr_on(1);
    // 选择候选：从 rr_cursor 开始扫描，实现基本 RR
    struct proc *best = 0;
    int best_score = -1;
    int best_idx = -1;
    for (int off = 0; off < NPROC; off++) {
      int i = (rr_cursor + off) % NPROC;
      struct proc *p = &proctable[i];
      acquire(&p->lock);
      if (p->state == RUNNABLE) {
        int score = effective_priority(p);
        if (score > best_score) {
          best_score = score;
          best = p;
          best_idx = i;
        }
      }
      release(&p->lock);
    }

    if (best) {
      acquire(&best->lock);
      if (best->state == RUNNABLE) {
        best->state = RUNNING;
        c->proc = best;
        set_current_process(best);
        // 下次从当前选择后的下一个位置开始，避免总是命中同一索引
        rr_cursor = (best_idx + 1) % NPROC;
        // 调度日志（精简版）：仅在 pid 变化选择打印
        sched_pick_counter++;
        if (best->pid != last_sched_pid) {
          //printf("sched_pick pid=%d prio=%d eff=%d slice=%d/%d ticks=%d\n",best->pid,best->priority,effective_priority(best),best->slice_ticks,mlfq_slice_for(best->priority),best->ticks);
          last_sched_pid = best->pid;
        }
        release(&best->lock);
        swtch(&c->context, &best->context);
        set_current_process(0);
        c->proc = 0;
      } else {
        release(&best->lock);
      }
    }
  }
}

// 每个时钟中断：统计运行/等待时长，并进行 aging
void proc_on_tick(void) {
  for (int i = 0; i < NPROC; i++) {
    struct proc *p = &proctable[i];
    acquire(&p->lock);
    if (p->state == RUNNING) {
      p->ticks++;
      // MLFQ：累计当前时间片内的 tick，用满则降级并重置
      p->slice_ticks++;
      int slice = mlfq_slice_for(p->priority);
      if (p->slice_ticks >= slice) {
        if (p->priority > PRIORITY_MIN) {
          p->priority--; // 用满时间片视为 CPU 密集型，降级
          printf("mlfq_demote pid=%d -> prio=%d\n", p->pid, p->priority);
        }
        p->slice_ticks = 0;
        p->need_resched = 1; // 请求软抢占
      }
    } else if (p->state == RUNNABLE) {
      p->wait_time++;
      // aging：等待时间达到阈值提升优先级，避免饥饿
      if (p->wait_time >= AGING_INTERVAL && p->priority < PRIORITY_MAX) {
        p->priority++;
        p->wait_time = 0;
        printf("aging_promote pid=%d -> prio=%d\n", p->pid, p->priority);
      }
    }
    release(&p->lock);
  }
}

// ---- 系统调用实现：优先级设置/查询 ----
int setpriority(int pid, int value) {
  if (value < PRIORITY_MIN || value > PRIORITY_MAX) return -1;
  struct proc *p = find_proc_by_pid(pid);
  if (!p) return -1;
  acquire(&p->lock);
  p->priority = value;
  p->wait_time = 0; // 重置等待计数，防止立即再次 aging
  p->slice_ticks = 0; // 重置片内计数，按新级别重新开始
  release(&p->lock);
  return 0;
}

int getpriority(int pid) {
  struct proc *p = find_proc_by_pid(pid);
  if (!p) return -1;
  acquire(&p->lock);
  int val = p->priority;
  release(&p->lock);
  return val;
}

// 调试输出：打印进程表
void proc_dump_table(void) {
  printf("=== Process Table ===\n");
  for (int i = 0; i < NPROC; i++) {
    struct proc *p = &proctable[i];
    acquire(&p->lock);
    if (p->state != UNUSED) {
      printf("PID:%d State:%d Name:%s\n", p->pid, p->state, p->name);
    }
    release(&p->lock);
  }
}

static const char* state_name(enum procstate s) {
  switch (s) {
    case UNUSED: return "UNUSED";
    case USED: return "USED";
    case RUNNABLE: return "RUNNABLE";
    case RUNNING: return "RUNNING";
    case SLEEPING: return "SLEEPING";
    case ZOMBIE: return "ZOMBIE";
    default: return "?";
  }
}

void proc_dump_detailed(void) {
  printf("PID PRIORITY STATE TICKS\n");
  for (int i = 0; i < NPROC; i++) {
    struct proc *p = &proctable[i];
    acquire(&p->lock);
    if (p->state != UNUSED) {
      printf("%d %d %s %d\n", p->pid, p->priority, state_name(p->state), p->ticks);
    }
    release(&p->lock);
  }
}
