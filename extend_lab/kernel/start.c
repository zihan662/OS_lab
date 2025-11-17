#include "types.h"
#include "memlayout.h"
#include "printf.h"
#include "console.h"
#include "pmm.h"
#include "pagetable.h"
#include "vm.h"
#include "assert.h"
#include "interrupts.h"
#include "timer.h"
#include "riscv.h"
#include "proc.h"
#include "spinlock.h"
#include "dir.h"
#include "bcache.h"
#include "string.h"
#include <stddef.h>
#include "log.h"
extern void uartinit(void);
extern void uart_puts(char *s);
extern char etext[];
static volatile uint64 ticks_observed = 0;
static volatile int *test_flag_ptr = 0;
// 时钟中断回调：保留测试计数，同时进行进程时间统计/aging
void schedule_on_tick(void) {
  ticks_observed++;
  if (test_flag_ptr) (*test_flag_ptr)++;
  extern void proc_on_tick(void);
  proc_on_tick();
}
// Kernel entry point from entry.S
void test_printf_basic() {
    printf("Testing integer: %d\n", 42);
    printf("Testing negative: %d\n", -123);
    printf("Testing zero: %d\n", 0);
    printf("Testing hex: 0x%x\n", 0xABC);
    printf("Testing string: %s\n", "Hello");
    printf("Testing char: %c\n", 'X');
    printf("Testing percent: %%\n");
}

void test_printf_edge_cases() {
    printf("INT_MAX: %d\n", 2147483647);
    printf("INT_MIN: %d\n", -2147483648);
    printf("NULL string: %s\n", (char*)0);
    printf("Empty string: %s\n", "");
}
void test_console_features() {
    clear_screen();  // 清屏
    goto_xy(5, 3);  // 光标移到 (5,3)
    printf_color(COLOR_RED, "Red text at (5,3): %d\n", 123);
    goto_xy(1, 5);  // 光标移到 (1,5)
    printf_color(COLOR_BLUE, "Blue text at (1,5): %s\n", "Hello");
    clear_line();   // 清除当前行
    goto_xy(1, 7);  // 光标移到 (1,7)
    printf("Line after clear: %d\n", 456);
}
void test_pmm_basic(){
  pmm_init();
  void *p1 = alloc_page();
  printf("Allocated page: %p\n", p1);
  free_page(p1);
  printf("Freed page: %p\n", p1);
  void *p3 = alloc_pages(2);
  printf("Allocated 2 pages: %p\n", p3);
  free_pages(p3,2);
  printf("Freed 2 pages: %p\n", p3);
}
void test_physical_memory(void) {
     // 测试基本分配和释放
     void *page1 = alloc_page();
     printf("Allocated page: %p\n", page1);
     void *page2 = alloc_page();
     printf("Allocated page: %p\n", page2);
     assert(page1 != page2);
     assert(((uint64)page1 & 0xFFF) == 0); // 页对齐检查
     // 测试数据写入
     *(int*)page1 = 0x12345678;
     assert(*(int*)page1 == 0x12345678);
     // 测试释放和重新分配
     free_page(page1);
     void *page3 = alloc_page();
     // page3可能等于page1（取决于分配策略）
     free_page(page2);
     free_page(page3);
     printf("physical successed !",0);
   }
void test_pagetable(void) {
     pagetable_t pt = create_pagetable();
     // 测试基本映射
     uint64 va = 0x1000000;
     uint64 pa = (uint64)alloc_page();
     assert(map_page(pt, va, pa, PTE_R | PTE_W) == 0);
     // 测试地址转换
     pte_t *pte = walk_lookup(pt, va);
     assert(pte != 0 && (*pte & PTE_V));
     assert(PTE_PA(*pte) == pa);
     // 测试权限位
     assert(*pte & PTE_R);
     assert(*pte & PTE_W);
     assert(!(*pte & PTE_X));
     free_page((void*)pa); // 释放物理页
     destroy_pagetable(pt);
     printf("pagetable successed !",0);
   }
