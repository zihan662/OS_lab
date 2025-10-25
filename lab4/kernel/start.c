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

  // 通过重复触发非法指令异常来近似测量陷阱处理开销
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
    // 清空旧的中断处理链，避免重复注册导致多次调用
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

void
start(void)
{
  // 仅允许 hart 0 进入内核初始化，其它 hart 原地自旋
  // tp 已在 entry.S 中由 OpenSBI 的 a0 设置，无需再写
  // Initialize UART (baud rate 38400, 8-bit, no parity)
  uartinit();
  if (r_tp() != 0) {
    for(;;);
  }
  test_printf_basic();
  test_printf_edge_cases();
  test_console_features();
  test_pmm_basic();
  //pmm_init();
  test_physical_memory();
  test_pagetable();
  test_virtual_memory();
  test_timer_interrupt();
  // 新增性能测试与异常处理测试
  test_interrupt_overhead();
  test_exception_handling();
  //Spin forever (prevent undefined behavior)
  for(;;);
}
