#include "types.h"
#include "memlayout.h"
#include "printf.h"
#include "console.h"
#include "pmm.h"
#include "pagetable.h"
#include "vm.h"
#include "assert.h"
extern void uartinit(void);
extern void uart_puts(char *s);
extern char etext[];
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
     printf("After enabling paging...\n",0);
     // 测试内核代码仍然可执行
  void (*code_test)(void) = (void (*)(void))((uint64)etext - 4); // 假设 etext 前有可执行代码
  code_test(); // 调用代码段中的函数（需确保安全）
  printf("Kernel code execution test passed\n",0);

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
void
start(void)
{
  // Initialize UART (baud rate 38400, 8-bit, no parity)
  uartinit();
  test_printf_basic();
  test_printf_edge_cases();
  test_console_features();
  test_pmm_basic();
  //pmm_init();
  test_physical_memory();
  test_pagetable();
  test_virtual_memory();
  //Spin forever (prevent undefined behavior)
  for(;;);
}