void test_virtual_memory(void) {
     printf("Before enabling paging...\n",0);
     // 启用分页
     kvminit();
     kvminithart();
     trap_init();
     timer_init(1000000ULL);
     printf("After enabling paging...\n",0);
     // 已移除不安全的内核代码执行测试

  // 测试内核数据仍然可访问
  static int test_data = 0;
  test_data = 0xabcdef;
  if (test_data != 0xabcdef) {
    panic("test_virtual_memory: data access failed");
  }
  printf("Kernel data access test passed: 0x%x\n", test_data);

  // 测试设备访问仍然正常
  uart_puts("UART test after paging\n");
  printf("UART device access test passed\n",0);

  printf("Finished test_virtual_memory\n",0);
}
void test_timer_interrupt(void) {
  printf("Testing timer interrupt...\n");
  uint64 start_time = get_time();
  int interrupt_count = 0;
  int last = -1;
  test_flag_ptr = &interrupt_count;
  while (interrupt_count < 5) {
    if (interrupt_count != last) {
      printf("Waiting for interrupt %d...\n", interrupt_count + 1);
      for (volatile int i = 0; i < 1000000; i++);
      last = interrupt_count;
    }
    asm volatile("wfi");
  }
  test_flag_ptr = 0;
  uint64 end_time = get_time();
  printf("Timer test completed: %d interrupts in %p cycles\n", interrupt_count, (void*)(end_time - start_time));
}

// 异常处理测试
void test_exception_handling(void) {
  printf("Testing exception handling...\n");

  // 测试除零异常（RISC-V 整数除零通常不产生异常，跳过）
  printf("Skip divide-by-zero test (no trap in RV64I).\n");

  // 测试非法指令异常：使用明确的非法编码（不依赖特权 CSR）
  printf("-> Trigger illegal instruction via .word 0xffffffff\n");
  asm volatile(".word 0xffffffff");
  printf("Illegal instruction handled and continued.\n");

  // 测试内存访问异常：读取未映射地址（可能导致内核 panic）
  printf("-> Trigger load page fault by reading VA=0x0\n");
  volatile uint64 v = *(volatile uint64*)0x0;
  (void)v;

  printf("Exception tests completed\n");
}

// 性能测试
void test_interrupt_overhead(void) {
  printf("Testing interrupt overhead...\n");
  const int N = 256;
  uint64 sum = 0;
  for (int i = 0; i < N; i++) {
    uint64 t0 = get_time();
    asm volatile(".word 0xffffffff");
    uint64 t1 = get_time();
    sum += (t1 - t0);
  }
  printf("Trap (illegal) avg cycles: %p\n", (void*)(sum / N));

  // 分析中断频率对系统性能的影响：改变定时器间隔进行采样
  uint64 window = 10 * 1000000ULL; // 采样窗口：10M cycles
  uint64 intervals[] = { 10000ULL, 50000ULL, 100000ULL };
  for (int i = 0; i < 3; i++) {
    register_interrupt(5, 0);
    ticks_observed = 0;
    timer_init(intervals[i]);
    uint64 start = get_time();
    while (get_time() - start < window) { /* busy-wait */ }
    printf("interval=%p -> ticks=%p in %p cycles\n",
           (void*)intervals[i], (void*)ticks_observed, (void*)window);
  }
  printf("Performance tests completed\n");
}

// ---- 新增：延时与测试任务/用例 ----
static void delay_cycles(uint64 cycles) {
  uint64 start = get_time();
  while (get_time() - start < cycles) {
    // 允许其他进程运行，避免长时间独占
    yield();
  }
}
// 消耗CPU时间但不主动让出，便于统计 RUNNING ticks
static void burn_cycles(uint64 cycles) {
  uint64 start = get_time();
  while (get_time() - start < cycles) {
    asm volatile("");
    // 软抢占演示：在忙等期间也检查是否用满片需要让出
    extern void preempt_check(void);
    preempt_check();
  }
}
static void simple_task(void) {
  struct proc *p = get_current_process();
  printf("simple_task pid=%d\n", p ? p->pid : -1);
  for (int i = 0; i < 5; i++) {
    yield();
  }
}

