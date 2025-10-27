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
#include <stddef.h>
extern void uartinit(void);
extern void uart_puts(char *s);
extern char etext[];
static volatile uint64 ticks_observed = 0;
static volatile int *test_flag_ptr = 0;
void schedule_on_tick(void) { ticks_observed++; if (test_flag_ptr) (*test_flag_ptr)++; }
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
    printf("reaped:%d \n",reaped);
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

// 作为初始内核线程，运行测试序列
static void kernel_test_main(void) {
  test_process_creation();
  test_scheduler();
  test_synchronization();
  debug_proc_table();
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
 
  // 将综合测试以内核线程运行，并进入调度器
  int root = create_process_named(kernel_test_main, "kernel_test_main");
  assert(root > 0);
  scheduler();
   for(;;);
}
