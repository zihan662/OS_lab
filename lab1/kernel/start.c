#include "types.h"
#include "memlayout.h"

extern void uartinit(void);
extern void uart_putc(char c);
extern void uart_puts(char *s);
// Kernel entry point from entry.S

void
start(void)
{
  // Initialize UART (baud rate 38400, 8-bit, no parity)
  uartinit();
  uart_puts("Hello,os");
  // Spin forever (prevent undefined behavior)
  for(;;);
}