static void cpu_intensive_task(void) {
  volatile uint64 acc = 0;
  for (int i = 0; i < 200000; i++) {
    acc += i * 2654435761ULL;
    if ((i % 10000) == 0) yield();
  }
}


// 生产者-消费者共享缓冲区
#define BUF_SIZE 16
static int buf[BUF_SIZE];
static int head = 0, tail = 0, count = 0;
static struct spinlock buf_lock;
static int not_empty_var = 0;
static void *not_empty_chan = &not_empty_var;

static void shared_buffer_init(void) {
  initlock(&buf_lock, "buf_lock");
  head = tail = count = 0;
  not_empty_var = 0;
}

static void producer_task(void) {
  for (int i = 0; i < 32; i++) {
    acquire(&buf_lock);
    if (count < BUF_SIZE) {
      buf[tail] = i;
      tail = (tail + 1) % BUF_SIZE;
      count++;
      printf("produce %d (count=%d)\n", i, count);
      wakeup(not_empty_chan);
    }
    release(&buf_lock);
    yield();
  }
}

static void consumer_task(void) {
  int consumed = 0;
  while (consumed < 32) {
    acquire(&buf_lock);
    while (count == 0) {
      sleep(not_empty_chan, &buf_lock);
    }
    int v = buf[head];
    head = (head + 1) % BUF_SIZE;
    count--;
    consumed++;
    printf("consume %d (count=%d)\n", v, count);
    release(&buf_lock);
    yield();
  }
}

// 测试函数集成
void test_process_creation(void) {
  printf("Testing process creation...\n");
  int pid = create_process_named(simple_task, "simple");
  assert(pid > 0);
  int count_created = 0;
  for (int i = 0; i < NPROC + 5; i++) {
    int tpid = create_process_named(simple_task, "simple");
    if (tpid > 0) {
      count_created++;
    } else {
      break;
    }
  }
  int total_created = count_created + 1; // 包含最初的 pid
  printf("Created %d processes\n", total_created);
  // 回收所有子进程：非阻塞 wait，需要主动让出 CPU 让子进程运行
  int reaped = 0;
  while (reaped < total_created) {
    int w = wait_process(NULL);
    if (w == -1) {
      // 没有僵尸子进程可回收，先让出 CPU 让子进程运行
      yield();
    } else {
      reaped++;
    }
  }
}

void test_scheduler(void) {
  printf("Testing scheduler...\n");
  for (int i = 0; i < 3; i++) {
    create_process_named(cpu_intensive_task, "cpu");
  }
  uint64 start_time = get_time();
  delay_cycles(1000000ULL); // 约等待一段时间并让出CPU
  uint64 end_time = get_time();
  printf("Scheduler test completed in %p cycles\n", (void*)(end_time - start_time));
}

void test_synchronization(void) {
  printf("Testing synchronization...\n");
  shared_buffer_init();
  int pid_prod = create_process_named(producer_task, "producer");
  int pid_cons = create_process_named(consumer_task, "consumer");
  // 阻塞等待两个特定子任务完成
  int waits = 0;
  if (pid_prod > 0 && waitpid(pid_prod, NULL) > 0) waits++;
  if (pid_cons > 0 && waitpid(pid_cons, NULL) > 0) waits++;
  printf("Synchronization test completed (waits=%d)\n", waits);
}

void debug_proc_table(void) {
  // 使用proc层提供的打印函数，避免访问内部静态表
  extern void proc_dump_table(void);
  proc_dump_table();
}

// ====== 文件系统测试（内核环境改编版） ======
void test_filesystem_integrity(void) {
  printf("Testing filesystem integrity...\n");
  // 创建测试文件
  int fd = open("testfile" , O_CREATE | O_RDWR);
  assert(fd >= 0);
  // 写入数据
  char buffer[] = "Hello, filesystem!";
  int bytes = write(fd, buffer, strlen(buffer));
  assert(bytes == (int)strlen(buffer));
  close(fd);
  // 重新打开并验证
  fd = open("testfile" , O_RDONLY);
  assert(fd >= 0);
  char read_buffer[64];
  bytes = read(fd, read_buffer, sizeof(read_buffer));
  read_buffer[bytes] = '\0';
  assert(strcmp(buffer, read_buffer) == 0);
  close(fd);
  // 删除文件
  assert(unlink("testfile") == 0);
  printf("Filesystem integrity test passed\n");
}

