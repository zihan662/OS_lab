//
// Minimal UART driver for 16550A, simplified from xv6.
// Focus on synchronous output only (uart_putc, uart_puts).
// Uses macros for status bits like xv6 original.
//

#include "types.h"
#include "memlayout.h"

// UART 16550 registers (memory-mapped at UART0 = 0x10000000)
#define Reg(reg) ((volatile unsigned char *)(UART0 + (reg)))

// UART control registers (same as xv6 for consistency)
#define RHR 0                 // receive holding register (for input bytes)
#define THR 0                 // transmit holding register (for output bytes)
#define LCR 3                 // line control register
#define LCR_EIGHT_BITS (3<<0) // 8-bit data, no parity
#define LCR_BAUD_LATCH (1<<7) // special mode to set baud rate
#define LSR 5                 // line status register
#define LSR_TX_IDLE (1<<5)    // THR can accept another character to send
#define LSR_RX_READY (1<<0)   //defined but not used in minimal output-only version

#define ReadReg(reg) (*(Reg(reg)))
#define WriteReg(reg, v) (*(Reg(reg)) = (v))

// Initialize UART: set baud rate to 38400, 8-bit data, no parity
// (Simplified from xv6 uartinit - no interrupts, no FIFO)
void
uartinit(void)
{
  // disable interrupts (keep xv6 style)
  WriteReg(1, 0x00);  // IER = 0 (offset 1)

  // special mode to set baud rate (same as xv6)
  WriteReg(LCR, LCR_BAUD_LATCH);

  // LSB for baud rate of 38.4K (same as xv6)
  WriteReg(0, 0x03);

  // MSB for baud rate of 38.4K (same as xv6)
  WriteReg(1, 0x00);

  // leave set-baud mode, and set word length to 8 bits, no parity (same as xv6)
  WriteReg(LCR, LCR_EIGHT_BITS);

  // Note: Skip FIFO setup (FCR) and interrupt enable (IER) for minimal version
}

// Output one character to UART (synchronous, like xv6 uartputc_sync)
// Wait for Transmit Holding Empty to be set in LSR (THRE bit)
void
uart_putc(char c)
{
  // wait for Transmit Holding Empty to be set in LSR (same phrasing as xv6)
  while((ReadReg(LSR) & LSR_TX_IDLE) == 0)
    ;
  WriteReg(THR, c);
}

// Output a null-terminated string to UART
void
uart_puts(char *s)
{
  while(*s) {
    uart_putc(*s);
    s++;
  }
  uart_putc('\n');  // Add newline for readability
}

// Optional: Simple input function (like xv6 uartgetc, for completeness)
int
uartgetc(void)
{
  if(ReadReg(LSR) & LSR_RX_READY) {  // LSR_RX_READY (input is waiting)
    return ReadReg(RHR);
  } else {
    return -1;
  }
}
