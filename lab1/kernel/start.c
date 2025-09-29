#include "types.h"
#include "memlayout.h"
#include "printf.h"
#include "console.h"
extern void uartinit(void);
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
void
start(void)
{
  // Initialize UART (baud rate 38400, 8-bit, no parity)
  uartinit();

  test_printf_basic();
  test_printf_edge_cases();
  test_console_features();
  // Spin forever (prevent undefined behavior)
  for(;;);
}