static void fs_worker_task(void) {
  // 并发访问：不同任务对若干块执行读写，观察计数器变化与锁正确性
  for (int j = 0; j < 200; j++) {
    uint32 bno = 200 + (j % 4);
    struct buffer_head *bh = get_block(0, bno);
    if (bh) {
      if ((j % 16) == 0) {
        // 偶尔写回以测试并发写路径
        bh->dirty = 1;
        sync_block(bh);
      }
      put_block(bh);
    }
    if ((j % 25) == 0) yield();
  }
}

void test_concurrent_access(void) {
  printf("Testing concurrent file access...\n");
  int pids[4]; int n = 0;
  for (int i = 0; i < 4; i++) {
    int pid = create_process_named(fs_worker_task, "fs_worker");
    if (pid > 0) pids[n++] = pid;
  }
  for (int i = 0; i < n; i++) {
    waitpid(pids[i], NULL);
  }
  printf("Concurrent access test completed\n");
}
// 崩溃恢复测试：构造带日志的未提交事务，模拟“崩溃后重启恢复”
void test_crash_recovery(void) {
  printf("Testing crash recovery...\n");
  // 初始化日志系统（使用常量定义的日志区）
  struct superblock sb;
  memset(&sb, 0, sizeof(sb));
  sb.log_start = LOG_START;
  sb.log_size = LOG_SIZE;
  log_init(0, &sb);

  // 选择两个目标块，写入基线内容 BEFORE
  const char *BEFORE = "BEFORE";
  const char *AFTER  = "AFTER";
  uint32 t0 = 500, t1 = 501;
  struct buffer_head *bh0 = get_block(0, t0);
  struct buffer_head *bh1 = get_block(0, t1);
  if (!bh0 || !bh1) {
    if (bh0) put_block(bh0);
    if (bh1) put_block(bh1);
    printf("Crash recovery setup failed: cannot get target blocks\n");
    return;
  }
  memcpy(bh0->data, BEFORE, strlen(BEFORE));
  bh0->dirty = 1; sync_block(bh0);
  memcpy(bh1->data, BEFORE, strlen(BEFORE));
  bh1->dirty = 1; sync_block(bh1);
  put_block(bh0); put_block(bh1);

  // 将“事务数据”写入日志区数据块（LOG_START+1, LOG_START+2）
  struct buffer_head *ld0 = get_block(0, LOG_START + 1);
  struct buffer_head *ld1 = get_block(0, LOG_START + 2);
  if (!ld0 || !ld1) {
    if (ld0) put_block(ld0);
    if (ld1) put_block(ld1);
    printf("Crash recovery setup failed: cannot get log data blocks\n");
    return;
  }
  // 将 AFTER 写入目标块，并把整个目标块内容复制到日志区，保证日志数据与预期一致
  bh0 = get_block(0, t0);
  bh1 = get_block(0, t1);
  if (!bh0 || !bh1) {
    if (bh0) put_block(bh0);
    if (bh1) put_block(bh1);
    printf("Crash recovery setup failed: cannot reacquire targets\n");
    // 释放日志块引用
    if (ld0) put_block(ld0);
    if (ld1) put_block(ld1);
    return;
  }
  size_t n_after = strlen(AFTER);
  memcpy(bh0->data, AFTER, n_after);
  bh0->dirty = 1; sync_block(bh0);
  memcpy(ld0->data, bh0->data, BLOCK_SIZE);
  ld0->dirty = 1; sync_block(ld0);
  memcpy(bh1->data, AFTER, n_after);
  bh1->dirty = 1; sync_block(bh1);
  memcpy(ld1->data, bh1->data, BLOCK_SIZE);
  ld1->dirty = 1; sync_block(ld1);
  // 模拟“崩溃”：home blocks 未安装事务，重置为零
  memset(bh0->data, 0, BLOCK_SIZE);
  bh0->dirty = 1; sync_block(bh0);
  memset(bh1->data, 0, BLOCK_SIZE);
  bh1->dirty = 1; sync_block(bh1);
  put_block(bh0);
  put_block(bh1);
  // 保持日志数据块引用，避免在恢复前被LRU淘汰

  // 写入日志头（记录目标块号集合），模拟“崩溃前已写入日志”
  struct buffer_head *hdr = get_block(0, LOG_START);
  if (!hdr) {
    printf("Crash recovery setup failed: cannot get log header block\n");
    return;
  }
  struct log_header lh;
  memset(&lh, 0, sizeof(lh));
  lh.n = 2;
  lh.block[0] = t0;
  lh.block[1] = t1;
  memcpy(hdr->data, &lh, sizeof(lh));
  hdr->dirty = 1; sync_block(hdr);
  // 保持日志头块引用，避免在恢复前被淘汰

  // 在恢复前抓取并保留目标块引用，避免在清空日志头时被淘汰
  struct buffer_head *keep0 = get_block(0, t0);
  struct buffer_head *keep1 = get_block(0, t1);
  // 模拟“重启”：直接调用恢复逻辑，将日志数据应用到 home blocks
  recover_log();

  // 校验：目标块内容应为 AFTER
  int ok = 0;
  bh0 = get_block(0, t0);
  bh1 = get_block(0, t1);
  size_t n = strlen(AFTER);
  if (!bh0 || !bh1) {
    printf("Recovery verify failed: bh0=%p bh1=%p\n", bh0, bh1);
  } else {
    int cmp0 = memcmp(bh0->data, AFTER, n);
    int cmp1 = memcmp(bh1->data, AFTER, n);
    printf("Verify: n=%d, bh0.valid=%d bh1.valid=%d\n", (int)n, bh0->valid, bh1->valid);
    printf("t0 first %d bytes:", (int)n);
    for (int i = 0; i < (int)n; i++) {
      printf(" %x", (unsigned)(unsigned char)bh0->data[i]);
    }
    printf(" \n");
    printf("t1 first %d bytes:", (int)n);
    for (int i = 0; i < (int)n; i++) {
      printf(" %x", (unsigned)(unsigned char)bh1->data[i]);
    }
    printf("\nmemcmp t0=%d t1=%d\n", cmp0, cmp1);
    if (cmp0 == 0 && cmp1 == 0) ok = 1;
  }
  if (bh0) put_block(bh0);
  if (bh1) put_block(bh1);
  // 释放日志相关块引用
  if (hdr) put_block(hdr);
  if (ld0) put_block(ld0);
  if (ld1) put_block(ld1);
  // 释放保持的目标块引用
  if (keep0) put_block(keep0);
  if (keep1) put_block(keep1);
  printf("Crash recovery %s\n", ok ? "passed" : "failed");
}
// 文件系统性能测试（基于块缓存模拟）
void test_filesystem_performance(void) {
  printf("Testing filesystem performance...\n");
  // 小文件写入（模拟为写入 1000 个不同块，每次 4 字节）
  uint64 start_time = get_time();
  for (int i = 0; i < 1000; i++) {
    uint32 bno = 4000 + (uint32)i; // 选择不与日志/超级块冲突的区域
    struct buffer_head *bh = get_block(0, bno);
    if (bh) {
      memcpy(bh->data, "test", 4);
      bh->dirty = 1; sync_block(bh);
      put_block(bh);
    }
  }
  uint64 small_files_time = get_time() - start_time;

  // 大文件写入（模拟为顺序写入 4MB 数据：1024 块 * 4KB）
  start_time = get_time();
  for (int i = 0; i < 1024; i++) {
    uint32 bno = 6000 + (uint32)i;
    struct buffer_head *bh = get_block(0, bno);
    if (bh) {
      memset(bh->data, (unsigned char)(i & 0xFF), BLOCK_SIZE);
      bh->dirty = 1; sync_block(bh);
      put_block(bh);
    }
  }
  uint64 large_file_time = get_time() - start_time;

   printf("Small blocks (1000x4B): %p cycles\n", (void*)small_files_time);
  printf("Large blocks (1024x4KB ~4MB): %p cycles\n", (void*)large_file_time);
}
// ==== 调度场景测试任务 ====
static void task_A(void) {
  struct proc *p = get_current_process();
  for (int i = 0; i < 50; i++) {
    burn_cycles(20000ULL);
    yield();
  }
  printf("task_A finish pid=%d ticks=%d\n", p ? p->pid : -1, p ? p->ticks : -1);
}

static void task_B(void) {
  struct proc *p = get_current_process();
  for (int i = 0; i < 50; i++) {
    burn_cycles(20000ULL);
    yield();
  }
  printf("task_B finish pid=%d ticks=%d\n", p ? p->pid : -1, p ? p->ticks : -1);
}

static void task_C(void) {
  struct proc *p = get_current_process();
  for (int i = 0; i < 50; i++) {
    burn_cycles(20000ULL);
    yield();
  }
  printf("task_C finish pid=%d ticks=%d\n", p ? p->pid : -1, p ? p->ticks : -1);
}

static void test_sched_T1(void) {
  printf("[T1] 两个任务，优先级差距大\n");
  int pidA = create_process_named(task_A, "task_A");
  int pidB = create_process_named(task_B, "task_B");
  printf("created: A=%d B=%d\n", pidA, pidB);
  setpriority(pidA, 8);
  setpriority(pidB, 2);
  for (int done = 0; done < 2;) {
    int w = wait_process(0);
    if (w == -1) { yield(); }
    else { done++; }
  }
  extern void proc_dump_detailed(void);
  proc_dump_detailed();
}

static void test_sched_T2(void) {
  printf("[T2] 相同优先级，行为等价RR（观察交替进展）\n");
  int pidA = create_process_named(task_A, "task_A");
  int pidB = create_process_named(task_B, "task_B");
  setpriority(pidA, 5);
  setpriority(pidB, 5);
  for (int done = 0; done < 2;) {
    int w = wait_process(0);
    if (w == -1) { yield(); }
    else { done++; }
  }
  extern void proc_dump_detailed(void);
  proc_dump_detailed();
}

static void test_sched_T3(void) {
  printf("[T3] 高低混合 + aging，最终均完成\n");
  int p1 = create_process_named(task_A, "task_A");
  int p2 = create_process_named(task_B, "task_B");
  int p3 = create_process_named(task_C, "task_C");
  setpriority(p1, 8);
  setpriority(p2, 2);
  setpriority(p3, 6);
  for (int done = 0; done < 3;) {
    int w = wait_process(0);
    if (w == -1) { yield(); }
    else { done++; }
  }
  extern void proc_dump_detailed(void);
  proc_dump_detailed();
}
// 作为初始内核线程，运行测试序列
static void kernel_test_main(void) {
  //test_process_creation();
  //test_scheduler();
  //test_synchronization();

  // 文件系统调试与测试
  //debug_filesystem_state();
  //debug_disk_io();
  //test_filesystem_integrity();
  //test_concurrent_access();
  //test_crash_recovery();
  //test_filesystem_performance();
  timer_init(80000ULL);
  test_sched_T1();
  test_sched_T2();
  test_sched_T3();
  printf("All integrated tests completed.\n");
}

void
start(void)
{
  uartinit();
  if (r_tp() != 0) {
    for(;;);
  }
  test_printf_basic();
  test_printf_edge_cases();
  test_console_features();
  test_pmm_basic();
  test_physical_memory();
  test_pagetable();
  test_virtual_memory();
  test_timer_interrupt();
  test_interrupt_overhead();
  //test_exception_handling();
 
  bcache_init();
  // 将综合测试以内核线程运行，并进入调度器
  int root = create_process_named(kernel_test_main, "kernel_test_main");
  assert(root > 0);
  scheduler();
   for(;;);
}
